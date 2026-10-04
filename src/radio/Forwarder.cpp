#include "Forwarder.h"

#include "Fcs.h"
#include "Log.h"
#include "Radiotap.h"
#include "config_types.h"

#include <algorithm>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

namespace winject
{

Forwarder::Forwarder(SharedRadioState* state, IPacketSocket* pkt, Config cfg)
    : state_(state), pkt_(pkt), cfg_(std::move(cfg))
{
    pool_.resize(cfg_.batch);
    for (auto& b : pool_)
    {
        b.resize(2048);
    }
}

int Forwarder::fwd_fd() const
{
    return fwd_fd_;
}

void Forwarder::set_fwd_fd(int fd)
{
    fwd_fd_ = fd;
}

void Forwarder::register_peer(const sockaddr_in& peer)
{
    const bool changed =
        !have_peer_ || peer_.sin_addr.s_addr != peer.sin_addr.s_addr ||
        peer_.sin_port != peer.sin_port;
    peer_ = peer;
    have_peer_ = true;
    if (changed)
    {
        char addr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &peer.sin_addr, addr, sizeof(addr));
        LOG_INF("peer %s:%u", addr, ntohs(peer.sin_port));
    }
}

void Forwarder::flush_send_queue()
{
    if (!have_peer_ || send_queue_.empty())
    {
        return;
    }
    // send_queue_ holds at most one recv batch (tune.rx_batch <= 64).
    ::mmsghdr msgs[64] = {};
    iovec iov[64] = {};
    const unsigned count =
        static_cast<unsigned>(std::min<size_t>(send_queue_.size(), 64));
    for (unsigned i = 0; i < count; ++i)
    {
        iov[i].iov_base = send_queue_[i].data();
        iov[i].iov_len = send_queue_[i].size();
        msgs[i].msg_hdr.msg_iov = &iov[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &peer_;
        msgs[i].msg_hdr.msg_namelen = sizeof(peer_);
    }
    const int sent = sendmmsg(fwd_fd_, msgs, count, MSG_DONTWAIT);
    if (sent < 0)
    {
        state_->rx.dropped_send_failed.fetch_add(
            static_cast<uint32_t>(send_queue_.size()), std::memory_order_relaxed);
        send_queue_.clear();
        return;
    }
    state_->rx.ether_pkt.fetch_add(static_cast<uint32_t>(sent),
                                   std::memory_order_relaxed);
    if (static_cast<size_t>(sent) < send_queue_.size())
    {
        state_->rx.dropped_send_failed.fetch_add(
            static_cast<uint32_t>(send_queue_.size() - sent),
            std::memory_order_relaxed);
    }
    send_queue_.clear();
}

void Forwarder::on_pkt_readable()
{
    uint32_t drops = 0;
    pkt_->read_statistics(&drops);
    if (drops > 0)
    {
        state_->rx.dropped_rx_queue.fetch_add(drops, std::memory_order_relaxed);
    }

    ::mmsghdr msgs[64] = {};
    iovec iov[64] = {};
    for (unsigned i = 0; i < cfg_.batch && i < 64; ++i)
    {
        iov[i].iov_base = pool_[i % pool_.size()].data();
        iov[i].iov_len = pool_[i].size();
        msgs[i].msg_hdr.msg_iov = &iov[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }
    const int n = pkt_->recv_batch(msgs, std::min(cfg_.batch, 64u));
    if (n <= 0)
    {
        return;
    }
    const mac_filter filter =
        mac_filter_unpack(state_->rx_filter_packed.load(std::memory_order_acquire));

    for (int i = 0; i < n; ++i)
    {
        state_->rx.air_pkt.fetch_add(1, std::memory_order_relaxed);
        const uint8_t* buf = static_cast<uint8_t*>(iov[i].iov_base);
        const size_t len = msgs[i].msg_len;
        RxRadiotapInfo rt = radiotap_parse_rx(buf, len);
        if (!rt.ok)
        {
            state_->rx.dropped_filter_mismatched.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (rt.has_tx_flags)
        {
            state_->rx.dropped_filter_mismatched.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const uint8_t* mpdu = buf + rt.rt_len;
        size_t mlen = len - rt.rt_len;
        std::vector<uint8_t> out;
        if (rt.flags & kRadiotapFcs)
        {
            out.assign(mpdu, mpdu + mlen);
        }
        else
        {
            out.assign(mpdu, mpdu + mlen);
            uint8_t fcs[4];
            wifi_fcs_store(mpdu, mlen, fcs);
            out.insert(out.end(), fcs, fcs + 4);
            mlen += 4;
        }
        if (mlen < 28 || mlen > 1504)
        {
            state_->rx.dropped_filter_mismatched.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (mlen >= 22 && !filter.matches(out.data() + 16))
        {
            state_->rx.dropped_filter_mismatched.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        state_->last_rssi_dbm.store(rt.dbm_antsignal, std::memory_order_relaxed);
        if (!have_peer_)
        {
            state_->rx.dropped_no_peer.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        send_queue_.push_back(std::move(out));
    }
    flush_send_queue();
}

}  // namespace winject
