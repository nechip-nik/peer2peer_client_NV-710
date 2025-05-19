#pragma once

#include <SFML/Graphics.hpp>
#include <string>

namespace ui {
    void drawInterface(sf::RenderWindow& window, const std::string& inputIP, 
                      const std::string& inputMessage, const std::string& inputFilePath, 
                      int selectedOption, bool typingFilePath);
    bool loadFont(sf::Font& font);
}