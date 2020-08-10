//
// Created by lixutong on 20-5-9.
//

#ifndef THREADPOOL_SVR_THREADPOOL_H
#define THREADPOOL_SVR_THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include <iostream>
#include "locker.h"

namespace threadpoolsvr {

template <typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T *request);        // add task to request queue

private:
    int m_thread_number;            // num of thread in threadpool
    int m_max_requests;             // max num of requests in m_workqueue
    pthread_t *m_threads;           // array of threadpool, size is m_thread_number
    std::list<T*> m_workqueue;      // request queue
    locker m_queuelocker;           // mutex of m_workqueue
    sem m_queuestat;                // are there tasks to be done
    bool m_stop;                    // whether to stop thread

    static void* worker(void *arg); // function in work thread, it continuously fetches tasks from the work queue and executes them
    void run();
};


template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) :
        m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(nullptr) {
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }
    // create threads, and detach them
    for (int i = 0; i < thread_number; ++ i) {
        std::cout << "create the " << i << "th thread" << std::endl;
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {    // the third args must be a static function
            delete []m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete []m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete []m_threads;
    m_stop = true;
}

/* add task to request queue */
template <typename T>
bool threadpool<T>::append(T *request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // notice that there are new tasks
    return true;
}

/* worker in every thread */
template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool*)arg;
    pool->run();
    return pool;
}

/* get new task from request queue and execute it */
template <typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait(); // wait for new task
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)   continue;
        request->process();
    }
}


} // namespace threadpoolsvr

#endif //THREADPOOL_SVR_THREADPOOL_H
