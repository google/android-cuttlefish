/*
* Copyright (C) 2014 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "pb_decode.h"
#include <pthread.h>
#include <hardware/ril/librilutils/proto/sap-api.pb.h>
#include <utils/Log.h>

using namespace std;

/**
 * Template queue class to handling requests for a rild socket.
 * <p>
 * This class performs the following functions :
 * <ul>
 *     <li>Enqueue.
 *     <li>Dequeue.
 *     <li>Check and dequeue.
 * </ul>
 */

template <typename T>
class Ril_queue {

   /**
     * Mutex attribute used in queue mutex initialization.
     */
    pthread_mutexattr_t attr;

   /**
     * Queue mutex variable for synchronized queue access.
     */
    pthread_mutex_t mutex_instance;

   /**
     * Condition to be waited on for dequeuing.
     */
    pthread_cond_t cond;

   /**
     * Front of the queue.
     */
    T *front;

    public:

       /**
         * Remove the first element of the queue.
         *
         * @return first element of the queue.
         */
        T* dequeue(void);

       /**
         * Add a request to the front of the queue.
         *
         * @param Request to be added.
         */
        void enqueue(T* request);

       /**
         * Check if the queue is empty.
         */
        int empty(void);

       /**
         * Check and remove an element with a particular message id and token.
         *
         * @param Request message id.
         * @param Request token.
         */
        int checkAndDequeue( MsgId id, int token);

       /**
         * Queue constructor.
         */
        Ril_queue(void);
};

template <typename T>
Ril_queue<T>::Ril_queue(void) {
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(&mutex_instance, &attr);
    cond = PTHREAD_COND_INITIALIZER;
    front = NULL;
}

template <typename T>
T* Ril_queue<T>::dequeue(void) {
    T* temp = NULL;

    pthread_mutex_lock(&mutex_instance);
    while(empty()) {
        pthread_cond_wait(&cond, &mutex_instance);
    }
    temp = this->front;
    if(NULL != this->front->p_next) {
        this->front = this->front->p_next;
    } else {
        this->front = NULL;
    }
    pthread_mutex_unlock(&mutex_instance);

    return temp;
}

template <typename T>
void Ril_queue<T>::enqueue(T* request) {

    pthread_mutex_lock(&mutex_instance);

    if(NULL == this->front) {
        this->front = request;
        request->p_next = NULL;
    } else {
        request->p_next = this->front;
        this->front = request;
    }
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex_instance);
}

template <typename T>
int Ril_queue<T>::checkAndDequeue(MsgId id, int token) {
    int ret = 0;
    T* temp;

    pthread_mutex_lock(&mutex_instance);

    for(T **ppCur = &(this->front); *ppCur != NULL; ppCur = &((*ppCur)->p_next)) {
        if (token == (*ppCur)->token && id == (*ppCur)->curr->id) {
            ret = 1;
            temp = *ppCur;
            *ppCur = (*ppCur)->p_next;
            free(temp);
            break;
        }
    }

    pthread_mutex_unlock(&mutex_instance);

    return ret;
}


template <typename T>
int Ril_queue<T>::empty(void) {

    if(this->front == NULL) {
        return 1;
    } else {
        return 0;
    }
}
