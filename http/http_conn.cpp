#include "http_conn.h"
#include <fstream> 
#include <iostream>
#include <time.h>
#include <assert.h>

using namespace std;
string output_date(){
	time_t now = time(NULL);
	struct tm *t2, t;
	t2 = localtime(&now);
	t = *t2;
	string ret = "";
	ret += " " + to_string((int)t.tm_hour) + ":" + to_string((int)t.tm_min);
	ret += ":" + to_string((int)t.tm_sec);
	return ret;
}
void setnonblocking(int socketfd)
{
    int old_opt = fcntl(socketfd, F_GETFL);
    int new_opt = old_opt | O_NONBLOCK;
    fcntl(socketfd, F_SETFL, new_opt);
}

void addfd(int epollfd, int socketfd)
{
    //把该描述符添加到epoll的事件表
    epoll_event m_event;                                   //新建一个事件
    m_event.data.fd = socketfd;                            //使得描述符为本描述符
    //本来要判断recv是不是等于0来判断对端socket是否关闭，现在只要看由于没有EPOLLRDHUP事件
    //EPOLLRDHUP减少了一次系统调用，优化了性能
    m_event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;       //监听输入、边缘触发、挂起
    epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &m_event); //将该事件添加
    setnonblocking(socketfd);                              //设置非阻塞
}

void removefd(int epollfd, int socketfd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, socketfd, 0); //把该套接字的对应事件删除
    close(socketfd);
}

void modfd(int epollfd, int socketed, int ev) {
    epoll_event m_event;
    m_event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    m_event.data.fd = socketed;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, socketed, &m_event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::init(int socketfd, const sockaddr_in &addr) //套接字的初始化，用于添加进epoll的事件表
{
    m_socket = socketfd;
    m_addr = addr;
    addfd(m_epollfd, m_socket);
    init();
}

void http_conn::init()
{
    filename = "";
    memset(read_buff, 0, BUFF_READ_SIZE);   //清空读缓冲
    memset(write_buff, 0, BUFF_WRITE_SIZE); //清空写缓冲
    read_for_now = 0;
    write_for_now = 0;
}

void http_conn::close_conn(string msg)
{
    //将当前的描述符在epoll监听事件里面去除
    if (m_socket != -1)
    {
        removefd(m_epollfd, m_socket);
        m_user_count--;
        m_socket = -1; //-1就是代表没有正在连接的套接字
    }
}

//对请求进行处理
void http_conn::process() 
{
    //首先进行报文的解析
    HTTP_CODE ret = process_read();
    if (ret == NO_REQUEST) { //没读完整，继续读
		cerr << "NO_REQUEST ? it will not happen!" << endl;
        modfd(m_epollfd, m_socket, EPOLLIN);
        return;
    }
    //然后进行报文的响应
    bool result = process_write(ret);
    //最后向epoll的监听的事件表中添加可写事件
    modfd(m_epollfd, m_socket, EPOLLOUT); 
}
//解析请求行
void http_conn::parser_requestline(const string &text, map<string, string> &m_map) {
    string m_method = text.substr(0, text.find(" "));
    string m_url = text.substr(text.find_first_of(" ") + 1, text.find_last_of(" ") - text.find_first_of(" ") - 1);
    string m_protocol = text.substr(text.find_last_of(" ") + 1);
    m_map["method"] = m_method;
    m_map["url"] = m_url;
    m_map["protocol"] = m_protocol;
}
//解析请求头部
void http_conn::parser_header(const string &text, map<string, string> &m_map){
    if (int(text.size()) > 0) {
        if (text.find(": ") <= text.size()) {
            string m_type = text.substr(0, text.find(": "));
            string m_content = "";
            if(text.find(": ") + 2 < int(text.size())) m_content = text.substr(text.find(": ") + 2);
            m_map[m_type] = m_content;
        } else if (text.find("=") <= text.size()) {
            string m_type = text.substr(0, text.find("="));
            string m_content = "";
            if(text.find("=") + 1 < int(text.size())) m_content = text.substr(text.find("=") + 1);
            m_map[m_type] = m_content;
        }
    }
}

void http_conn::parser_postinfo(const string &text, map<string, string> &m_map)
{
    //username=zzy&passwd=123456&votename=red
    //cerr << "post:   " << text << endl;
    string processd = "";   //当前正在处理的字符串
    string strleft = text;  //剩余待处理的字符串
    while (true)
    {
        processd = strleft.substr(0, strleft.find("&"));
        m_map[processd.substr(0, processd.find("="))] = processd.substr(processd.find("=") + 1);
        if (strleft.find("&") == string::npos) //后面没有&了，处理完了
            break;
        strleft = strleft.substr(strleft.find("&") + 1);
    }
}

//从状态机驱动主状态机
http_conn::HTTP_CODE http_conn::process_read(){ 
    //m_head : 当前正在处理的报文段，m_left : 剩余未处理的报文段
    string m_head = "";
    string m_left = read_buff; //把读入缓冲区的数据转化为string 
    int flag = 0;
    int do_post_flag = 0;
    while (1) {
        //对每一行进行处理
        m_head = m_left.substr(0, m_left.find("\r\n"));
        m_left = m_left.substr(m_left.find("\r\n") + 2);
        if (flag == 0) {
            flag = 1;
            //解析请求行
            parser_requestline(m_head, m_map);
        }
        //解析请求内容
        else if (do_post_flag) {
            parser_postinfo(m_head, m_map);
            break;
        }
        //解析请求头部
        else{
            parser_header(m_head, m_map);
        }
        if (m_head == "")  //处理到空行了
            do_post_flag = 1;
        if (m_left == "")  //报文都解析完了
            break;
    }
    if (m_map["method"] == "POST"){
        return POST_REQUEST;
    } else if (m_map["method"] == "GET")
    {
        return GET_REQUEST;
    } else {
		cerr << "can't happen! http_conn.cpp 179" << endl;
        return NO_REQUEST;
    }
}
//解析报文，得到请求资源的路径
bool http_conn::do_request(){
    //区分get和post都请求了那些文件或网页
   // cerr << "method: " << m_map["method"] << " url: " << m_map["url"] << endl;
    if (m_map["method"] == "POST"){
		//cout << "ok " << m_map["url"] << endl;
		//拿到可以操作Redis数据库的对象
        redis_clt *m_redis = redis_clt::getinstance();
        if (m_map["url"] == "/base.html" || m_map["url"] == "/") {//如果来自于登录界面
            //处理登录请求
            //cerr << "处理登录请求" << endl;
          //  cerr << "user's passwd: " << m_redis->getUserpasswd(m_map["username"]) << endl;
            //cerr << "we got : " << m_map["passwd"] << endl;
            if (m_redis->getUserpasswd(m_map["username"]) == m_map["passwd"]){
                if (m_redis->getUserpasswd(m_map["username"]) == "root"){
                    filename = "./root/redirect.html"; //登录进入管理员欢迎界面(NOT DONE)
				}
                else {
                    //filename = "./root/welcome.html"; //登录进入欢迎界面
                    filename = "./root/redirect.html";
				}
            }
            else {
                //cerr << "登录失败界面" << endl;
                filename = "./root/error.html"; //进入登录失败界面
            }
        }else if(m_map["url"] == "/5"){ //投票页面
			filename = "./root/vote.html";
		} else if(m_map["url"] == "/6"){ //投票统计
			filename = "./root/chart.html";
		} else if(m_map["url"] == "/7"){ //聊天室
			filename = "./root/chat.html";
		} else if(m_map["url"] == "/8"){ //日志
			filename = "./root/log.html";
		}
		//如果来自注册界面
        else if (m_map["url"] == "/register.html") {
            m_redis->setUserpasswd(m_map["username"], m_map["passwd"]);
            filename = "./root/register.html"; //注册后进入初始登录界面
        } else if (m_map["url"] == "/vote.html") {
            m_redis->vote(m_map["votename"]);
            postmsg = "";
            return false;
        }
        else if (m_map["url"] == "/getvote") { //如果主页要请求投票
            //读取redis,并且redis.cpp把返回内容转化为json形式
          //  cerr << "get read vote !" << endl;
            postmsg = m_redis->getvoteboard();
        //    filename = "./root/error.html";
         //   cerr << postmsg << endl;
            return false;
        } else if(m_map["url"] == "/chat.html"){
			//cout << "m_map[content] = " << m_map["content"] << endl;
			//cout << "m_map[user_id] = " << m_map["user_id"] << endl;
			ofstream outfile;    //定义输出流对象
			outfile.open("./root/log/chat.txt", ios::app);    //打开文件
			if (!outfile){
				cerr << "打开output.txt文件失败" << endl;
				exit(-1);
			}
			outfile << m_map["user_id"] << ' ';
			outfile << " 在 " << output_date() << " 说 : <br>\n";
			outfile << "<br>" << '\n';
			string temp_content = "";
			for(char c : m_map["content"]){
				if(c == '+') temp_content += ' ';
				else temp_content += c;
			}
			outfile << temp_content << "<br><br><br>" << '\n';
			outfile.close();    //关闭文件
			postmsg = "";
			return false;
		} 
        else {
            filename = "./root/base.html"; //进入初始登录界面
        }
    }
    else if (m_map["method"] == "GET"){
        if (m_map["url"] == "/"){
            m_map["url"] = "/base.html";
        } 
        filename = "./root" + m_map["url"];
    }
    else{
        filename = "./root/error.html";
    }
    return true;
    //cout << "return filename : " << filename << endl;
}
void http_conn::unmap(){
    if (file_addr){
        munmap(file_addr, m_file_stat.st_size);
        file_addr = 0;
    }
}
bool http_conn::process_write(HTTP_CODE ret) { //去取http请求报文要的资源

    if (do_request()){
        int fd = open(filename.c_str(), O_RDONLY);
        //取得指定文件的文件属性
        stat(filename.c_str(), &m_file_stat);
        //将文件映射入内存
        //0：系统随意分配起点，PROT_READ:内存段可读，MAP_PRIVATE：拷贝，内存区域的写入不会影响到原文件
        file_addr = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        m_iovec[2].iov_base = file_addr;
        m_iovec[2].iov_len = m_file_stat.st_size;
        m_iovec_length = 3;
        close(fd); //居然忘记关闭描述符了
    } else{  
		//投票和聊天室
        if (postmsg != ""){
            if(int(postmsg.length()) < 20)
                cerr <<"wrong pstmsg : "<< postmsg << endl;
            strcpy(post_temp, postmsg.c_str());
            m_iovec[2].iov_base = post_temp;
            m_iovec[2].iov_len = (postmsg.size()) * sizeof(char);
            m_iovec_length = 3;
        }
        else {
            m_iovec_length = 1;
        }

    }

    return true;
}

bool http_conn::read() {//把socket的东西全部读到读缓冲区里面
    if (read_for_now > BUFF_READ_SIZE) {//如果当前可以写入读缓冲区的位置已经超出了缓冲区长度了
        cerr << "it won't happen, http_conn.cpp 335" << endl;
        return false;
    }
    int bytes_read = 0;
    while (true){
        bytes_read = recv(m_socket, read_buff + read_for_now, BUFF_READ_SIZE - read_for_now, 0);
        if (bytes_read == -1) {//出现错误
            if (errno == EAGAIN || errno == EWOULDBLOCK) { //读不下，暂时没遇到过??
			//	assert(0);
                break;
            }
            return false;
        }
        else if (bytes_read == 0) {
            cerr << "bytes_read == 0" << endl;
            return false; 
            continue;
        }
        read_for_now += bytes_read;
    }
   // cerr << read_buff << endl;
	//cerr << "*******************" << endl;
    return true;
}

bool http_conn::write() {//把响应的内容写到写缓冲区中,并说明该连接是否为长连接
    int bytes_write = 0;
    string response_head = "HTTP/1.1 233 OVO\r\n";
    char head_temp[(int)response_head.size()];
    strcpy(head_temp, response_head.c_str());
    m_iovec[0].iov_base = head_temp;
    m_iovec[0].iov_len = response_head.size() * sizeof(char);
    
    string se = "WHO_IS_THIS:ZZY\r\nContent-Length: ";
    se += to_string(m_iovec[2].iov_len);
    se += "\r\n\r\n";
    char se_temp[(int)se.size()];
    strcpy(se_temp, se.c_str());
    m_iovec[1].iov_base = se_temp;
    m_iovec[1].iov_len = se.size() * sizeof(char);

    bytes_write = writev(m_socket, m_iovec, m_iovec_length);

    if (bytes_write <= 0){
        return false;
    }
    unmap();
    return false;
    
    if (m_map["Connection"] == "keep-alive"){
        return true;
    } else {
        return false;
    }
}
