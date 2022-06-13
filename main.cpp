#include <assert.h>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdlib.h>
#include <vector>
#include <time.h>

#include "./lock/lock.h"
#include "./threadpool/pool.h"
#include "./time/m_time.h"
#include "./http/http_conn.h"

using namespace std;

extern void addfd(int epollfd, int socketfd);
extern void setnonblocking(int socketfd);

const int MAX_FD = 65536;           //
const int MAX_EVENT_NUMBER = 10000; //EPOLL_EVENT最大监听事件数
const int MAX_WAIT_TIME = 10;       //定时器

pool<http_conn> m_threadpool(10, 10000);
//创建http连接列表
vector<http_conn *> http_user_list(MAX_FD, nullptr);
//创建用户信息列表，其中包含定时器信息
vector<client_data *> client_data_list(MAX_FD, nullptr);
int user_count = 0;

//管道(用于信号通信)，epoll监听树文件描述符，双向链表维护的所有的定时器
int pipefd[2];
int epollfd = 0; 
t_client_list m_timer_list;

string output_time(){
	time_t now = time(NULL);
	struct tm *t2, t;
	t2 = localtime(&now);
	t = *t2;
	string ret = "";
	ret += to_string((int)t.tm_year + 1900) + "-" + to_string((int)t.tm_mon + 1) + "-";
	ret += to_string((int)t.tm_mday) + " " + to_string((int)t.tm_hour) + ":";
	ret += to_string((int)t.tm_min) + ":" + to_string((int)t.tm_sec) + " ";
	return ret;
}
//LT方式监听
void addfd_lt(int epollfd, int socketfd){
	epoll_event event;
	event.data.fd = socketfd;
	event.events = EPOLLIN | EPOLLRDHUP;//?
	epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &event);
}
void addsig(int sig, void (handler)(int), bool restart = true){ //?
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if(restart) 
		sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}
void sig_handler(int sig){
	//cout << "SIGALARM TRIGGERED" << endl;
	int errno_temp = errno;
	int msg = sig;
	send(pipefd[1], (char *)&msg, 1, 0); //写入当前是哪个信号发出的信号,管道的属性：向后面的写，前面的就读出来
	//保证可重入性
	//可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    errno = errno_temp;           
}
//定时器回调函数，真正地关闭非活动连接,仅仅在定时器事件中被触发，不会主动调用
void cb_func(client_data *c_data){
	//删除非活动连接在socket上的注册事件
     epoll_ctl(epollfd, EPOLL_CTL_DEL, c_data->sockfd, 0);
     assert(c_data != nullptr);
     //关闭文件描述符
     //一开始写的注释的写法，后来发现erase是直接释放空间，弱智了，改了写法后speed 16MB->24MB
    //http_user_list.erase(http_user_list.begin() + c_data->sockfd);
    http_user_list[c_data->sockfd] = nullptr;
    //client_data_list.erase(client_data_list.begin() + c_data->sockfd);
    client_data_list[c_data->sockfd] = nullptr;
    close(c_data->sockfd);
	cerr << "your time is up :< - " << c_data->sockfd << endl;
    //减少连接数
    http_conn::m_user_count--;
}
void time_handler(){
	cerr << "tik tok" << endl;
	m_timer_list.tick();
	alarm(MAX_WAIT_TIME);
}
template <typename T>
void destroy_user_list(T user_list){
	int count = 0;
	for(auto it : user_list){
		if(it){
			count++;
			delete it;
		}
	}
	cout << output_time() << "delete " << count << " items" << "<br>" << endl;
}


int main(int argc, char *argv[]) {
	FILE *fp = nullptr;
	fp = freopen("./root/log/log.txt", "a", stdout);
	if (fp == nullptr) exit(0);
	//output_time();
	int port;
	if(argc <= 1){   //默认服务器端口
		cout << output_time() << "server start to work, open default port 12345" << "<br>" << endl;
		port = 12345;
	} else{          //指定服务器端口
		port = atoi(argv[1]);
		cout << output_time() << "server start to work, open port " << port << "<br>" << endl;
	}
	
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);
	
	//设置端口复用
	int flag = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	
	bind(listenfd, (struct sockaddr *)&address, sizeof(address));
	
	listen(listenfd, 5); //!!这都能忘？
	
	//创建内核事件表
	epoll_event event_list[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);
	addfd_lt(epollfd, listenfd); //LT方式监听？
	http_conn::m_epollfd = epollfd;
	
	//创建管道，用于定时器和其他信号消息的通知
	socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
	setnonblocking(pipefd[1]);
	addfd(epollfd, pipefd[0]);   //监听读
	
	addsig(SIGALRM, sig_handler, false); //定时器
	addsig(SIGTERM, sig_handler, false); //不带参数的kill命令
	addsig(SIGINT, sig_handler, false);  //终止进程，ctrl + c
	
	alarm(MAX_WAIT_TIME); //开始定时,定期处理超时链接
	bool timeout = false;
	bool stop_server = false;
	while(!stop_server){
		//阻塞
		int number = epoll_wait(epollfd, event_list, MAX_EVENT_NUMBER, -1); 
		for(int i = 0; i < number; i++){
			int socketfd = event_list[i].data.fd;
			if(socketfd == listenfd){
				//cout << "1" << endl;
				struct sockaddr_in client_addr;
				socklen_t client_addr_length = sizeof(client_addr);
				int connfd = accept(listenfd, (struct sockaddr *)(&client_addr), &client_addr_length);
				cerr << "new client :> - " << connfd << endl;
				//注意格式的转换
				/*char str[4096];
				printf("received from %s at PORT %d\n", 
                        inet_ntop(AF_INET, &client_addr.sin_addr, str, sizeof(str)), 
                        ntohs(client_addr.sin_port));*/
				if(connfd < 0){
					printf("can't accept!\n");
					continue;
				}
				if(http_conn::m_user_count >= MAX_FD) continue;
				if(http_user_list[connfd] == nullptr){
					cout << output_time() << "new http connected, fd = " << connfd << "<br>" << endl;
					http_user_list[connfd] = new http_conn;
				}
				http_user_list[connfd]->init(connfd, client_addr);
				
				//----创建并初始化定时器------//
				if(client_data_list[connfd] == nullptr){
				//	cout << "new timer " << connfd << endl;
					client_data_list[connfd] = new client_data;
				}
				client_data_list[connfd]->sockfd = connfd;
				client_data_list[connfd]->address = client_addr;
				
				t_client *timer = new t_client;
				timer->user_data = client_data_list[connfd];
				timer->cb_func = cb_func;
				time_t time_now = time(nullptr);
				timer->livetime = time_now + MAX_WAIT_TIME;
				
				client_data_list[connfd]->timer = timer;
				//-----------------------//
				//双向链表加入当前定时器
				m_timer_list.add_timer(timer);
				
			}  else if (event_list[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
				//cout << "2" << endl;
				cout << output_time() << "client closed, fd = " << socketfd << "<br>" << endl;
				cerr << "connection is OVER : " << strerror(errno) << endl;
                http_user_list[socketfd]->close_conn("error!");
                //关闭对应的计时器
                t_client *timer = client_data_list[socketfd]->timer;
                if (timer) //删除维护的定时器链表中的那个事件的定时器
                    m_timer_list.del_timer(timer);
			} 
			//读管道的信号一定要放在EPOLLIN前面，不然一旦发出ALARM信号就会段错误
			//管道前面的描述符可读的话
			else if((socketfd == pipefd[0]) && (event_list[i].events & EPOLLIN)){ 
				//cout << "3" << endl;
				int sig;
				char signals[1024];
				int retmsg = recv(socketfd, signals, sizeof(signals), 0);
				if(retmsg <= 0){
					cerr << "receive signal fault ? i don't think it will happen" << endl;
					continue;
				} else {
					cerr << "retmsg (i think always one) : " << retmsg << endl;
					for(int i = 0; i < retmsg; i++){
						if(signals[i] == SIGALRM){
						//cout << "SIGALARM TRIGGERED" << endl;
							timeout = true;
						} else if(signals[i] == SIGTERM || signals[i] == SIGINT){
							stop_server = true;
						}
					}
				}
			} else if(event_list[i].events & EPOLLIN){
				//cerr << "4" << endl;
				//cout << output_time() << "user request, fd = " << socketfd << endl;
				t_client *timer = client_data_list[socketfd]->timer;
				if(http_user_list[socketfd]->read()){
					//如果读到了一些数据（已经读到了读缓存区里）,就把这个请求放到工作线程的请求队列中
					m_threadpool.append(http_user_list[socketfd]);
					//针对这个活跃的连接，更新他的定时器里规定的存活事件
					if(timer){
						time_t timenow = time(NULL);
						timer->livetime = timenow + MAX_WAIT_TIME;
						m_timer_list.adjust_timer(timer);
					} else{
						cout << "didn't create timer for sockfd : " << socketfd << "<br>" << endl;
					} 
				} else{ //没有读到数据的话
					//cout << "read nothing" << endl;
					cerr << "read nothing (i don't think it will happen)" << endl;
					http_user_list[socketfd]->close_conn("read nothing");
					if(timer){
				//		cout << "del 3" << endl;
						m_timer_list.del_timer(timer);
					}
				}
			}  else if(event_list[i].events & EPOLLOUT){
				//cout << "5" << endl;
				//cout << output_time() << "server respond, fd = " << socketfd << "<br>" << endl;
				t_client *timer = client_data_list[socketfd]->timer;
				if(http_user_list[socketfd]->write()){
					//长连接的话，不关闭(NOT DONE)
					if(timer){
						//cout << "long connect" << endl;
						time_t timenow = time(NULL);
						timer->livetime = timenow + MAX_WAIT_TIME;
						//调整当前timer的位置（因为很活跃）
						m_timer_list.adjust_timer(timer);
					} else{
						//cout << " didn't create timer for socket : " << socketfd << endl;
					}
				} else{
					//短连接，关闭连接
					//cout << "write nothing" << endl;
					http_user_list[socketfd]->close_conn("write over");
					if(timer){
						m_timer_list.del_timer(timer);
					}
				}
			} 
			if(timeout){
				//cout << "timeout im here" << endl;
				time_handler();
				timeout = false;
			}
		}
	}
    cout << output_time() << "server close" << "<br>" << endl;
	close(epollfd);
	close(listenfd);
	destroy_user_list<vector<http_conn *>>(http_user_list);
    destroy_user_list<vector<client_data *>>(client_data_list);
    fclose(fp);
	return 0;
}
