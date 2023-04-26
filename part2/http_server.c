#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "connection_queue.h"
#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5

int keep_going = 1;
const char *serve_dir;

void handle_sigint(int signo) {
    keep_going = 0;
}

void* thread_func(void* arg) {
    char resource_name[BUFSIZE];
    int client_fd;
    connection_queue_t* queue = (connection_queue_t *) arg;

    // loop until we receive a shutdown
    // dequeue a client fd, read its http request, and write back http response
    while(queue->shutdown != 1) {
        client_fd = connection_dequeue(queue);
        if(client_fd == -1) {
            if ((queue->shutdown) == 0) {
                printf("connection_dequeue_error\n");
            }
            return NULL;
        }

        // gets the correct directory for file requests
        if (strcpy(resource_name, serve_dir) == NULL) {
            perror("strcpy");
            close(client_fd);
            return NULL;
        }
        
        if(read_http_request(client_fd, resource_name) == -1) {
            perror("read_http");
            close(client_fd);
            return NULL;
        } // serve_dir/resource_name

        if(write_http_response(client_fd, resource_name) == -1) {
            perror("write_http");
            close(client_fd);
            return NULL;
        }

        if(close(client_fd) == -1) {
            perror("close");
            return NULL;
        }
    }
    
    return NULL;
}

int main(int argc, char **argv) {
    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }
    serve_dir = argv[1];
    const char *port = argv[2];

    // Initialize thread-safe data struct
    connection_queue_t queue;
    if (connection_queue_init(&queue) != 0) {
        printf("Failed to initialize queue\n");
        return 1;
    }
    
    // Catch SIGINT so we can clean up properly
    struct sigaction sigact;
    sigact.sa_handler = handle_sigint;
    if (sigfillset(&sigact.sa_mask) == -1) {
        perror("sigfillset");
        connection_queue_free(&queue);
        return 1;
    }

    sigact.sa_flags = 0; // Note the lack of SA_RESTART
    if (sigaction(SIGINT, &sigact, NULL) == -1) {
        perror("sigaction");
        connection_queue_free(&queue);
        return 1;
    }

    // Set up hints - we'll take either IPv4 or IPv6, TCP socket type
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // We'll be acting as a server
    struct addrinfo *server;

    // Set up address info for socket() and connect()
    int ret_val = getaddrinfo(NULL, port, &hints, &server);
    if (ret_val != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(ret_val));
        connection_queue_free(&queue);
        return 1;
    }
    // Initialize socket file descriptor
    int sock_fd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (sock_fd == -1) {
        perror("socket");
        connection_queue_free(&queue);
        freeaddrinfo(server);
        return 1;
    }
    // Bind socket to receive at a specific port
    if (bind(sock_fd, server->ai_addr, server->ai_addrlen) == -1) {
        perror("bind");
        connection_queue_free(&queue);
        freeaddrinfo(server);
        close(sock_fd);
        return 1;
    }
    freeaddrinfo(server);
    // Designate socket as a server socket
    if (listen(sock_fd, LISTEN_QUEUE_LEN) == -1) {
        perror("listen");
        connection_queue_free(&queue);
        close(sock_fd);
        return 1;
    }

    // sigprocmask to block ALL signals, save current mask
    sigset_t new_mask;
    sigset_t old_mask;
    if(sigfillset(&new_mask) == -1) {
        perror("sigfillset");
        connection_queue_free(&queue);
        return 1;
    }
    if(sigprocmask(SIG_SETMASK, &new_mask, &old_mask) == -1) {
        perror("sigprocmask");
        connection_queue_free(&queue);
        close(sock_fd);
        return 1;
    }

    // Create thread pool
    // We want to use a return code and set it for any proceeding error handling 
    // from here so we can reuse cleanup logic 
    pthread_t pool[N_THREADS];
    int result;
    int return_code = 0;
    for(int i = 0; i< N_THREADS; i++) {
        if ((result = pthread_create(pool + i, NULL, thread_func, &queue)) != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(result));
            connection_queue_free(&queue);
            return 1;
        }
    }
    // restore old mask
    if(sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1) {
        perror("sigprocmask");
        keep_going = 0;
        return_code = 1;
    }

    // Accept loop here 
    while (keep_going != 0) {
        // Wait to receive a connection request from client
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno != EINTR) {
                perror("accept");
                return_code = 1;
                break;
            } else {
                break;
            }
        }
        
        // add new client fd to queue. may block (okay)
        if(connection_enqueue(&queue, client_fd) == -1) {
            if (queue.shutdown == 0) {
                printf("Connection_enqueue error\n");
                return_code = 1;
            }
            break;
        } 
        
        // main thread no longer communicates with the new client as in Part 1.
    }

    // Cleanup, even if we had SIGINT: shutdown, wait for threads, free
    if (connection_queue_shutdown(&queue) == -1) {
        printf("Connection_queue_shutdown error\n");
        return_code = 1;
    }

    // wait for threads to terminate
    for (int i = 0; i < N_THREADS; i++) {
        int result = pthread_join(pool[i], NULL);
        if (result != 0) {
            fprintf(stderr, "pthread_join failed: %s\n", strerror(result));
            for (int j = i + 1; j < N_THREADS; j++) {
                pthread_join(pool[j], NULL);
            }
            return_code = 1;
        }
    }

    if (connection_queue_free(&queue) == -1) {
        printf("Connection_queue_free error\n");
        return_code = 1;
    } 

    if (close(sock_fd) == -1) {
        perror("close");
        return_code = 1;
    }

    // TODO Complete the rest of this function
    return return_code;
}