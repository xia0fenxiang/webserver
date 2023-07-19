#ifndef LOCKER_H
#define LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>


// 线程同步机制所需要使用的一些封装类

// 互斥锁、条件变量、信号量

// 互斥锁类

class locker{
public:
    // 构造函数，创建互斥锁
    locker() {
        // 定义互斥锁，返回0为成功
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            // 抛出异常
            throw std::exception();
        }
    }

    // 析构函数，销毁互斥锁
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    // 上锁
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // 解锁
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    // 获取互斥锁
    pthread_mutex_t* get() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


// 条件变量类， 控制线程用
class cond{
public:
    cond() {
        // 创建条件变量， 成功返回0
        if(pthread_cond_init(&m_cond, NULL) != 0) {
            // 不成功抛出异常
            throw std::exception();
        }
    }

    ~cond () {
        // 析构函数,销毁条件变量
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }

    bool timewait(pthread_mutex_t* mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }

    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }


private:
    pthread_cond_t m_cond;
};


// 信号量类
class sem {
public:

    sem() {
        if( sem_init( &m_sem, 0, 0 ) != 0 ) {
            throw std::exception();
        }
    }

    sem(int num) {
        if( sem_init( &m_sem, 0, num ) != 0 ) {
            throw std::exception();
        }
    }

    ~sem () {
        sem_destroy(&m_sem);
    }

    // 
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }

    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

// class sem {
// public:
//     sem() {
//         if( sem_init( &m_sem, 0, 0 ) != 0 ) {
//             throw std::exception();
//         }
//     }
//     sem(int num) {
//         if( sem_init( &m_sem, 0, num ) != 0 ) {
//             throw std::exception();
//         }
//     }
//     ~sem() {
//         sem_destroy( &m_sem );
//     }
//     // 等待信号量
//     bool wait() {
//         return sem_wait( &m_sem ) == 0;
//     }
//     // 增加信号量
//     bool post() {
//         return sem_post( &m_sem ) == 0;
//     }
// private:
//     sem_t m_sem;
// };


#endif