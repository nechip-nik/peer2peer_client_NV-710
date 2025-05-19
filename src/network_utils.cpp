#include "network_utils.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <chrono>
#include <thread>

namespace network {
    std::string myIP;
    std::string myMac;
    std::mutex consoleMutex;
    std::queue<std::string> messageQueue;
    std::atomic<bool> running{true};
    std::atomic<bool> serverRunning{true};

    std::string getMacAddress(const std::string& interface) {
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

    std::string getInterfaceIP(const std::string& interface) {
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

    void addMessage(const std::string& msg) {
        std::lock_guard<std::mutex> lock(consoleMutex);
        messageQueue.push(msg);
        if (messageQueue.size() > 20) {
            messageQueue.pop();
        }
    }
}