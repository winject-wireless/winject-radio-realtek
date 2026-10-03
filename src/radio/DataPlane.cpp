#include "DataPlane.h"

#include "Log.h"
#include "NetUtil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace winject
{

DataPlane::DataPlane(SharedRadioState* state, IPacketSocket* pkt,
                     const AppConfig& cfg)
    : state_(state), pkt_(pkt), cfg_(cfg)
{
    Injector::Config icfg;
    icfg.ring_size = cfg_.tune.tx_queue_sz;
    icfg.batch = cfg_.tune.rx_batch;
    icfg.tx_retry_us = cfg_.radio.tx_retry_us;
    if (!cfg_.net.trusted_ipv4.empty())
    {
        icfg.trusted_ipv4 = cfg_.net.trusted_ipv4;
    }
    injector_ = std::make_unique<Injector>(state_, pkt_, icfg);

    Forwarder::Config fcfg;
    fcfg.batch = cfg_.tune.rx_batch;
    if (!cfg_.net.trusted_ipv4.empty())
    {
        fcfg.trusted_ipv4 = cfg_.net.trusted_ipv4;
    }
    forwarder_ = std::make_unique<Forwarder>(state_, pkt_, fcfg);
}

DataPlane::~DataPlane()
{
    stop();
    join();
}

Injector* DataPlane::injector()
{
    return injector_.get();
}

bool DataPlane::start()
{
    shutdown_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    inject_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    reg_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    fwd_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (shutdown_fd_ < 0 || inject_fd_ < 0 || reg_fd_ < 0 || fwd_fd_ < 0)
    {
        return false;
    }
    in_addr bind_addr = {};
    inet_pton(AF_INET, cfg_.net.bind_addr.c_str(), &bind_addr);
    auto bind_udp = [&](int fd, uint16_t port) {
        sockaddr_in sa = {};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr = bind_addr;
        const int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        int buf = 1024 * 1024;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
        return bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
    };
    if (!bind_udp(inject_fd_, cfg_.net.inject_port) ||
        !bind_udp(reg_fd_, cfg_.net.forward_port) ||
        !bind_udp(fwd_fd_, 0))
    {
        return false;
    }
    int flags = fcntl(inject_fd_, F_GETFL, 0);
    fcntl(inject_fd_, F_SETFL, flags | O_NONBLOCK);
    fcntl(reg_fd_, F_SETFL, flags | O_NONBLOCK);
    injector_->set_udp_fd(inject_fd_);
    forwarder_->set_reg_fd(reg_fd_);
    forwarder_->set_fwd_fd(fwd_fd_);
    running_ = true;
    thread_ = std::thread([this]() { thread_main(); });
    return true;
}

void DataPlane::stop()
{
    if (shutdown_fd_ >= 0)
    {
        const uint64_t one = 1;
        write(shutdown_fd_, &one, sizeof(one));
    }
    running_ = false;
}

void DataPlane::join()
{
    if (thread_.joinable())
    {
        thread_.join();
    }
    if (inject_fd_ >= 0)
    {
        close(inject_fd_);
        inject_fd_ = -1;
    }
    if (reg_fd_ >= 0)
    {
        close(reg_fd_);
        reg_fd_ = -1;
    }
    if (fwd_fd_ >= 0)
    {
        close(fwd_fd_);
        fwd_fd_ = -1;
    }
    if (shutdown_fd_ >= 0)
    {
        close(shutdown_fd_);
        shutdown_fd_ = -1;
    }
}

void DataPlane::thread_main()
{
    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0)
    {
        LOG_ERR("data epoll_create1 failed");
        return;
    }
    auto add = [&](int fd, uint32_t events) {
        epoll_event ev = {};
        ev.events = events;
        ev.data.fd = fd;
        epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
    };
    add(shutdown_fd_, EPOLLIN);
    add(inject_fd_, EPOLLIN);
    add(reg_fd_, EPOLLIN);
    add(pkt_->fd(), EPOLLIN);
    add(injector_->retry_timer_fd(), EPOLLIN);
    epoll_event events[16];
    while (running_.load())
    {
        const int n = epoll_wait(ep, events, 16, 500);
        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        for (int i = 0; i < n; ++i)
        {
            const int fd = events[i].data.fd;
            if (fd == shutdown_fd_)
            {
                running_ = false;
                break;
            }
            if (fd == inject_fd_)
            {
                injector_->on_udp_readable();
            }
            else if (fd == reg_fd_)
            {
                forwarder_->on_reg_readable();
            }
            else if (fd == pkt_->fd())
            {
                forwarder_->on_pkt_readable();
            }
            else if (fd == injector_->retry_timer_fd())
            {
                injector_->on_retry_timer();
            }
        }
    }
    close(ep);
}

}  // namespace winject
