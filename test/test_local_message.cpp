#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>

using namespace std;

const int P2P_PORT = 5555;
const int FILE_BUFFER_SIZE = 4096;

string myIP;
string myMac;

string getMacAddress(const string& interface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ-1);
    
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(fd);
        return "";
    }
    
    close(fd);
    
    unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return string(macStr);
}

string getInterfaceIP(const string& interface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ-1);
    
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        close(fd);
        return "";
    }
    
    close(fd);
    return inet_ntoa(((sockaddr_in*)&ifr.ifr_addr)->sin_addr);
}

void handleClient(int clientSocket, const string& clientIP) {
    char buffer[FILE_BUFFER_SIZE];
    int n = recv(clientSocket, buffer, sizeof(buffer), 0);
    if (n <= 0) {
        close(clientSocket);
        return;
    }
    
    string request(buffer, n);
    
    if (request.find("FILE:") == 0) {
        // Извлечение имени файла
        size_t headerEndPos = request.find('\n');
        if (headerEndPos == string::npos) {
            cerr << "Invalid file header format" << endl;
            close(clientSocket);
            return;
        }
    
        string fileName = request.substr(5, headerEndPos - 5);
        fileName = fileName.substr(fileName.find_last_of('/') + 1);
    
        ofstream file(fileName, ios::binary);
        if (!file) {
            cerr << "Failed to create file: " << fileName << endl;
            close(clientSocket);
            return;
        }
    
        cout << "Receiving file: " << fileName << " from " << clientIP << endl;
    
        // Пишем остаток данных из первого буфера
        size_t dataStartPos = headerEndPos + 1;
        if (dataStartPos < n) {
            file.write(buffer + dataStartPos, n - dataStartPos);
        }
    
        // Дополнительно читаем данные
        while ((n = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
            file.write(buffer, n);
        }
    
        file.close();
        cout << "File received: " << fileName << endl;
    } else {
        // Обычное сообщение
        cout << "Message from " << clientIP << ": " << request << endl;
    }
    
    close(clientSocket);
}

void runP2PServer(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port); // <- Теперь порт из аргумента
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 5) < 0) {
        perror("listen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    cout << "P2P server started on " << myIP << ":" << port << endl;

    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(sockfd, (sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            perror("accept");
            continue;
        }

        string clientIP = inet_ntoa(clientAddr.sin_addr);
        thread(handleClient, clientSocket, clientIP).detach();
    }

    close(sockfd);
}

void sendMessage(const string& peerIP, const string& message) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return;
    }
    
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(P2P_PORT);
    if (inet_pton(AF_INET, peerIP.c_str(), &serverAddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return;
    }
    
    if (connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        close(sockfd);
        return;
    }
    
    send(sockfd, message.c_str(), message.size(), 0);
    close(sockfd);
}

void sendFile(const string& peerIP, const string& filePath) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return;
    }
    
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(P2P_PORT);
    if (inet_pton(AF_INET, peerIP.c_str(), &serverAddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return;
    }
    
    if (connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        close(sockfd);
        return;
    }
    
    ifstream file(filePath, ios::binary);
    if (!file) {
        cerr << "Failed to open file: " << filePath << endl;
        close(sockfd);
        return;
    }
    
    string fileName = filePath.substr(filePath.find_last_of('/') + 1);
    string header = "FILE:" + fileName + "\n";
    send(sockfd, header.c_str(), header.size(), 0);
    
    char buffer[FILE_BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer))) {
        send(sockfd, buffer, file.gcount(), 0);
    }
    
    if (file.gcount() > 0) {
        send(sockfd, buffer, file.gcount(), 0);
    }
    
    file.close();
    close(sockfd);
    cout << "File sent: " << filePath << " to " << peerIP << endl;
}

void userInterface() {
    while (true) {
        cout << "\nOptions:\n"
             << "1. Send message\n"
             << "2. Send file\n"
             << "3. Exit\n"
             << "Choice: ";
        
        int choice;
        cin >> choice;
        cin.ignore(); // Очистка буфера
        
        switch (choice) {
            case 1: {
                string ip, message;
                cout << "Enter peer IP: ";
                getline(cin, ip);
                cout << "Enter message: ";
                getline(cin, message);
                sendMessage(ip, message);
                break;
            }
            case 2: {
                string ip, filePath;
                cout << "Enter peer IP: ";
                getline(cin, ip);
                cout << "Enter file path: ";
                getline(cin, filePath);
                sendFile(ip, filePath);
                break;
            }
            case 3:
                exit(0);
            default:
                cout << "Invalid choice" << endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <interface> <port>" << endl;
        return EXIT_FAILURE;
    }

    string interface(argv[1]);
    int port = atoi(argv[2]);

    myIP = getInterfaceIP(interface);
    myMac = getMacAddress(interface);

    if (myIP.empty() || myMac.empty()) {
        cerr << "Failed to get network information for interface " << interface << endl;
        return EXIT_FAILURE;
    }

    cout << "P2P Client started on " << myIP << ":" << port << " (MAC: " << myMac << ")" << endl;

    // Передаём порт в сервер
    thread serverThread(runP2PServer, port);
    userInterface();

    return 0;
}