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
#include <SFML/Graphics.hpp>

using namespace std;
using namespace sf;

const int P2P_PORT = 5555;
const int FILE_BUFFER_SIZE = 4096;

string myIP;
string myMac;
mutex consoleMutex;

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

void handleClient(int clientSocket, const string& clientIP, vector<string>& messages) {
    char buffer[FILE_BUFFER_SIZE];
    int n = recv(clientSocket, buffer, sizeof(buffer), 0);
    if (n <= 0) {
        close(clientSocket);
        return;
    }
    
    string request(buffer, n);
    
    if (request.find("FILE:") == 0) {
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
    
        {
            lock_guard<mutex> lock(consoleMutex);
            messages.push_back("Receiving file: " + fileName + " from " + clientIP);
        }
    
        size_t dataStartPos = headerEndPos + 1;
        if (dataStartPos < n) {
            file.write(buffer + dataStartPos, n - dataStartPos);
        }
    
        while ((n = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
            file.write(buffer, n);
        }
    
        file.close();
        {
            lock_guard<mutex> lock(consoleMutex);
            messages.push_back("File received: " + fileName);
        }
    } else {
        lock_guard<mutex> lock(consoleMutex);
        messages.push_back("Message from " + clientIP + ": " + request);
    }
    
    close(clientSocket);
}

void runP2PServer(vector<string>& messages) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(P2P_PORT);
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
    
    {
        lock_guard<mutex> lock(consoleMutex);
        messages.push_back("P2P server started on " + myIP + ":" + to_string(P2P_PORT));
    }
    
    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(sockfd, (sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            perror("accept");
            continue;
        }
        
        string clientIP = inet_ntoa(clientAddr.sin_addr);
        thread(handleClient, clientSocket, clientIP, ref(messages)).detach();
    }
    
    close(sockfd);
}

void sendMessage(const string& peerIP, const string& message, vector<string>& messages) {
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
    
    lock_guard<mutex> lock(consoleMutex);
    messages.push_back("Message sent to " + peerIP + ": " + message);
}

void sendFile(const string& peerIP, const string& filePath, vector<string>& messages) {
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
    
    lock_guard<mutex> lock(consoleMutex);
    messages.push_back("File sent to " + peerIP + ": " + filePath);
}

void drawInterface(RenderWindow& window, Font& font, vector<string>& messages, 
                  string& inputIP, string& inputMessage, string& inputFilePath, 
                  int& selectedOption) {
    window.clear(Color::White);
    
    // Рисуем заголовок
    Text title("P2P Client", font, 30);
    title.setFillColor(Color::Black);
    title.setPosition(20, 20);
    window.draw(title);
    
    // Рисуем информацию о клиенте
    Text clientInfo("Your IP: " + myIP + " MAC: " + myMac, font, 20);
    clientInfo.setFillColor(Color::Black);
    clientInfo.setPosition(20, 70);
    window.draw(clientInfo);
    
    // Рисуем лог сообщений
    Text logTitle("Message Log:", font, 20);
    logTitle.setFillColor(Color::Black);
    logTitle.setPosition(20, 120);
    window.draw(logTitle);
    
    RectangleShape logBackground(Vector2f(760, 300));
    logBackground.setFillColor(Color(240, 240, 240));
    logBackground.setPosition(20, 150);
    window.draw(logBackground);
    
    for (size_t i = 0; i < messages.size(); ++i) {
        Text message(messages[i], font, 16);
        message.setFillColor(Color::Black);
        message.setPosition(30, 160 + i * 25);
        
        // Ограничиваем количество отображаемых сообщений
        if (160 + i * 25 < 430) {
            window.draw(message);
        }
    }
    
    // Рисуем меню выбора
    vector<string> options = {
        "1. Send message",
        "2. Send file",
        "3. Exit"
    };
    
    for (size_t i = 0; i < options.size(); ++i) {
        Text option(options[i], font, 20);
        option.setFillColor(i == selectedOption ? Color::Red : Color::Black);
        option.setPosition(20, 470 + i * 30);
        window.draw(option);
    }
    
    // Рисуем поля ввода в зависимости от выбранной опции
    if (selectedOption == 0) {
        Text ipLabel("Peer IP:", font, 18);
        ipLabel.setFillColor(Color::Black);
        ipLabel.setPosition(20, 560);
        window.draw(ipLabel);
        
        RectangleShape ipBox(Vector2f(200, 30));
        ipBox.setFillColor(Color::White);
        ipBox.setOutlineColor(Color::Black);
        ipBox.setOutlineThickness(1);
        ipBox.setPosition(100, 560);
        window.draw(ipBox);
        
        Text ipText(inputIP, font, 18);
        ipText.setFillColor(Color::Black);
        ipText.setPosition(105, 565);
        window.draw(ipText);
        
        Text msgLabel("Message:", font, 18);
        msgLabel.setFillColor(Color::Black);
        msgLabel.setPosition(20, 600);
        window.draw(msgLabel);
        
        RectangleShape msgBox(Vector2f(400, 30));
        msgBox.setFillColor(Color::White);
        msgBox.setOutlineColor(Color::Black);
        msgBox.setOutlineThickness(1);
        msgBox.setPosition(100, 600);
        window.draw(msgBox);
        
        Text msgText(inputMessage, font, 18);
        msgText.setFillColor(Color::Black);
        msgText.setPosition(105, 605);
        window.draw(msgText);
    } else if (selectedOption == 1) {
        Text ipLabel("Peer IP:", font, 18);
        ipLabel.setFillColor(Color::Black);
        ipLabel.setPosition(20, 560);
        window.draw(ipLabel);
        
        RectangleShape ipBox(Vector2f(200, 30));
        ipBox.setFillColor(Color::White);
        ipBox.setOutlineColor(Color::Black);
        ipBox.setOutlineThickness(1);
        ipBox.setPosition(100, 560);
        window.draw(ipBox);
        
        Text ipText(inputIP, font, 18);
        ipText.setFillColor(Color::Black);
        ipText.setPosition(105, 565);
        window.draw(ipText);
        
        Text fileLabel("File path:", font, 18);
        fileLabel.setFillColor(Color::Black);
        fileLabel.setPosition(20, 600);
        window.draw(fileLabel);
        
        RectangleShape fileBox(Vector2f(400, 30));
        fileBox.setFillColor(Color::White);
        fileBox.setOutlineColor(Color::Black);
        fileBox.setOutlineThickness(1);
        fileBox.setPosition(100, 600);
        window.draw(fileBox);
        
        Text fileText(inputFilePath, font, 18);
        fileText.setFillColor(Color::Black);
        fileText.setPosition(105, 605);
        window.draw(fileText);
    }
    
    // Рисуем кнопку отправки
    RectangleShape sendButton(Vector2f(100, 40));
    sendButton.setFillColor(Color::Green);
    sendButton.setPosition(350, 650);
    window.draw(sendButton);
    
    Text sendText("Send", font, 20);
    sendText.setFillColor(Color::White);
    sendText.setPosition(375, 655);
    window.draw(sendText);
    
    window.display();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <interface>" << endl;
        return EXIT_FAILURE;
    }
    
    string interface(argv[1]);
    myIP = getInterfaceIP(interface);
    myMac = getMacAddress(interface);
    
    if (myIP.empty() || myMac.empty()) {
        cerr << "Failed to get network information for interface " << interface << endl;
        return EXIT_FAILURE;
    }
    
    // Создаем графическое окно
    RenderWindow window(VideoMode(800, 720), "P2P Client");
    Font font;
    if (!font.loadFromFile("/usr/share/fonts/truetype/msttcorefonts/arial.ttf")) {
        cerr << "Failed to load font" << endl;
        return EXIT_FAILURE;
    }
    
    vector<string> messages;
    messages.push_back("P2P Client started on " + myIP + " (MAC: " + myMac + ")");
    
    thread serverThread(runP2PServer, ref(messages));
    
    string inputIP;
    string inputMessage;
    string inputFilePath;
    int selectedOption = 0;
    bool typing = false;
    bool typingIP = false;
    bool typingMessage = false;
    bool typingFilePath = false;
    
    while (window.isOpen()) {
        Event event;
        while (window.pollEvent(event)) {
            if (event.type == Event::Closed) {
                window.close();
            }
            
            if (event.type == Event::KeyPressed) {
                if (event.key.code == Keyboard::Up) {
                    selectedOption = (selectedOption - 1 + 3) % 3;
                } else if (event.key.code == Keyboard::Down) {
                    selectedOption = (selectedOption + 1) % 3;
                } else if (event.key.code == Keyboard::Enter) {
                    if (selectedOption == 2) {
                        window.close();
                    }
                }
            }
            
            if (event.type == Event::TextEntered && typing) {
                if (event.text.unicode == '\b') {
                    if (typingIP && !inputIP.empty()) {
                        inputIP.pop_back();
                    } else if (typingMessage && !inputMessage.empty()) {
                        inputMessage.pop_back();
                    } else if (typingFilePath && !inputFilePath.empty()) {
                        inputFilePath.pop_back();
                    }
                } else if (event.text.unicode < 128) {
                    if (typingIP) {
                        inputIP += static_cast<char>(event.text.unicode);
                    } else if (typingMessage) {
                        inputMessage += static_cast<char>(event.text.unicode);
                    } else if (typingFilePath) {
                        inputFilePath += static_cast<char>(event.text.unicode);
                    }
                }
            }
            
            if (event.type == Event::MouseButtonPressed) {
                if (event.mouseButton.button == Mouse::Left) {
                    Vector2f mousePos = window.mapPixelToCoords(Mouse::getPosition(window));
                    
                    // Проверяем клик по полям ввода
                    if (selectedOption == 0) {
                        if (FloatRect(100, 560, 200, 30).contains(mousePos)) {
                            typing = true;
                            typingIP = true;
                            typingMessage = false;
                            typingFilePath = false;
                        } else if (FloatRect(100, 600, 400, 30).contains(mousePos)) {
                            typing = true;
                            typingIP = false;
                            typingMessage = true;
                            typingFilePath = false;
                        }
                    } else if (selectedOption == 1) {
                        if (FloatRect(100, 560, 200, 30).contains(mousePos)) {
                            typing = true;
                            typingIP = true;
                            typingMessage = false;
                            typingFilePath = false;
                        } else if (FloatRect(100, 600, 400, 30).contains(mousePos)) {
                            typing = true;
                            typingIP = false;
                            typingMessage = false;
                            typingFilePath = true;
                        }
                    }
                    
                    // Проверяем клик по кнопке отправки
                    if (FloatRect(350, 650, 100, 40).contains(mousePos)) {
                        if (selectedOption == 0 && !inputIP.empty() && !inputMessage.empty()) {
                            sendMessage(inputIP, inputMessage, messages);
                            inputMessage.clear();
                        } else if (selectedOption == 1 && !inputIP.empty() && !inputFilePath.empty()) {
                            sendFile(inputIP, inputFilePath, messages);
                            inputFilePath.clear();
                        }
                    }
                }
            }
        }
        
        drawInterface(window, font, messages, inputIP, inputMessage, inputFilePath, selectedOption);
    }
    
    serverThread.detach();
    return 0;
}