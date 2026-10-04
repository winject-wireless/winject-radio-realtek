#include "Injector.h"

#include "DplaneClassify.h"
#include "NetUtil.h"

#include <errno.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace winject
{

Injector::Injector(SharedRadioState* state, IPacketSocket* pkt, Config cfg)
    : state_(state), pkt_(pkt), cfg_(std::move(cfg))
{
    pool_.resize(cfg_.ring_size);
    for (auto& buf : pool_)
    {
        buf.resize(1500);
    }
    retry_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}

int Injector::udp_fd() const
{
    return udp_fd_;
}

void Injector::set_udp_fd(int fd)
{
    udp_fd_ = fd;
}

void Injector::set_on_register(std::function<void(const sockaddr_in& peer)> fn)
{
    on_register_ = std::move(fn);
}

bool Injector::trusted_peer(const sockaddr_in& peer) const
{
    return sockaddr_ipv4_trusted(peer, cfg_.trusted_ipv4);
}

size_t Injector::ring_occupancy() const
{
    return ring_.size();
}

bool Injector::retry_timer_armed() const
{
    return retry_armed_;
}

int Injector::retry_timer_fd() const
{
    return retry_fd_;
}

void Injector::arm_retry_timer()
{
    if (retry_fd_ < 0)
    {
        return;
    }
    itimerspec its = {};
    its.it_value.tv_nsec = 200000;
    timerfd_settime(retry_fd_, 0, &its, nullptr);
    retry_armed_ = true;
}

void Injector::on_retry_timer()
{
    retry_armed_ = false;
    if (retry_fd_ >= 0)
    {
        uint64_t exp = 0;
        read(retry_fd_, &exp, sizeof(exp));
    }
    drain_ring();
}

void Injector::on_udp_readable()
{
    ::mmsghdr msgs[64] = {};
    iovec iov[64] = {};
    sockaddr_in peers[64] = {};
    for (unsigned i = 0; i < cfg_.batch && i < 64; ++i)
    {
        iov[i].iov_base = pool_[i % pool_.size()].data();
        iov[i].iov_len = 1500;
        msgs[i].msg_hdr.msg_iov = &iov[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &peers[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(peers[i]);
        msgs[i].msg_hdr.msg_flags = MSG_TRUNC;
    }
    const int n =
        recvmmsg(udp_fd_, msgs, std::min(cfg_.batch, 64u), MSG_DONTWAIT, nullptr);
    if (n <= 0)
    {
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        const size_t len = msgs[i].msg_len;
        const bool trunc =
            (msgs[i].msg_hdr.msg_flags & MSG_TRUNC) != 0;
        if (!trusted_peer(peers[i]))
        {
            state_->tx.dropped_invalid_frame.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        switch (dplane_classify(static_cast<uint32_t>(len), trunc))
        {
        case DplaneKind::registration:
            if (on_register_)
            {
                on_register_(peers[i]);
            }
            continue;
        case DplaneKind::invalid:
            state_->tx.dropped_invalid_frame.fetch_add(1, std::memory_order_relaxed);
            continue;
        case DplaneKind::mpdu:
            break;
        }
        state_->tx.ether_pkt.fetch_add(1, std::memory_order_relaxed);
        if (ring_.size() >= cfg_.ring_size)
        {
            state_->tx.dropped_tx_queue.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        Frame f;
        f.data.assign(static_cast<uint8_t*>(iov[i].iov_base),
                      static_cast<uint8_t*>(iov[i].iov_base) + len);
        f.enqueued = std::chrono::steady_clock::now();
        ring_.push_back(std::move(f));
    }
    drain_ring();
}

void Injector::drain_ring()
{
    const TxProfile profile = state_->tx_profile.load();
    while (!ring_.empty())
    {
        Frame f = std::move(ring_.front());
        iovec iov[2];
        iov[0].iov_base = const_cast<uint8_t*>(profile.bytes);
        iov[0].iov_len = profile.len;
        iov[1].iov_base = f.data.data();
        iov[1].iov_len = f.data.size();
        const int sent = pkt_->send_mpdu(iov, 2);
        if (sent >= 0)
        {
            state_->tx.air_pkt.fetch_add(1, std::memory_order_relaxed);
            ring_.pop_front();
            continue;
        }
        if (errno == EAGAIN || errno == ENOBUFS)
        {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - f.enqueued)
                                .count();
            if (static_cast<unsigned>(us) > cfg_.tx_retry_us)
            {
                state_->tx.dropped_wifi.fetch_add(1, std::memory_order_relaxed);
                ring_.pop_front();
                continue;
            }
            ring_.front() = std::move(f);
            arm_retry_timer();
            return;
        }
        state_->tx.dropped_wifi.fetch_add(1, std::memory_order_relaxed);
        ring_.pop_front();
    }
}

}  // namespace winject
