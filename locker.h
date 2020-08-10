//
// Created by lixutong on 20-5-9.
//

#ifndef THREADPOOL_SVR_LOCKER_H
#define THREADPOOL_SVR_LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

namespace threadpoolsvr {

/* class of semaphore */
class sem {
public:
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

/* class of mutex */
class locker {
public:
    locker() {
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};

} // namespace threadpoolsvr


#endif //THREADPOOL_SVR_LOCKER_H