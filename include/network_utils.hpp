#pragma once

#include <string>
#include <mutex>
#include <queue>
#include <atomic>
#include <cstring>

namespace network {
    std::string getMacAddress(const std::string& interface);
    std::string getInterfaceIP(const std::string& interface);
    void addMessage(const std::string& msg);
    
    extern std::string myIP;
    extern std::string myMac;
    extern std::mutex consoleMutex;
    extern std::queue<std::string> messageQueue;
    extern std::atomic<bool> running;
    extern std::atomic<bool> serverRunning;
}