#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define MAX_MSG_LEN 1024
#define BASE_PORT 8888
#define MAX_CLIENTS 100

typedef struct {
    char ip[16];
    int port;
} ClientInfo;

int sockfd;
int my_port;
struct sockaddr_in other_addr;
socklen_t addr_len = sizeof(other_addr);

void *receive_messages(void *arg) {
    char buffer[MAX_MSG_LEN];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while(1) {
        memset(buffer, 0, MAX_MSG_LEN);
        int recv_len = recvfrom(sockfd, buffer, MAX_MSG_LEN, 0, 
                              (struct sockaddr *)&sender_addr, &sender_len);
        
        if(recv_len > 0) {
            printf("\n[Message from %s:%d] %.*s\n> ", 
                  inet_ntoa(sender_addr.sin_addr),
                  ntohs(sender_addr.sin_port),
                  recv_len, buffer);
            fflush(stdout);
        }
    }
    return NULL;
}

int find_free_port() {
    int test_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(test_sock < 0) return -1;

    struct sockaddr_in test_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    for(int port = BASE_PORT; port < BASE_PORT + MAX_CLIENTS; port++) {
        test_addr.sin_port = htons(port);
        if(bind(test_sock, (struct sockaddr *)&test_addr, sizeof(test_addr)) == 0) {
            close(test_sock);
            return port;
        }
    }

    close(test_sock);
    return -1;
}

int main(int argc, char *argv[]) {
    my_port = find_free_port();
    if(my_port == -1) {
        fprintf(stderr, "Could not find free port\n");
        exit(EXIT_FAILURE);
    }

    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(my_port)
    };

    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Running client on port %d\n", my_port);
    printf("Usage: /send <ip> <port> - set recipient\n");
    printf("       /exit - quit\n\n");

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_messages, NULL);

    while(1) {
        printf("> ");
        fflush(stdout);
        
        char buffer[MAX_MSG_LEN];
        fgets(buffer, MAX_MSG_LEN, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';

        if(strcmp(buffer, "/exit") == 0) {
            break;
        }
        else if(strncmp(buffer, "/send ", 6) == 0) {
            char ip[16];
            int port;
            if(sscanf(buffer + 6, "%15s %d", ip, &port) == 2) {
                memset(&other_addr, 0, sizeof(other_addr));
                other_addr.sin_family = AF_INET;
                other_addr.sin_port = htons(port);
                if(inet_aton(ip, &other_addr.sin_addr) == 0) {
                    printf("Invalid IP address\n");
                    continue;
                }
                printf("Recipient set to %s:%d\n", ip, port);
            } else {
                printf("Usage: /send <ip> <port>\n");
            }
        }
        else if(other_addr.sin_port != 0) {
            if(sendto(sockfd, buffer, strlen(buffer), 0,
                     (struct sockaddr *)&other_addr, addr_len) < 0) {
                perror("sendto failed");
            }
        } else {
            printf("No recipient set. Use /send <ip> <port>\n");
        }
    }

    close(sockfd);
    return 0;
}