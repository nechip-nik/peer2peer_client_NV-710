#pragma once

#include <boost/asio.hpp>

namespace udp_hole {
    void punch(const std::string& peerIP, boost::asio::io_context& ioContext);
}