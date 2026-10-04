#include "DeviceWatch.h"

#include "Log.h"

#include <cerrno>
#include <cstring>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace winject
{

int64_t monotonic_ms()
{
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

int open_route_netlink()
{
    const int fd =
        socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0)
    {
        return -1;
    }
    sockaddr_nl sa = {};
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = 0;
    sa.nl_groups = RTMGRP_LINK;
    if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

void drain_route_netlink(int fd)
{
    char buf[8192];
    for (;;)
    {
        const ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
            {
                break;
            }
            break;
        }
        if (n == 0)
        {
            break;
        }
    }
}

DeviceWatchState::DeviceWatchState(std::string ifname, unsigned ifindex)
    : ifname_(std::move(ifname)), ifindex_(ifindex)
{
}

WatchAction DeviceWatchState::check(unsigned idx, const std::string& driver,
                                    const std::string& expected, int64_t now_ms)
{
    if (phase_ == Phase::attached)
    {
        if (idx == ifindex_)
        {
            return WatchAction::none;
        }
        LOG_WRN("device %s lost", ifname_.c_str());
        phase_ = Phase::lost;
        candidate_idx_ = 0;
        candidate_since_ms_ = 0;
    }

    if (phase_ == Phase::lost)
    {
        if (idx == 0)
        {
            return WatchAction::none;
        }
        if (driver.empty() || driver != expected)
        {
            if (idx != last_wrong_driver_idx_)
            {
                LOG_WRN("device %s back on driver %s, waiting", ifname_.c_str(),
                        driver.empty() ? "?" : driver.c_str());
                last_wrong_driver_idx_ = idx;
            }
            return WatchAction::none;
        }
        if (phase_ != Phase::candidate || candidate_idx_ != idx)
        {
            LOG_INF("device %s back ifindex=%u", ifname_.c_str(), idx);
            phase_ = Phase::candidate;
            candidate_idx_ = idx;
            candidate_since_ms_ = now_ms;
        }
        return WatchAction::none;
    }

    // candidate
    if (idx == candidate_idx_ && !driver.empty() && driver == expected)
    {
        if (now_ms - candidate_since_ms_ >= kDeviceStableMs)
        {
            return WatchAction::restart;
        }
        return WatchAction::none;
    }

    phase_ = Phase::lost;
    candidate_idx_ = 0;
    candidate_since_ms_ = 0;
    return check(idx, driver, expected, now_ms);
}

bool StartupWaitState::check(bool ok, const std::string& ifname, int ifindex,
                             int64_t now_ms)
{
    if (!ok)
    {
        have_anchor_ = false;
        return false;
    }
    if (!have_anchor_ || ifname != anchor_ifname_ ||
        ifindex != anchor_ifindex_)
    {
        have_anchor_ = true;
        anchor_ms_ = now_ms;
        anchor_ifname_ = ifname;
        anchor_ifindex_ = ifindex;
        return false;
    }
    return now_ms - anchor_ms_ >= kDeviceStableMs;
}

void wait_for_device(const RadioConfig& radio, DeviceSelector& sel,
                     DeviceMatch* out)
{
    StartupWaitState debounce;
    const int netlink_fd = open_route_netlink();
    const bool have_netlink = netlink_fd >= 0;
    if (!have_netlink)
    {
        LOG_WRN("rtnetlink open failed, using timed scan only");
    }

    std::string last_err;
    bool waiting_logged = false;

    for (;;)
    {
        if (have_netlink)
        {
            pollfd pfd = {netlink_fd, POLLIN, 0};
            const int pr = poll(&pfd, 1, kDeviceCheckIntervalMs);
            if (pr > 0 && (pfd.revents & POLLIN))
            {
                drain_route_netlink(netlink_fd);
            }
        }
        else
        {
            usleep(static_cast<useconds_t>(kDeviceCheckIntervalMs) * 1000);
        }

        const int64_t now_ms = monotonic_ms();
        std::string err;
        std::vector<std::string> seen;
        DeviceMatch match;
        if (sel.resolve(radio, &match, &err, &seen))
        {
            if (debounce.check(true, match.ifname, match.ifindex, now_ms))
            {
                *out = match;
                if (have_netlink)
                {
                    close(netlink_fd);
                }
                LOG_INF("device found %s ifindex=%d", match.ifname.c_str(),
                        match.ifindex);
                return;
            }
        }
        else
        {
            if (!waiting_logged)
            {
                LOG_WRN("waiting for device: %s", err.c_str());
                waiting_logged = true;
                last_err = err;
            }
            else if (err != last_err)
            {
                LOG_WRN("waiting for device: %s", err.c_str());
                last_err = err;
            }
            debounce.check(false, {}, -1, now_ms);
        }
    }
}

DeviceWatch::DeviceWatch(std::string ifname, unsigned ifindex,
                         std::string expected_driver,
                         RestartCallback on_restart)
    : ifname_(std::move(ifname)),
      ifindex_(ifindex),
      expected_driver_(std::move(expected_driver)),
      on_restart_(std::move(on_restart)),
      state_(ifname_, ifindex_)
{
}

void DeviceWatch::perform_check()
{
    const unsigned idx = if_nametoindex(ifname_.c_str());
    std::string driver;
    if (idx != 0 && idx != ifindex_)
    {
        selector_.read_driver(ifname_, &driver);
    }
    const WatchAction action =
        state_.check(idx, driver, expected_driver_, monotonic_ms());
    if (action == WatchAction::restart)
    {
        on_restart_();
    }
}

void DeviceWatch::arm_scan(IOReactor& reactor)
{
    scan_timer_ = reactor.get_timer().wait_ms(
        kDeviceCheckIntervalMs, [this, &reactor]() {
            perform_check();
            arm_scan(reactor);
        });
}

bool DeviceWatch::start(IOReactor& reactor)
{
    netlink_fd_ = open_route_netlink();
    if (netlink_fd_ < 0)
    {
        LOG_WRN("rtnetlink open failed, using timed scan only");
    }
    else if (!reactor.add_read_rdy(netlink_fd_, [this]() {
                 drain_route_netlink(netlink_fd_);
                 perform_check();
             }))
    {
        close(netlink_fd_);
        netlink_fd_ = -1;
        LOG_WRN("rtnetlink poll registration failed, using timed scan only");
    }
    perform_check();
    arm_scan(reactor);
    return true;
}

void DeviceWatch::stop(IOReactor& reactor)
{
    reactor.get_timer().cancel(scan_timer_);
    if (netlink_fd_ >= 0)
    {
        reactor.rem_read_rdy(netlink_fd_);
        close(netlink_fd_);
        netlink_fd_ = -1;
    }
}

}  // namespace winject
