#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

// Функция для приёма сообщений
void *receive_messages(void *arg) {
    int client_socket = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            printf("Соединение разорвано.\n");
            break;
        }
        printf("Получено: %s\n", buffer);
    }

    close(client_socket);
    pthread_exit(NULL);
}

// Функция для отправки сообщений
void *send_messages(void *arg) {
    int client_socket = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        printf("Введите сообщение: ");
        fgets(buffer, BUFFER_SIZE, stdin);

        // Убираем символ новой строки
        buffer[strcspn(buffer, "\n")] = 0;

        if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
            perror("Ошибка отправки сообщения");
            break;
        }
    }

    close(client_socket);
    pthread_exit(NULL);
}

// Основная функция
void start_peer(const char *host, int port, const char *peer_host, int peer_port) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Создаём сокет
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Ошибка создания сокета");
        exit(EXIT_FAILURE);
    }

    // Настраиваем адрес сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(host);

    // Привязываем сокет к адресу
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Ошибка привязки сокета");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Ожидаем подключения
    if (listen(server_socket, 1) < 0) {
        perror("Ошибка ожидания подключения");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Ожидание подключения на %s:%d...\n", host, port);

    // Если указан адрес другого пира, подключаемся к нему
    if (peer_host && peer_port > 0) {
        printf("Подключение к %s:%d...\n", peer_host, peer_port);

        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket < 0) {
            perror("Ошибка создания клиентского сокета");
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(peer_port);
        peer_addr.sin_addr.s_addr = inet_addr(peer_host);

        if (connect(client_socket, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
            perror("Ошибка подключения к пиру");
            close(client_socket);
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        printf("Подключение установлено с %s:%d\n", peer_host, peer_port);
    } else {
        // Иначе ждём подключения другого пира
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0) {
            perror("Ошибка принятия подключения");
            close(server_socket);
            exit(EXIT_FAILURE);
        }

        printf("Подключение установлено с %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    }

    // Запускаем потоки для приёма и отправки сообщений
    pthread_t receive_thread, send_thread;
    pthread_create(&receive_thread, NULL, receive_messages, &client_socket);
    pthread_create(&send_thread, NULL, send_messages, &client_socket);

    pthread_join(receive_thread, NULL);
    pthread_join(send_thread, NULL);

    close(client_socket);
    close(server_socket);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Использование: %s <host> <port> [peer_host] [peer_port]\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *peer_host = (argc > 3) ? argv[3] : NULL;
    int peer_port = (argc > 4) ? atoi(argv[4]) : 0;

    start_peer(host, port, peer_host, peer_port);

    return 0;
}