#ifndef WINJECT_RADIO_FORWARDER_H_
#define WINJECT_RADIO_FORWARDER_H_

#include "Counters.h"
#include "PacketSocket.h"

#include <netinet/in.h>
#include <optional>
#include <string>
#include <vector>

namespace winject
{

class Forwarder
{
public:
    struct Config
    {
        unsigned batch = 16;
        std::optional<std::string> trusted_ipv4;
    };

    Forwarder(SharedRadioState* state, IPacketSocket* pkt, Config cfg);

    int fwd_fd() const;
    void set_fwd_fd(int fd);
    void register_peer(const sockaddr_in& peer);
    void on_pkt_readable();

private:
    void flush_send_queue();

    SharedRadioState* state_;
    IPacketSocket* pkt_;
    Config cfg_;
    int fwd_fd_ = -1;
    bool have_peer_ = false;
    sockaddr_in peer_ = {};
    std::vector<std::vector<uint8_t>> send_queue_;
    std::vector<std::vector<uint8_t>> pool_;
};

}  // namespace winject

#endif  // WINJECT_RADIO_FORWARDER_H_
