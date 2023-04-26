#include <stdio.h>
#include <string.h>
#include "connection_queue.h"

int connection_queue_init(connection_queue_t *queue) {

    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    queue->shutdown = 0;

    for (int i = 0; i<CAPACITY; i++) {
        queue->client_fds[i] = -1;
    }
    int result;

    // lock for mutex lock and conditions for a full and empty queue
    if ((result = pthread_mutex_init(&queue->lock, NULL)) != 0) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_cond_init(&queue->queue_full, NULL)) != 0) {
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_cond_init(&queue->queue_empty, NULL)) != 0) {
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        return -1;
    }
    
    return 0;
}

int connection_enqueue(connection_queue_t *queue, int connection_fd) {
    int result;
    
    // grab lock
    if ((result = pthread_mutex_lock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }

    // make sure queue isnt full
    while (queue->length == CAPACITY) {
        if ((result = pthread_cond_wait(&queue->queue_full, &queue->lock)) != 0) {
            fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            return -1;
        }

        // handle shutdown - need to release lock
        if (queue->shutdown == 1) {
            if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
                fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
                return -1;
            }
            return -1;
        }
    }

    // enqueue client fd and increment length
    queue->client_fds[queue->write_idx] = connection_fd;
    queue->length++;

    // update write_idx
    if (queue->write_idx == 4) {
        queue->write_idx = 0;
    } else {
        queue->write_idx++;
    }

    // signal condition and release lock
    if ((result = pthread_cond_signal(&queue->queue_empty)) != 0) {
        fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }
    
    return 0;
}

int connection_dequeue(connection_queue_t *queue) {
    int result;

    // grab the lock
    if ((result = pthread_mutex_lock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }

    // make sure queue isnt empty
    while (queue->length == 0) {
        if ((result = pthread_cond_wait(&queue->queue_empty, &queue->lock)) != 0) {
            fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            return -1;
        }

        // handle shutdown - release lock
        if (queue->shutdown == 1) {
            if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
                fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
                return -1;
            }
            return -1;
        }
    }

    // get the current FD and decrement length
    int return_fd = queue->client_fds[queue->read_idx];
    queue->client_fds[queue->read_idx] = 1;
    queue->length--;

    // update read_idx
    if (queue->read_idx == 4) {
        queue->read_idx = 0;
    } else {
        queue->read_idx++;
    }

    // signal condition and release lock
    if ((result = pthread_cond_signal(&queue->queue_full)) != 0) {
        fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }

    if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }

    return return_fd;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    queue->shutdown = 1; // stops adding new clients inside thread_func
    int result;

    // need to grab lock then broadcast conditions to all threads
    if ((result = pthread_mutex_lock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_cond_broadcast(&queue->queue_empty)) != 0) {
        fprintf(stderr, "pthread_cond_broadcast: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    if ((result = pthread_cond_broadcast(&queue->queue_full)) != 0) {
        fprintf(stderr, "pthread_cond_broadcast: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }

    return 0;
}

int connection_queue_free(connection_queue_t *queue) {
    int result;
    // locking and unlocking theoretically not needed but added just in case
    if ((result = pthread_mutex_lock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }

    // free up conditions and mutex
    if ((result = pthread_cond_destroy(&queue->queue_full)) != 0) {
        fprintf(stderr, "pthread_cond_destroy: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    if ((result = pthread_cond_destroy(&queue->queue_empty)) != 0) {
        fprintf(stderr, "pthread_cond_destroy: %s\n", strerror(result));
        pthread_mutex_unlock(&queue->lock);
        return -1;
    }
    if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_mutex_destroy(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_destroy: %s\n", strerror(result));
        return -1;
    }
    return 0;
}
