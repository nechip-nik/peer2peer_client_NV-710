#include "udp_hole_punching.hpp"
#include "network_utils.hpp"
#include <boost/bind/bind.hpp>

namespace asio = boost::asio;
using asio::ip::udp;

namespace udp_hole {
    void punch(const std::string& peerIP, asio::io_context& ioContext) {
        try {
            udp::socket socket(ioContext, udp::endpoint(udp::v4(), 0));
            udp::endpoint peerEndpoint(asio::ip::address::from_string(peerIP), 5556);

            std::string punchMsg = "HOLE_PUNCH";
            socket.send_to(asio::buffer(punchMsg), peerEndpoint);
            
            network::addMessage("[UDP] Sent hole punch to " + peerIP);
        } catch (const std::exception& e) {
            network::addMessage(std::string("[UDP ERROR] ") + e.what());
        }
    }
}