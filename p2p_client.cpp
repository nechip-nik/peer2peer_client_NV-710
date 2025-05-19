#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <SFML/Graphics.hpp>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;
using namespace sf;
namespace fs = std::filesystem;
namespace asio = boost::asio;
using asio::ip::tcp;
using asio::ip::udp;
using boost::placeholders::_1;
using boost::placeholders::_2;

// Конфигурация
const int P2P_PORT = 5555;
const int UDP_HOLE_PUNCH_PORT = 5556;
const int BUFFER_SIZE = 4096;
const int MAX_MESSAGES = 20;

// Глобальные переменные
string myIP;
string myMac;
mutex consoleMutex;
queue<string> messageQueue;
atomic<bool> running{true};
atomic<bool> serverRunning{true};
Font font;

// Получаем MAC-адрес
string getMacAddress(const string& interface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifreq ifr{};
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("ioctl");
        close(fd);
        return "";
    }
    close(fd);

    unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return macStr;
}

// Получаем IP-адрес интерфейса
string getInterfaceIP(const string& interface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifreq ifr{};
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        perror("ioctl");
        close(fd);
        return "";
    }
    close(fd);
    return inet_ntoa(((sockaddr_in*)&ifr.ifr_addr)->sin_addr);
}

// Добавляем сообщение в очередь
void addMessage(const string& msg) {
    lock_guard<mutex> lock(consoleMutex);
    messageQueue.push(msg);
    if (messageQueue.size() > MAX_MESSAGES) {
        messageQueue.pop();
    }
}

// P2P сервер (принимает соединения)
void runP2PServer(asio::io_context& ioContext) {
    try {
        tcp::acceptor acceptor(ioContext, tcp::endpoint(tcp::v4(), P2P_PORT));
        addMessage("[SERVER] Started on " + myIP + ":" + to_string(P2P_PORT));

        while (serverRunning) {
            tcp::socket socket(ioContext);
            
            // Устанавливаем неблокирующий режим для accept
            acceptor.non_blocking(true);
            boost::system::error_code ec;
            acceptor.accept(socket, ec);
if (ec) {
    if (ec == asio::error::operation_aborted || !serverRunning) {
        break;
    }
    if (ec == asio::error::would_block) {
        this_thread::sleep_for(chrono::milliseconds(100));
        continue;
    }
    addMessage("[SERVER] Accept error: " + ec.message());
    continue;
}
            
            string peerIP = socket.remote_endpoint().address().to_string();
            addMessage("[SERVER] Connection from " + peerIP);

            // Читаем заголовок (первые 100 байт)
            vector<char> headerBuffer(100);
            size_t headerBytes = socket.read_some(asio::buffer(headerBuffer), ec);
            
            if (ec) {
                addMessage("[SERVER] Error reading header: " + ec.message());
                continue;
            }
            
            string header(headerBuffer.begin(), headerBuffer.begin() + headerBytes);
            
            if (header.find("FILE:") == 0) {
                // Обработка файла
                size_t pos = header.find('\n');
                if (pos == string::npos) {
                    addMessage("[SERVER] Invalid file header");
                    continue;
                }
                
                string fileName = header.substr(5, pos - 5);
                addMessage("[SERVER] Receiving file: " + fileName);
                
                ofstream file(fileName, ios::binary);
                if (!file) {
                    addMessage("[SERVER] Failed to create file: " + fileName);
                    continue;
                }
                
                // Записываем оставшиеся данные из заголовка
                if (headerBytes > pos + 1) {
                    file.write(&headerBuffer[pos + 1], headerBytes - (pos + 1));
                }
                
                // Читаем остальные данные файла
                vector<char> fileBuffer(BUFFER_SIZE);
                while (serverRunning) {
                    size_t bytesRead = socket.read_some(asio::buffer(fileBuffer), ec);
                    if (ec || bytesRead == 0) break;
                    file.write(fileBuffer.data(), bytesRead);
                }
                
                file.close();
                if (ec) {
                    addMessage("[SERVER] File transfer error: " + ec.message());
                } else {
                    addMessage("[SERVER] File received: " + fileName);
                }
            } else {
                // Обычное сообщение
                string message(headerBuffer.begin(), headerBuffer.begin() + headerBytes);
                addMessage("[SERVER] From " + peerIP + ": " + message);
            }
        }
    } catch (const exception& e) {
        if (serverRunning) {
            addMessage(string("[SERVER ERROR] ") + e.what());
        }
    }
    addMessage("[SERVER] Server stopped");
}

// Отправка сообщения через P2P
void sendP2PMessage(const string& peerIP, const string& message, asio::io_context& ioContext) {
    try {
        tcp::socket socket(ioContext);
        tcp::endpoint endpoint(asio::ip::address::from_string(peerIP), P2P_PORT);
        
        // Устанавливаем таймаут
        socket.open(tcp::v4());
        socket.set_option(asio::ip::tcp::no_delay(true));
        asio::socket_base::linger linger_option(true, 5);
        socket.set_option(linger_option);
        
        socket.connect(endpoint);
        socket.write_some(asio::buffer(message));
        
        addMessage("[CLIENT] Sent to " + peerIP + ": " + message);
    } catch (const exception& e) {
        addMessage(string("[CLIENT ERROR] ") + e.what());
    }
}

// Отправка файла через P2P
void sendP2PFile(const string& peerIP, const string& filePath, asio::io_context& ioContext) {
    try {
        // Проверяем существование файла
        if (!fs::exists(filePath)) {
            addMessage("[CLIENT] File does not exist: " + filePath);
            return;
        }

        // Проверяем возможность открытия файла
        ifstream file(filePath, ios::binary | ios::ate);
        if (!file) {
            addMessage("[CLIENT] Failed to open file: " + filePath);
            return;
        }
        
        // Получаем имя файла (без пути)
        size_t lastSlash = filePath.find_last_of("/\\");
        string fileName = (lastSlash == string::npos) ? filePath : filePath.substr(lastSlash + 1);
        
        tcp::socket socket(ioContext);
        tcp::endpoint endpoint(asio::ip::address::from_string(peerIP), P2P_PORT);
        
        // Устанавливаем таймаут
        socket.open(tcp::v4());
        socket.set_option(asio::ip::tcp::no_delay(true));
        asio::socket_base::linger linger_option(true, 5);
        socket.set_option(linger_option);
        
        socket.connect(endpoint);
        
        // Формируем заголовок
        string header = "FILE:" + fileName + "\n";
        
        // Отправляем заголовок
        socket.write_some(asio::buffer(header));
        
        // Отправляем содержимое файла
        file.seekg(0, ios::beg);
        vector<char> buffer(BUFFER_SIZE);
        boost::system::error_code ec;
        
        while (file && serverRunning) {
            file.read(buffer.data(), buffer.size());
            size_t bytesRead = file.gcount();
            if (bytesRead == 0) break;
            
            size_t bytesSent = socket.write_some(asio::buffer(buffer.data(), bytesRead), ec);
            if (ec) {
                addMessage("[CLIENT] File transfer error: " + ec.message());
                break;
            }
        }
        
        file.close();
        if (!ec) {
            addMessage("[CLIENT] Sent file to " + peerIP + ": " + fileName);
        }
    } catch (const exception& e) {
        addMessage(string("[CLIENT ERROR] ") + e.what());
    }
}

// UDP Hole Punching
void udpHolePunching(const string& peerIP, asio::io_context& ioContext) {
    try {
        udp::socket socket(ioContext, udp::endpoint(udp::v4(), 0));
        udp::endpoint peerEndpoint(asio::ip::address::from_string(peerIP), UDP_HOLE_PUNCH_PORT);

        string punchMsg = "HOLE_PUNCH";
        socket.send_to(asio::buffer(punchMsg), peerEndpoint);
        
        addMessage("[UDP] Sent hole punch to " + peerIP);
    } catch (const exception& e) {
        addMessage(string("[UDP ERROR] ") + e.what());
    }
}

// Отрисовка интерфейса
// Отрисовка интерфейса
void drawInterface(RenderWindow& window, const string& inputIP, const string& inputMessage, 
    const string& inputFilePath, int selectedOption, bool typingFilePath) {
window.clear(Color(240, 240, 240));

// Заголовок
Text title("P2P Client", font, 30);
title.setFillColor(Color::Black);
title.setPosition(20, 20);
window.draw(title);

// Информация о клиенте
Text clientInfo("IP: " + myIP + "  MAC: " + myMac, font, 18);
clientInfo.setFillColor(Color::Black);
clientInfo.setPosition(20, 70);
window.draw(clientInfo);

// Лог сообщений
RectangleShape logBackground(Vector2f(760, 300));
logBackground.setFillColor(Color::White);
logBackground.setOutlineColor(Color(200, 200, 200));
logBackground.setOutlineThickness(2);
logBackground.setPosition(20, 120);
window.draw(logBackground);

// Отображаем сообщения
lock_guard<mutex> lock(consoleMutex);
queue<string> tempQueue = messageQueue;
int yPos = 130;
while (!tempQueue.empty() && yPos < 400) {
Text msg(tempQueue.front(), font, 16);
msg.setFillColor(Color::Black);
msg.setPosition(30, yPos);
window.draw(msg);
tempQueue.pop();
yPos += 20;
}

// Меню
vector<string> options = {
"1. Send message",
"2. Send file",
"3. Exit"
};

for (size_t i = 0; i < options.size(); ++i) {
Text option(options[i], font, 20);
option.setFillColor(i == selectedOption ? Color::Red : Color::Black);
option.setPosition(20, 450 + i * 30);
window.draw(option);
}

// Поля ввода и кнопка Send только если выбран не Exit
if (selectedOption != 2) {
if (selectedOption == 0) {
// Поля для сообщения
Text ipLabel("Peer IP:", font, 18);
ipLabel.setFillColor(Color::Black);
ipLabel.setPosition(20, 540);
window.draw(ipLabel);

RectangleShape ipBox(Vector2f(200, 30));
ipBox.setFillColor(Color::White);
ipBox.setOutlineColor(Color::Black);
ipBox.setOutlineThickness(1);
ipBox.setPosition(100, 540);
window.draw(ipBox);

Text ipText(inputIP, font, 18);
ipText.setFillColor(Color::Black);
ipText.setPosition(105, 545);
window.draw(ipText);

Text msgLabel("Message:", font, 18);
msgLabel.setFillColor(Color::Black);
msgLabel.setPosition(20, 580);
window.draw(msgLabel);

RectangleShape msgBox(Vector2f(400, 30));
msgBox.setFillColor(Color::White);
msgBox.setOutlineColor(Color::Black);
msgBox.setOutlineThickness(1);
msgBox.setPosition(100, 580);
window.draw(msgBox);

Text msgText(inputMessage, font, 18);
msgText.setFillColor(Color::Black);
msgText.setPosition(105, 585);
window.draw(msgText);
} else if (selectedOption == 1) {
// Поля для файла
Text ipLabel("Peer IP:", font, 18);
ipLabel.setFillColor(Color::Black);
ipLabel.setPosition(20, 540);
window.draw(ipLabel);

RectangleShape ipBox(Vector2f(200, 30));
ipBox.setFillColor(Color::White);
ipBox.setOutlineColor(Color::Black);
ipBox.setOutlineThickness(1);
ipBox.setPosition(100, 540);
window.draw(ipBox);

Text ipText(inputIP, font, 18);
ipText.setFillColor(Color::Black);
ipText.setPosition(105, 545);
window.draw(ipText);

Text fileLabel("File path:", font, 18);
fileLabel.setFillColor(Color::Black);
fileLabel.setPosition(20, 580);
window.draw(fileLabel);

RectangleShape fileBox(Vector2f(400, 30));
fileBox.setFillColor(Color::White);
fileBox.setOutlineColor(Color::Black);
fileBox.setOutlineThickness(1);
fileBox.setPosition(100, 580);
window.draw(fileBox);

Text fileText(inputFilePath, font, 18);
fileText.setFillColor(Color::Black);
fileText.setPosition(105, 585);
window.draw(fileText);
}

// Кнопка отправки
RectangleShape sendButton(Vector2f(100, 40));
sendButton.setFillColor(Color(100, 200, 100));
sendButton.setPosition(350, 640);
window.draw(sendButton);

Text sendText("Send", font, 20);
sendText.setFillColor(Color::White);
sendText.setPosition(375, 645);
window.draw(sendText);
}

window.display();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <interface>" << endl;
        return 1;
    }

    // Получаем сетевую информацию
    string interface(argv[1]);
    myIP = getInterfaceIP(interface);
    myMac = getMacAddress(interface);

    if (myIP.empty() || myMac.empty()) {
        cerr << "Failed to get network info for interface " << interface << endl;
        return 1;
    }

    // Инициализация SFML
    RenderWindow window(VideoMode(800, 720), "P2P Client");
    if (!font.loadFromFile("/usr/share/fonts/truetype/msttcorefonts/arial.ttf")) {
        cerr << "Failed to load font" << endl;
        return 1;
    }

    // Запуск P2P сервера в отдельном потоке
    asio::io_context ioContext;
    thread serverThread([&ioContext]() {
        runP2PServer(ioContext);
    });

    // Основной цикл
    string inputIP, inputMessage, inputFilePath;
    int selectedOption = 0;
    bool typing = false;
    bool typingIP = false;
    bool typingMessage = false;
    bool typingFilePath = false;

    while (window.isOpen()) {
        Event event;
        while (window.pollEvent(event)) {
            if (event.type == Event::Closed) {
                running = false;
                serverRunning = false;
                
                // Останавливаем сервер
                ioContext.stop();
                
                // Закрываем окно
                window.close();
            }

            // Навигация по меню
            if (event.type == Event::KeyPressed) {
                if (event.key.code == Keyboard::Up) {
                    selectedOption = (selectedOption - 1 + 3) % 3;
                } else if (event.key.code == Keyboard::Down) {
                    selectedOption = (selectedOption + 1) % 3;
                } else if (event.key.code == Keyboard::Enter) {
                    if (selectedOption == 2) {
                        running = false;
                        serverRunning = false;
                        ioContext.stop();
                        window.close();
                    }
                }
            }

            // Ввод текста
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

            // Клики мышью
            if (event.type == Event::MouseButtonPressed) {
                Vector2f mousePos = window.mapPixelToCoords(Mouse::getPosition(window));

                // Поле ввода IP
                if (FloatRect(100, 540, 200, 30).contains(mousePos)) {
                    typing = true;
                    typingIP = true;
                    typingMessage = false;
                    typingFilePath = false;
                }
                // Поле ввода сообщения/пути к файлу
                else if (FloatRect(100, 580, 400, 30).contains(mousePos)) {
                    typing = true;
                    typingIP = false;
                    typingMessage = (selectedOption == 0);
                    typingFilePath = (selectedOption == 1);
                }
                // Кнопка отправки
                else if (FloatRect(350, 640, 100, 40).contains(mousePos)) {
                    if (selectedOption == 0 && !inputIP.empty() && !inputMessage.empty()) {
                        // Сначала пробуем hole punching
                        udpHolePunching(inputIP, ioContext);
                        // Затем отправляем сообщение
                        sendP2PMessage(inputIP, inputMessage, ioContext);
                        inputMessage.clear();
                    } else if (selectedOption == 1 && !inputIP.empty() && !inputFilePath.empty()) {
                        // Сначала пробуем hole punching
                        udpHolePunching(inputIP, ioContext);
                        // Затем отправляем файл
                        sendP2PFile(inputIP, inputFilePath, ioContext);
                        inputFilePath.clear();
                    }
                }
            }
        }

        drawInterface(window, inputIP, inputMessage, inputFilePath, selectedOption, typingFilePath);
        this_thread::sleep_for(chrono::milliseconds(30));
    }

    // Остановка сервера
    running = false;
    serverRunning = false;
    ioContext.stop();
    
    if (serverThread.joinable()) {
        serverThread.join();
    }

    return 0;
}
// /usr/share/fonts/truetype/msttcorefonts/arial.ttf