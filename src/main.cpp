#include "client_ui.hpp"
#include "network_utils.hpp"
#include "p2p_client.hpp"
#include "udp_hole_punching.hpp"
#include <SFML/Graphics.hpp>
#include <boost/asio.hpp>
#include <thread>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <interface>" << std::endl;
        return 1;
    }

    // Получаем сетевую информацию
    std::string interface(argv[1]);
    network::myIP = network::getInterfaceIP(interface);
    network::myMac = network::getMacAddress(interface);

    if (network::myIP.empty() || network::myMac.empty()) {
        std::cerr << "Failed to get network info for interface " << interface << std::endl;
        return 1;
    }

    // Инициализация SFML
    sf::RenderWindow window(sf::VideoMode(800, 720), "P2P Client");
    sf::Font font;
    if (!ui::loadFont(font)) {
        std::cerr << "Failed to load font" << std::endl;
        return 1;
    }

    // Запуск P2P сервера в отдельном потоке
    boost::asio::io_context ioContext;
    std::thread serverThread([&ioContext]() {
        p2p::runP2PServer(ioContext);
    });

    // Основной цикл
    std::string inputIP, inputMessage, inputFilePath;
    int selectedOption = 0;
    bool typing = false;
    bool typingIP = false;
    bool typingMessage = false;
    bool typingFilePath = false;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                network::running = false;
                network::serverRunning = false;
                ioContext.stop();
                window.close();
            }

            // Навигация по меню
            if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Up) {
                    selectedOption = (selectedOption - 1 + 3) % 3;
                } else if (event.key.code == sf::Keyboard::Down) {
                    selectedOption = (selectedOption + 1) % 3;
                } else if (event.key.code == sf::Keyboard::Enter) {
                    if (selectedOption == 2) {
                        network::running = false;
                        network::serverRunning = false;
                        ioContext.stop();
                        window.close();
                    }
                }
            }

            // Ввод текста
            if (event.type == sf::Event::TextEntered && typing) {
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
            if (event.type == sf::Event::MouseButtonPressed) {
                sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));

                // Поле ввода IP
                if (sf::FloatRect(100, 540, 200, 30).contains(mousePos)) {
                    typing = true;
                    typingIP = true;
                    typingMessage = false;
                    typingFilePath = false;
                }
                // Поле ввода сообщения/пути к файлу
                else if (sf::FloatRect(100, 580, 400, 30).contains(mousePos)) {
                    typing = true;
                    typingIP = false;
                    typingMessage = (selectedOption == 0);
                    typingFilePath = (selectedOption == 1);
                }
                // Кнопка отправки
                else if (sf::FloatRect(350, 640, 100, 40).contains(mousePos)) {
                    if (selectedOption == 0 && !inputIP.empty() && !inputMessage.empty()) {
                        udp_hole::punch(inputIP, ioContext);
                        p2p::sendP2PMessage(inputIP, inputMessage, ioContext);
                        inputMessage.clear();
                    } else if (selectedOption == 1 && !inputIP.empty() && !inputFilePath.empty()) {
                        udp_hole::punch(inputIP, ioContext);
                        p2p::sendP2PFile(inputIP, inputFilePath, ioContext);
                        inputFilePath.clear();
                    }
                }
            }
        }

        ui::drawInterface(window, inputIP, inputMessage, inputFilePath, selectedOption, typingFilePath);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    // Остановка сервера
    network::running = false;
    network::serverRunning = false;
    ioContext.stop();
    
    if (serverThread.joinable()) {
        serverThread.join();
    }

    return 0;
}