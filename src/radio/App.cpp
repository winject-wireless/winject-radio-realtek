#include "App.h"

#include "Log.h"
#include "Modulation.h"

#include <bfc/timer.hpp>
#include <csignal>
#include <linux/nl80211.h>
#include <cstring>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

namespace winject
{

namespace
{
App* g_app = nullptr;
volatile sig_atomic_t g_shutdown_fd = -1;

void on_signal(int)
{
    if (g_shutdown_fd >= 0)
    {
        const uint64_t one = 1;
        const ssize_t n = write(g_shutdown_fd, &one, sizeof(one));
        (void)n;
    }
    else
    {
        _exit(0);  // still in bring-up: nothing to clean up
    }
}

int64_t monotonic_us()
{
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

}  // namespace

bool App::bring_up()
{
    std::string err;
    const char* config_path = nullptr;
    for (size_t i = 0; i + 1 < argv_copy_.size(); ++i)
    {
        if (argv_copy_[i] != nullptr &&
            strcmp(argv_copy_[i], "--config") == 0)
        {
            config_path = argv_copy_[i + 1];
            break;
        }
    }
    if (config_path == nullptr)
    {
        LOG_ERR("missing --config <path>");
        return false;
    }
    if (!cfg_.load(config_path, &err))
    {
        LOG_ERR("%s", err.c_str());
        return false;
    }
    log_set_level(cfg_.log_level);
    if (!power_cal_.load_csv(cfg_.radio.txpower, &err))
    {
        LOG_ERR("%s", err.c_str());
        return false;
    }

    DeviceSelector selector;
    std::vector<std::string> seen;
    if (!selector.resolve(cfg_.radio, &dev_, &err, &seen))
    {
        LOG_ERR("%s", err.c_str());
        return false;
    }
    netlink_release_nm(dev_.ifname);

    nl_ = std::make_unique<Nl80211>();
    if (nl_->open() != 0)
    {
        LOG_ERR("nl80211 open failed");
        return false;
    }
    char alpha2[2] = {cfg_.radio.regdom[0], cfg_.radio.regdom[1]};
    if (nl_->set_regdom(alpha2) != 0)
    {
        LOG_WRN("set regdom failed");
    }

    // Releasing the interface from NetworkManager makes wpa_supplicant detach
    // asynchronously, and it can switch the interface back to managed after
    // our set_monitor succeeded. Confirm the type after a settle delay.
    bool monitor_ok = false;
    for (int attempt = 1; attempt <= 5 && !monitor_ok; ++attempt)
    {
        if (!netlink_set_up(dev_.ifname, false))
        {
            LOG_ERR("link down failed");
            return false;
        }
        const int rc = nl_->set_monitor(dev_.ifindex);
        netlink_set_up(dev_.ifname, true);
        usleep(300000);
        InterfaceInfo info;
        monitor_ok = rc == 0 && nl_->get_interface(dev_.ifindex, &info) == 0 &&
                     info.iftype == NL80211_IFTYPE_MONITOR;
        if (!monitor_ok)
        {
            LOG_WRN("monitor mode attempt %d failed (set rc=%d)", attempt, rc);
        }
    }
    if (!monitor_ok)
    {
        LOG_ERR("monitor mode failed");
        return false;
    }

    uint32_t wiphy = 0;
    if (nl_->get_wiphy(dev_.ifindex, &wiphy) != 0)
    {
        LOG_ERR("get wiphy failed");
        return false;
    }
    std::vector<ChannelInfo> chs;
    if (nl_->get_channels(wiphy, &chs) != 0 || chs.empty())
    {
        LOG_ERR("channel list empty");
        return false;
    }
    channels_.clear();
    for (const ChannelInfo& c : chs)
    {
        if (c.number != 0)
        {
            channels_.push_back(c.number);
        }
    }

    settings_ = std::make_unique<Settings>(cfg_.state_dir);

    pkt_ = std::make_unique<PacketSocket>();
    if (!pkt_->open(dev_.ifindex, cfg_.tune.sock_rcvbuf))
    {
        LOG_ERR("packet socket open failed");
        return false;
    }

    radio_ = std::make_unique<RealtekRadio>(nl_.get(), pkt_.get(), &state_,
                                            dev_.ifindex, cfg_.radio, power_cal_,
                                            channels_);
    SlotData slot;
    bool applied = false;
    if (settings_->load_current(&slot, &err))
    {
        applied = radio_->apply_full(slot.radio) &&
                  radio_->set_rx_filter(slot.rx_filter) == mplane_status::ok;
        if (!applied)
        {
            LOG_WRN("current slot did not apply, using defaults");
        }
    }
    if (!applied && !radio_->apply_full(radio_config{}))
    {
        LOG_ERR("cannot apply radio settings");
        return false;
    }
    const radio_config cur = radio_->current();
    LOG_INF("%s: channel=%u tx_power=%d modulation=%s", dev_.ifname.c_str(),
            cur.channel, cur.tx_power_dbm, cur.modulation);

    data_ = std::make_unique<DataPlane>(&state_, pkt_.get(), cfg_);
    if (!data_->start())
    {
        LOG_ERR("data plane start failed");
        return false;
    }

    start_us_ = monotonic_us();
    device_backend_ = std::make_unique<RealtekDeviceBackend>(
        settings_.get(), radio_.get(), data_.get(), argv_copy_, start_us_);
    radio_backend_ = std::make_unique<RealtekRadioBackend>(
        &state_, radio_.get(), data_->injector());
    mplane_ = std::make_unique<MplaneServer>(*device_backend_, *radio_backend_,
                                             cfg_.net);
    if (!mplane_->start(reactor_, {}))
    {
        LOG_ERR("mplane start failed");
        return false;
    }

    shutdown_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    reactor_.add_read_rdy(shutdown_fd_, [this]() { reactor_.stop(); });
    g_shutdown_fd = shutdown_fd_;
    return true;
}

void App::shutdown()
{
    if (mplane_)
    {
        mplane_->stop(reactor_);
    }
    reactor_.stop();
    if (data_)
    {
        data_->stop();
        data_->join();
    }
    g_shutdown_fd = -1;
    if (shutdown_fd_ >= 0)
    {
        close(shutdown_fd_);
        shutdown_fd_ = -1;
    }
}

int App::run(int argc, char** argv)
{
    if (argc < 2)
    {
        LOG_ERR("usage: winject-radio-realtek --config <path>");
        return 2;
    }
    argv_copy_.assign(argv, argv + argc);
    argv_copy_.push_back(nullptr);  // execv on reset
    g_app = this;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (!bring_up())
    {
        return 1;
    }

    std::function<void()> poll_reset;
    poll_reset = [this, &poll_reset]() {
        if (device_backend_->consume_reset_pending())
        {
            usleep(200000);
            execv("/proc/self/exe", argv_copy_.data());
            _exit(1);
        }
        reactor_.get_timer().wait_ms(50, poll_reset);
    };
    poll_reset();

    reactor_.run();
    shutdown();
    g_app = nullptr;
    return 0;
}

}  // namespace winject
