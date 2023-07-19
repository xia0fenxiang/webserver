#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include"locker.h"
#include<cstdio>


// 线程池类，定义成模板类是为了代码的复用，模板参数T是任务类


template<typename T>
class threadpool {

public:
    // 构造
    threadpool(int thread_number = 8, int max_requests = 10000);
    // 析构
    ~threadpool();

    // 添加请求到请求队列
    bool append(T* request);

private:

    static void* worker(void* arg);
    void run();
private:
    // 线程的数量
    int m_thread_number;

    // 线程数组 大小为m_thread_number
    pthread_t* m_threads;

    // 请求队列中，最多允许等待处理的请求的数量
    int m_max_requests;

    // 请求队列
    std::list<T*> m_workqueue;

    // 互斥锁
    locker m_queue_mutex;

    // 信号量来判断是否有任务需要处理
    sem m_queue_sem;

    // 是否结束线程
    bool m_stop;
};
// 初始化列表方式初始化参数： threadpool(int XXX, int XXX) : m_thread_number(thread_number) ........ {}
template<typename T>
threadpool< T >::threadpool(int thread_number, int max_requests): 
        m_thread_number(thread_number), m_max_requests(max_requests), 
        m_stop(false), m_threads(NULL) {
    if((thread_number == 0) || (max_requests <= 0)) {
        throw std:: exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }

    // 创建thread_number个线程， 并将他们设置成线程脱离
    for(int i = 0; i < m_thread_number; i++) {
        printf("creating %dth thread\n", i);
        // 创建线程， 线程号为m_threads + i, 线程执行函数worker，传参this指针
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std:: exception();
        }
        // 设置线程分离
        if(pthread_detach(m_threads[i]) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}


template<typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;

}

// 向工作队列中添加任务，主体流程：上锁、添加、解锁、信号量加一
template<typename T>
bool threadpool<T>::append(T* request) {

    //  线程同步上互斥锁
    m_queue_mutex.lock();
    // 如果当前工作队列超过最大任务量，解锁返回添加失败
    if(m_workqueue.size() > m_max_requests) {
        m_queue_mutex.unlock();
        return false;
    }
    // 正常的话， 工作队列添加请求
    m_workqueue.push_back(request);
    // 线程同步， 解互斥锁
    m_queue_mutex.unlock();
    // 任务列表信号量+1
    m_queue_sem.post();
    return true;
}

// 子线程里的工作：运行run函数的工作
template<typename T>
void* threadpool<T>::worker(void* arg) {

    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}


// run函数是要进行的处理：通过信号量判断是否有工作需要处理，如有，进行如下操作：
// 上锁、拿到请求、解锁（需要线程同步)、处理
template<typename T>
void threadpool<T>::run() {
    while(!m_stop) {
        // 判断有无任务需要完成，没有的话会阻塞， 有的话信号量减一
        m_queue_sem.wait();
        // 往下运行即为有任务
        // 互斥锁上锁
        m_queue_mutex.lock();
        // 如果工作列表为空，解锁继续循环
        if(m_workqueue.empty()) {
            m_queue_mutex.unlock();
            continue;
        }

        // 得到请求
        T* request = m_workqueue.front();
        // 从工作列表中删除该请求，并解锁
        m_workqueue.pop_front();
        m_queue_mutex.unlock();

        // 如果没拿到请求， 继续循环
        if(!request) {
            continue;
        }

        // 执行请求的处理程序
        request->process();
    }
} 



#endif