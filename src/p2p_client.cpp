#include "p2p_client.hpp"
#include "network_utils.hpp"
#include <boost/bind/bind.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;
namespace asio = boost::asio;
using asio::ip::tcp;

namespace p2p {
    void runP2PServer(asio::io_context& ioContext) {
        try {
            tcp::acceptor acceptor(ioContext, tcp::endpoint(tcp::v4(), 5555));
            network::addMessage("[SERVER] Started on " + network::myIP + ":5555");
    
            while (network::serverRunning) {
                tcp::socket socket(ioContext);
                
                acceptor.non_blocking(true);
                boost::system::error_code ec;
                acceptor.accept(socket, ec);
                
                if (ec) {
                    if (ec == asio::error::operation_aborted || !network::serverRunning) {
                        break;
                    }
                    if (ec == asio::error::would_block) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    network::addMessage("[SERVER] Accept error: " + ec.message());
                    continue;
                }
                
                std::string peerIP = socket.remote_endpoint().address().to_string();
                network::addMessage("[SERVER] Connection from " + peerIP);

                std::vector<char> headerBuffer(100);
                size_t headerBytes = socket.read_some(asio::buffer(headerBuffer), ec);
                
                if (ec) {
                    network::addMessage("[SERVER] Error reading header: " + ec.message());
                    continue;
                }
                
                std::string header(headerBuffer.begin(), headerBuffer.begin() + headerBytes);
                
                if (header.find("FILE:") == 0) {
                    size_t pos = header.find('\n');
                    if (pos == std::string::npos) {
                        network::addMessage("[SERVER] Invalid file header");
                        continue;
                    }
                    
                    std::string fileName = header.substr(5, pos - 5);
                    network::addMessage("[SERVER] Receiving file: " + fileName);
                    
                    std::ofstream file(fileName, std::ios::binary);
                    if (!file) {
                        network::addMessage("[SERVER] Failed to create file: " + fileName);
                        continue;
                    }
                    
                    if (headerBytes > pos + 1) {
                        file.write(&headerBuffer[pos + 1], headerBytes - (pos + 1));
                    }
                    
                    std::vector<char> fileBuffer(4096);
                    while (network::serverRunning) {
                        size_t bytesRead = socket.read_some(asio::buffer(fileBuffer), ec);
                        if (ec || bytesRead == 0) break;
                        file.write(fileBuffer.data(), bytesRead);
                    }
                    
                    file.close();
                    if (ec) {
                        network::addMessage("[SERVER] File transfer error: " + ec.message());
                    } else {
                        network::addMessage("[SERVER] File received: " + fileName);
                    }
                } else {
                    std::string message(headerBuffer.begin(), headerBuffer.begin() + headerBytes);
                    network::addMessage("[SERVER] From " + peerIP + ": " + message);
                }
            }
        } catch (const std::exception& e) {
            if (network::serverRunning) {
                network::addMessage(std::string("[SERVER ERROR] ") + e.what());
            }
        }
        network::addMessage("[SERVER] Server stopped");
    }

    void sendP2PMessage(const std::string& peerIP, const std::string& message, 
                       asio::io_context& ioContext) {
        try {
            tcp::socket socket(ioContext);
            tcp::endpoint endpoint(asio::ip::address::from_string(peerIP), 5555);
            
            socket.open(tcp::v4());
            socket.set_option(asio::ip::tcp::no_delay(true));
            asio::socket_base::linger linger_option(true, 5);
            socket.set_option(linger_option);
            
            socket.connect(endpoint);
            socket.write_some(asio::buffer(message));
            
            network::addMessage("[CLIENT] Sent to " + peerIP + ": " + message);
        } catch (const std::exception& e) {
            network::addMessage(std::string("[CLIENT ERROR] ") + e.what());
        }
    }

    void sendP2PFile(const std::string& peerIP, const std::string& filePath, 
                    asio::io_context& ioContext) {
        try {
            if (!fs::exists(filePath)) {
                network::addMessage("[CLIENT] File does not exist: " + filePath);
                return;
            }

            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file) {
                network::addMessage("[CLIENT] Failed to open file: " + filePath);
                return;
            }
            
            size_t lastSlash = filePath.find_last_of("/\\");
            std::string fileName = (lastSlash == std::string::npos) ? filePath : filePath.substr(lastSlash + 1);
            
            tcp::socket socket(ioContext);
            tcp::endpoint endpoint(asio::ip::address::from_string(peerIP), 5555);
            
            socket.open(tcp::v4());
            socket.set_option(asio::ip::tcp::no_delay(true));
            asio::socket_base::linger linger_option(true, 5);
            socket.set_option(linger_option);
            
            socket.connect(endpoint);
            
            std::string header = "FILE:" + fileName + "\n";
            socket.write_some(asio::buffer(header));
            
            file.seekg(0, std::ios::beg);
            std::vector<char> buffer(4096);
            boost::system::error_code ec;
            
            while (file && network::serverRunning) {
                file.read(buffer.data(), buffer.size());
                size_t bytesRead = file.gcount();
                if (bytesRead == 0) break;
                
                size_t bytesSent = socket.write_some(asio::buffer(buffer.data(), bytesRead), ec);
                if (ec) {
                    network::addMessage("[CLIENT] File transfer error: " + ec.message());
                    break;
                }
            }
            
            file.close();
            if (!ec) {
                network::addMessage("[CLIENT] Sent file to " + peerIP + ": " + fileName);
            }
        } catch (const std::exception& e) {
            network::addMessage(std::string("[CLIENT ERROR] ") + e.what());
        }
    }
}