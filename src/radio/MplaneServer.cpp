#include "MplaneServer.h"

#include "Log.h"
#include "NetUtil.h"
#include "mplane_reply.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace winject
{

MplaneServer::MplaneServer(mplane_device_backend& device,
                           mplane_radio_backend& radio, const NetConfig& net)
    : net_(net), commands_(device, &radio, nullptr)
{
    buf_.resize(16384);
}

bool MplaneServer::start(IOReactor& reactor, ResetHook on_reset)
{
    on_reset_ = std::move(on_reset);
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0)
    {
        return false;
    }
    in_addr bind_addr = {};
    inet_pton(AF_INET, net_.bind_addr.c_str(), &bind_addr);
    sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(net_.console_port);
    sa.sin_addr = bind_addr;
    const int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0)
    {
        close(fd_);
        fd_ = -1;
        return false;
    }
    return reactor.add_read_rdy(fd_, [this]() { on_readable(); });
}

void MplaneServer::stop(IOReactor& reactor)
{
    if (fd_ >= 0)
    {
        reactor.rem_read_rdy(fd_);
        close(fd_);
        fd_ = -1;
    }
}

void MplaneServer::on_readable()
{
    char line[1500];
    sockaddr_in peer = {};
    socklen_t len = sizeof(peer);
    const ssize_t n =
        recvfrom(fd_, line, sizeof(line) - 1, MSG_DONTWAIT,
                 reinterpret_cast<sockaddr*>(&peer), &len);
    if (n <= 0)
    {
        return;
    }
    if (!net_.trusted_ipv4.empty())
    {
        in_addr want = {};
        if (!parse_ipv4(net_.trusted_ipv4, &want) ||
            peer.sin_addr.s_addr != want.s_addr)
        {
            return;
        }
    }
    line[n] = '\0';
    class vec_reply : public mplane_reply
    {
    public:
        explicit vec_reply(std::vector<char>* out) : out_(out) {}
        void write(const char* data, size_t sz) override
        {
            out_->insert(out_->end(), data, data + sz);
        }

    private:
        std::vector<char>* out_;
    };
    std::vector<char> reply;
    vec_reply r(&reply);
    commands_.handle_text(line, r);
    if (reply.empty())
    {
        return;
    }
    size_t off = 0;
    while (off < reply.size())
    {
        const size_t chunk = std::min(reply.size() - off, size_t(16384));
        sendto(fd_, reply.data() + off, chunk, 0,
               reinterpret_cast<sockaddr*>(&peer), len);
        off += chunk;
    }
}

}  // namespace winject
