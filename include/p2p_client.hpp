#pragma once

#include <boost/asio.hpp>

namespace p2p {
    void runP2PServer(boost::asio::io_context& ioContext);
    void sendP2PMessage(const std::string& peerIP, const std::string& message, 
                       boost::asio::io_context& ioContext);
    void sendP2PFile(const std::string& peerIP, const std::string& filePath, 
                    boost::asio::io_context& ioContext);
}