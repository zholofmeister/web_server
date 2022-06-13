#ifndef LOCK_H
#define LOCK_H

#include <pthread.h>
#include <semaphore.h>
//封装信号量和锁
class sem 
{
	private:
		sem_t m_sem;
		
	public:
		sem()
		{
			sem_init(&m_sem, 0, 0);
		}
		~sem()
		{
			sem_destroy(&m_sem);
		}
		bool wait() //p操作，-1
		{
			return (sem_wait(&m_sem) == 0);
		}
		bool post() //v操作，+1
		{
			return (sem_post(&m_sem) == 0);
		}
};

class locker
{
	private:
		pthread_mutex_t m_lock;
		
	public:
		locker()
		{
			pthread_mutex_init(&m_lock,0);
		}
		~locker()
		{
			pthread_mutex_destroy(&m_lock);
		}
		bool dolock()
		{ 
			return (pthread_mutex_lock(&m_lock)==0);
		}
		bool unlock()
		{
			return (pthread_mutex_unlock(&m_lock) == 0);
		}
};
#endif
