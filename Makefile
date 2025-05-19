# Компилятор и флаги
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -pthread
LDFLAGS := -L/usr/local/lib
LDLIBS := -lsfml-graphics -lsfml-window -lsfml-system -lboost_system -pthread

# Исходные файлы
SRC_DIR := src
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,obj/%.o,$(SRCS))

# Имя исполняемого файла
TARGET := p2p_client

# Правило по умолчанию
all: directories $(TARGET)

# Создаем необходимые директории
directories:
	@mkdir -p obj 

# Компиляция
obj/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Линковка
$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@


# Очистка
clean:
	rm -rf obj $(TARGET) 


# Запуск (после установки шрифта)
run: $(TARGET) install-font
	./$(TARGET) eth0

.PHONY: all clean directories resources install-font run