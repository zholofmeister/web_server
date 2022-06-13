#ifndef M_TIME_H
#define M_TIME_H

#include <bits/stdc++.h>
#include <assert.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <vector>
using namespace std;

class t_client;
class client_data{             //用户信息
	public:
		int sockfd;           //服务器上用于与该用户对话的文件描述符
		sockaddr_in address;  //该用户的地址信息
		t_client *timer;      //定时器信息
};

class t_client{              //定时器信息
	public:
		t_client() : pre(nullptr), next(nullptr) {};
	public:
		time_t livetime;                //定时器的时间
		void (*cb_func)(client_data *); //回调函数
		t_client *pre;                  //前一个节点
		t_client *next;                 //后一个节点
		client_data *user_data;         //用户信息
};

class t_client_list{
	public:
		t_client_list() : head(nullptr), tail(nullptr) {};
		~t_client_list();
		void add_timer(t_client *timer);             //添加定时器
		void del_timer(t_client *timer);             //彻底删除定时器
		void adjust_timer(t_client *timer);          //调整定时器在链表中的位置
		void tick();                                 //定期删除超时的定时器
		t_client *remove_from_list(t_client *timer); //从链表中移除定时器
	public:
		t_client *head;   //链表头
		t_client *tail;   //链表尾
};

t_client_list::~t_client_list(){
	t_client *temp = head;
	while(temp){
		head = temp->next;
		delete temp;
		temp = head;
	}
}

//对于列表中的时间事件进行遍历，清空超时的事件
void t_client_list::tick(){
	if(!head) return;
	time_t cur = time(NULL);
	t_client *temp = head;
	while(temp) {
		//若当前遍历到的定时器没超时，那后面的肯定也没超时，就退出循环
		if(cur < temp->livetime) break;
		else {
			//如果时间到了，就关闭连接
			temp->cb_func(temp->user_data); //将client_data类型作为参数传入
		}
		head = temp->next;
		if(head) head->pre = NULL;
		delete temp; //释放t_client_list里超时的t_client对象
		temp = head;
	}
}
void t_client_list::add_timer(t_client *timer){
	if(!timer){
		cout << "not a timer" << endl;
		return;
	}
	if(!head){
		head = tail = timer;
		return;
	}
	if(timer->livetime < head->livetime){
		timer->next = head;
		head->pre = timer;
		head = timer;
		return;
	} else{
		t_client *temp = head;
		while(temp){
			//找到了要插入的位置
			//因为是插入双向链表，所以比较繁琐
			//Situation : temppre <-> timer <-> temp
			if(temp->livetime > timer->livetime){
				t_client *temppre = timer->pre;
				timer->pre = temp->pre;
				timer->next = temp;
				temppre->next = timer;
				temp->pre = timer;
			}
			temp = temp->next;
		}
		//前面都插不了，就插到尾部
		tail->next = timer;
		timer->pre = tail;
		tail = timer;
		return;
	}
}
t_client *t_client_list::remove_from_list(t_client *timer){
	if(!timer){
		return timer;
	} else if(timer == head && timer == tail){
		head = tail = nullptr;
	} else if(timer == head){
		head = head->next;
		head->pre = nullptr;
		timer->next = nullptr;
	} else if(timer == tail){
		tail = tail->pre;
		tail->next = nullptr;
		timer->pre = nullptr;
	} else{
		timer->pre->next = timer->next;
		timer->next->pre = timer->pre;
		timer->pre = nullptr;
		timer->next = nullptr;
	}
	return timer;
}
void t_client_list::del_timer(t_client *timer){
	if(!timer){
		cout << "can't remove this timer dummy!" << endl;
		return;
	}
	remove_from_list(timer);
	delete timer;
}
void t_client_list::adjust_timer(t_client *timer){
	//调整定时器位置的方法：先把定时器拿下来，然后再放上去(比较朴素，想写二分的，但已经用了双向链表了)
	if(timer){
		t_client *temp = remove_from_list(timer);
		add_timer(temp);
		//试试下面的写法，感觉一样的？
		//add_timer(timer);
	}
}
#endif
