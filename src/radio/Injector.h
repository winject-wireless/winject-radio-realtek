#ifndef WINJECT_RADIO_INJECTOR_H_
#define WINJECT_RADIO_INJECTOR_H_

#include "Counters.h"
#include "PacketSocket.h"
#include "TxProfile.h"

#include <chrono>
#include <deque>
#include <functional>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <vector>

namespace winject
{

class Injector
{
public:
    struct Config
    {
        unsigned ring_size = 20;
        unsigned batch = 16;
        unsigned tx_retry_us = 50000;
        std::optional<std::string> trusted_ipv4;
    };

    Injector(SharedRadioState* state, IPacketSocket* pkt, Config cfg);

    int udp_fd() const;
    void set_udp_fd(int fd);
    void set_on_register(std::function<void(const sockaddr_in& peer)> fn);
    void on_udp_readable();
    void on_retry_timer();
    bool retry_timer_armed() const;
    int retry_timer_fd() const;
    void arm_retry_timer();
    size_t ring_occupancy() const;

private:
    struct Frame
    {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point enqueued;
    };

    void drain_ring();
    bool trusted_peer(const sockaddr_in& peer) const;

    std::function<void(const sockaddr_in&)> on_register_;
    SharedRadioState* state_;
    IPacketSocket* pkt_;
    Config cfg_;
    int udp_fd_ = -1;
    int retry_fd_ = -1;
    std::deque<Frame> ring_;
    std::vector<std::vector<uint8_t>> pool_;
    bool retry_armed_ = false;
};

}  // namespace winject

#endif  // WINJECT_RADIO_INJECTOR_H_
