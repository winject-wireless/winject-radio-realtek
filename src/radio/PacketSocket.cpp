#include "PacketSocket.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace winject
{

PacketSocket::~PacketSocket()
{
    close();
}

bool PacketSocket::open(int ifindex, unsigned rcvbuf)
{
    close();
    fd_ = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (fd_ < 0)
    {
        return false;
    }
    int one = 1;
    setsockopt(fd_, SOL_PACKET, PACKET_QDISC_BYPASS, &one, sizeof(one));
    if (rcvbuf > 0)
    {
        int buf = static_cast<int>(rcvbuf);
        setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
    }
    struct sockaddr_ll addr = {};
    addr.sll_family = AF_PACKET;
    addr.sll_ifindex = ifindex;
    addr.sll_protocol = htons(ETH_P_ALL);
    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        close();
        return false;
    }
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    return true;
}

void PacketSocket::close()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

int PacketSocket::fd() const
{
    return fd_;
}

int PacketSocket::send_mpdu(const struct iovec* iov, int iovcnt)
{
    struct msghdr msg = {};
    msg.msg_iov = const_cast<iovec*>(iov);
    msg.msg_iovlen = static_cast<size_t>(iovcnt);
    return static_cast<int>(sendmsg(fd_, &msg, 0));
}

int PacketSocket::recv_batch(::mmsghdr* msgs, unsigned count)
{
    return recvmmsg(fd_, msgs, count, MSG_DONTWAIT, nullptr);
}

bool PacketSocket::read_statistics(uint32_t* tp_drops)
{
    struct tpacket_stats stats = {};
    socklen_t len = sizeof(stats);
    if (getsockopt(fd_, SOL_PACKET, PACKET_STATISTICS, &stats, &len) != 0)
    {
        return false;
    }
    const uint32_t total = stats.tp_drops;
    const uint32_t delta =
        total >= last_tp_drops_ ? total - last_tp_drops_ : total;
    last_tp_drops_ = total;
    if (tp_drops != nullptr)
    {
        *tp_drops = delta;
    }
    return true;
}

int PacketSocket::attach_bpf(const ::sock_fprog* prog)
{
    return setsockopt(fd_, SOL_SOCKET, SO_ATTACH_FILTER, prog, sizeof(*prog));
}

int PacketSocket::detach_bpf()
{
    struct sock_fprog empty = {};
    return setsockopt(fd_, SOL_SOCKET, SO_DETACH_FILTER, &empty, sizeof(empty));
}

}  // namespace winject
