#ifndef WINJECT_RADIO_DEVICE_WATCH_H_
#define WINJECT_RADIO_DEVICE_WATCH_H_

#include "Config.h"
#include "DeviceSelector.h"
#include "IOReactor.h"

#include <bfc/timer.hpp>
#include <cstdint>
#include <functional>
#include <string>

namespace winject
{

constexpr int kDeviceCheckIntervalMs = 1000;
constexpr int kDeviceStableMs = 1000;

enum class WatchAction
{
    none,
    restart,
};

class DeviceWatchState
{
public:
    DeviceWatchState(std::string ifname, unsigned ifindex);

    WatchAction check(unsigned idx, const std::string& driver,
                      const std::string& expected, int64_t now_ms);

private:
    enum class Phase
    {
        attached,
        lost,
        candidate,
    };

    std::string ifname_;
    unsigned ifindex_;
    Phase phase_ = Phase::attached;
    unsigned candidate_idx_ = 0;
    int64_t candidate_since_ms_ = 0;
    unsigned last_wrong_driver_idx_ = 0;
};

class StartupWaitState
{
public:
    bool check(bool ok, const std::string& ifname, int ifindex, int64_t now_ms);

private:
    bool have_anchor_ = false;
    int64_t anchor_ms_ = 0;
    std::string anchor_ifname_;
    int anchor_ifindex_ = -1;
};

int open_route_netlink();
void drain_route_netlink(int fd);

void wait_for_device(const RadioConfig& radio, DeviceSelector& sel,
                     DeviceMatch* out);

class DeviceWatch
{
public:
    using RestartCallback = std::function<void()>;

    DeviceWatch(std::string ifname, unsigned ifindex, std::string expected_driver,
                RestartCallback on_restart);

    bool start(IOReactor& reactor);
    void stop(IOReactor& reactor);

private:
    void perform_check();
    void arm_scan(IOReactor& reactor);

    std::string ifname_;
    unsigned ifindex_;
    std::string expected_driver_;
    RestartCallback on_restart_;
    DeviceSelector selector_;
    DeviceWatchState state_;
    int netlink_fd_ = -1;
    bfc::timer<std::function<void()>>::timer_id_t scan_timer_{};
};

int64_t monotonic_ms();

}  // namespace winject

#endif  // WINJECT_RADIO_DEVICE_WATCH_H_
