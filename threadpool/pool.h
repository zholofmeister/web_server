#ifndef POOL_H
#define POOL_H

#include "../lock/lock.h"

#include <iostream>
#include <list>
#include <pthread.h>
#include <vector>

using namespace std;

template <typename T>
class pool{
	private:
		list<T *> request_list;
		vector<pthread_t> thread_list;
		locker m_lock; //给任务队列加锁
		sem m_sem;
		int m_thread_num;
		int m_max_request;
		
	private:
		static void *worker(void *arg);
		void run();
	
	public:
		pool(int thread_num = 10, int max_request = 10);
		~pool();
		bool append(T *request);
};

template <typename T>
pool<T>::pool(int thread_num, int max_request) : m_thread_num(thread_num), m_max_request(max_request) {
	cerr << "creating pool" << endl;
	//初始化thread_num个线程，后面如果不够再加
	thread_list = vector<pthread_t> (thread_num, 0);
	for(int i = 0; i < m_thread_num; i++){
		if(pthread_create(&thread_list[i], NULL, worker, this) != 0){
			cerr << "can't create thread" << endl;
		} 
		if(pthread_detach(thread_list[i]) != 0){
			cerr << "can't detach" << endl;
		}
	}
}

template <typename T>
pool<T>::~pool(){
	;
}

template <typename T>
bool pool<T>::append(T *request){
	m_lock.dolock();
	//请求队列里任务太多
	if((int)request_list.size() > m_max_request){
		m_lock.unlock();
		return false;
	}
	request_list.push_back(request);
	m_lock.unlock();
	m_sem.post();
	return true;
}

template <typename T>
void *pool<T>::worker(void *arg){
	pool<T> *pool_ptr = static_cast<pool<T> *>(arg);
	//try this :)
	//pool<T> *pool_ptr = arg
	pool_ptr->run();
}
template <typename T>
void pool<T>::run(){
	while(1){
		//cout << "im running" << endl;
		m_sem.wait();
		m_lock.dolock();  //这里互斥锁的作用是保证在某个时间，只能有一个线程对任务队列进行操作
		if(request_list.size() <= 0){ //感觉这里并不可能发生？
			cerr << "pool 80 happened? i don't think so" << endl;
			m_lock.unlock();
		} else{
			if(int(request_list.size()) == 0){
				m_lock.unlock();
				continue;
			}
			T* request = request_list.front();
			request_list.pop_front(); //若没有元素给你pop，会抛出异常
			m_lock.unlock(); // !!虽然要尽早释放，但前提是等工作做完
			/*if(!request){
				continue;
			}*/
			request->process();
		}
	}
}

#endif
