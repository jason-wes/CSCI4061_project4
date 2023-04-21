#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "http.h"

#define BUFSIZE 512

const char *get_mime_type(const char *file_extension) {
    if (strcmp(".txt", file_extension) == 0) {
        return "text/plain";
    } else if (strcmp(".html", file_extension) == 0) {
        return "text/html";
    } else if (strcmp(".jpg", file_extension) == 0) {
        return "image/jpeg";
    } else if (strcmp(".png", file_extension) == 0) {
        return "image/png";
    } else if (strcmp(".pdf", file_extension) == 0) {
        return "application/pdf";
    }

    return NULL;
}

int read_http_request(int fd, char *resource_name) {
    char buf[BUFSIZE];
    char *token;
    char *resource_token;

    // make a copy of fd
    int fd_copy = dup(fd);
    if (fd_copy == -1) {
        perror("dup");
        return -1;
    }

    FILE *socket_stream = fdopen(fd_copy, "r");
    if (socket_stream == NULL) {
        perror("fdopen");
        close(fd_copy);
        return -1;
    }

    // Disable the usual stdio buffering
    if (setvbuf(socket_stream, NULL, _IONBF, 0) != 0) {
        perror("setvbuf");
        fclose(socket_stream);
        return -1;
    }

    // Keep consuming lines until we find an empty line
    while (fgets(buf, BUFSIZE, socket_stream) != NULL) {
        if (strcmp(buf, "\r\n") == 0) {
            break;
        } else {
            // if its not the last line of the request, can run the same logic on all lines
            token = strtok(buf, " ");

            // error check strtok
            if (token == NULL) {
                perror("strtok");
                fclose(socket_stream);
                return -1;
            }

            // At the beginning of the request, after GET, next strtok will give the resource name
            if (strcmp(token, "GET") == 0) {
                resource_token = strtok(NULL, " ");

                // error check strtok
                if (resource_token == NULL) {
                    perror("strtok");
                    fclose(socket_stream);
                    return -1;
                }
                
                // concatentate strings and error check
                if (strcat(resource_name, resource_token) == NULL) {
                    perror("strcat");
                    fclose(socket_stream);
                    return -1;
                }
            }
        }
    }

    if (fclose(socket_stream) != 0) {
        perror("fclose");
        return -1;
    }

    return 0;
}

int write_http_response(int fd, const char *resource_path) {
    char http_response[BUFSIZE];
    char buf[BUFSIZE];
    if (access(resource_path, F_OK) == 0) {
        // file exists
        int file = open(resource_path, O_RDONLY);
        if(file == -1) {
            perror("open");
            return -1;
        }

        // get file size
        struct stat file_info;
        if(fstat(file, &file_info) == -1) {
            perror("fstat");
            return -1;
        }
        int content_length = file_info.st_size;

        int bytes_written;
        int bytes_read;

        // get file type
        const char *extension = strrchr(resource_path, '.');
        if (extension == NULL) {
            perror("strrchr");
            close(file);
            return -1;
        }
        const char *content_type = get_mime_type(extension);
        if (content_type == NULL) {
            printf("error getting content type for http repsonse");
            close(file);
            return -1;
        }

        // constructs http_response header
        sprintf(http_response, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", content_type, content_length);

        // write http_response header
        bytes_written = write(fd, http_response, strlen(http_response));

        if (bytes_written < 0) {
            perror("write");
            close(file);
            return -1;
        }

        // read file data and write to TCP fd
        while((bytes_read = read(file, buf, BUFSIZE)) > 0) {
            if(bytes_read == -1) {
                perror("read");
                close(file);
                return -1;
            }
            int total_written = 0;
            while(total_written < bytes_read) {
                bytes_written = write(fd, buf, bytes_read - total_written);
                if(bytes_written < 0) {
                    perror("write");
                    close(file);
                    return -1;
                }
                total_written += bytes_written;
            }
        }


        // write in chunks
    } else {
        // file doesnt exist
        strcpy(http_response, "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n");
        
        write(fd, &http_response, strlen(http_response));
    }



    // TODO Not yet implemented
    return 0;
}
