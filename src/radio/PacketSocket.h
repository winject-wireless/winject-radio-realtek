#ifndef WINJECT_RADIO_PACKET_SOCKET_H_
#define WINJECT_RADIO_PACKET_SOCKET_H_

#include <cstddef>
#include <cstdint>
#include <linux/filter.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <vector>

namespace winject
{

class IPacketSocket
{
public:
    virtual ~IPacketSocket() = default;
    virtual int fd() const = 0;
    virtual int send_mpdu(const struct iovec* iov, int iovcnt) = 0;
    virtual int recv_batch(::mmsghdr* msgs, unsigned count) = 0;
    virtual bool read_statistics(uint32_t* tp_drops) = 0;
    virtual int attach_bpf(const ::sock_fprog* prog) = 0;
    virtual int detach_bpf() = 0;
};

class PacketSocket : public IPacketSocket
{
public:
    ~PacketSocket() override;
    bool open(int ifindex, unsigned rcvbuf);
    void close();
    int fd() const override;
    int send_mpdu(const struct iovec* iov, int iovcnt) override;
    int recv_batch(::mmsghdr* msgs, unsigned count) override;
    bool read_statistics(uint32_t* tp_drops) override;
    int attach_bpf(const ::sock_fprog* prog) override;
    int detach_bpf() override;

private:
    int fd_ = -1;
    uint32_t last_tp_drops_ = 0;
};

}  // namespace winject

#endif  // WINJECT_RADIO_PACKET_SOCKET_H_
