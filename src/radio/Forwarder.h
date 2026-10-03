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

    int reg_fd() const;
    int fwd_fd() const;
    void set_reg_fd(int fd);
    void set_fwd_fd(int fd);
    void on_reg_readable();
    void on_pkt_readable();

private:
    bool trusted_peer(const sockaddr_in& peer) const;
    void flush_send_queue();

    SharedRadioState* state_;
    IPacketSocket* pkt_;
    Config cfg_;
    int reg_fd_ = -1;
    int fwd_fd_ = -1;
    bool have_peer_ = false;
    sockaddr_in peer_ = {};
    std::vector<std::vector<uint8_t>> send_queue_;
    std::vector<std::vector<uint8_t>> pool_;
};

}  // namespace winject

#endif  // WINJECT_RADIO_FORWARDER_H_
