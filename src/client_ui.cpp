#include "client_ui.hpp"
#include "network_utils.hpp"
#include <SFML/Graphics.hpp>

namespace ui {
    bool loadFont(sf::Font& font) {
        return font.loadFromFile("/usr/share/fonts/truetype/msttcorefonts/arial.ttf");
    }

    void drawInterface(sf::RenderWindow& window, const std::string& inputIP, 
                      const std::string& inputMessage, const std::string& inputFilePath, 
                      int selectedOption, bool typingFilePath) {
        window.clear(sf::Color(240, 240, 240));

        sf::Font font;
        if (!loadFont(font)) return;

        // Заголовок
        sf::Text title("P2P Client", font, 30);
        title.setFillColor(sf::Color::Black);
        title.setPosition(20, 20);
        window.draw(title);

        // Информация о клиенте
        sf::Text clientInfo("IP: " + network::myIP + "  MAC: " + network::myMac, font, 18);
        clientInfo.setFillColor(sf::Color::Black);
        clientInfo.setPosition(20, 70);
        window.draw(clientInfo);

        // Лог сообщений
        sf::RectangleShape logBackground(sf::Vector2f(760, 300));
        logBackground.setFillColor(sf::Color::White);
        logBackground.setOutlineColor(sf::Color(200, 200, 200));
        logBackground.setOutlineThickness(2);
        logBackground.setPosition(20, 120);
        window.draw(logBackground);

        // Отображаем сообщения
        std::lock_guard<std::mutex> lock(network::consoleMutex);
        std::queue<std::string> tempQueue = network::messageQueue;
        int yPos = 130;
        while (!tempQueue.empty() && yPos < 400) {
            sf::Text msg(tempQueue.front(), font, 16);
            msg.setFillColor(sf::Color::Black);
            msg.setPosition(30, yPos);
            window.draw(msg);
            tempQueue.pop();
            yPos += 20;
        }

        // Меню
        std::vector<std::string> options = {
            "1. Send message",
            "2. Send file",
            "3. Exit"
        };

        for (size_t i = 0; i < options.size(); ++i) {
            sf::Text option(options[i], font, 20);
            option.setFillColor(i == selectedOption ? sf::Color::Red : sf::Color::Black);
            option.setPosition(20, 450 + i * 30);
            window.draw(option);
        }

        // Поля ввода и кнопка Send только если выбран не Exit
        if (selectedOption != 2) {
            if (selectedOption == 0) {
                // Поля для сообщения
                sf::Text ipLabel("Peer IP:", font, 18);
                ipLabel.setFillColor(sf::Color::Black);
                ipLabel.setPosition(20, 540);
                window.draw(ipLabel);

                sf::RectangleShape ipBox(sf::Vector2f(200, 30));
                ipBox.setFillColor(sf::Color::White);
                ipBox.setOutlineColor(sf::Color::Black);
                ipBox.setOutlineThickness(1);
                ipBox.setPosition(100, 540);
                window.draw(ipBox);

                sf::Text ipText(inputIP, font, 18);
                ipText.setFillColor(sf::Color::Black);
                ipText.setPosition(105, 545);
                window.draw(ipText);

                sf::Text msgLabel("Message:", font, 18);
                msgLabel.setFillColor(sf::Color::Black);
                msgLabel.setPosition(20, 580);
                window.draw(msgLabel);

                sf::RectangleShape msgBox(sf::Vector2f(400, 30));
                msgBox.setFillColor(sf::Color::White);
                msgBox.setOutlineColor(sf::Color::Black);
                msgBox.setOutlineThickness(1);
                msgBox.setPosition(100, 580);
                window.draw(msgBox);

                sf::Text msgText(inputMessage, font, 18);
                msgText.setFillColor(sf::Color::Black);
                msgText.setPosition(105, 585);
                window.draw(msgText);
            } else if (selectedOption == 1) {
                // Поля для файла
                sf::Text ipLabel("Peer IP:", font, 18);
                ipLabel.setFillColor(sf::Color::Black);
                ipLabel.setPosition(20, 540);
                window.draw(ipLabel);

                sf::RectangleShape ipBox(sf::Vector2f(200, 30));
                ipBox.setFillColor(sf::Color::White);
                ipBox.setOutlineColor(sf::Color::Black);
                ipBox.setOutlineThickness(1);
                ipBox.setPosition(100, 540);
                window.draw(ipBox);

                sf::Text ipText(inputIP, font, 18);
                ipText.setFillColor(sf::Color::Black);
                ipText.setPosition(105, 545);
                window.draw(ipText);

                sf::Text fileLabel("File path:", font, 18);
                fileLabel.setFillColor(sf::Color::Black);
                fileLabel.setPosition(20, 580);
                window.draw(fileLabel);

                sf::RectangleShape fileBox(sf::Vector2f(400, 30));
                fileBox.setFillColor(sf::Color::White);
                fileBox.setOutlineColor(sf::Color::Black);
                fileBox.setOutlineThickness(1);
                fileBox.setPosition(100, 580);
                window.draw(fileBox);

                sf::Text fileText(inputFilePath, font, 18);
                fileText.setFillColor(sf::Color::Black);
                fileText.setPosition(105, 585);
                window.draw(fileText);
            }

            // Кнопка отправки
            sf::RectangleShape sendButton(sf::Vector2f(100, 40));
            sendButton.setFillColor(sf::Color(100, 200, 100));
            sendButton.setPosition(350, 640);
            window.draw(sendButton);

            sf::Text sendText("Send", font, 20);
            sendText.setFillColor(sf::Color::White);
            sendText.setPosition(375, 645);
            window.draw(sendText);
        }

        window.display();
    }
}