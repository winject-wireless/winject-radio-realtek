#include "RealtekBackends.h"

#include "Modulation.h"
#include "Version.h"

#include <climits>
#include <time.h>
#include <unistd.h>

namespace winject
{

std::string RealtekRadioBackend::mod_list_ = modulation_list_string();

RealtekDeviceBackend::RealtekDeviceBackend(Settings* settings,
                                           RealtekRadio* radio,
                                           DataPlane* data,
                                           std::vector<char*> argv,
                                           int64_t start_us)
    : settings_(settings), radio_(radio), data_(data), argv_(std::move(argv)),
      start_us_(start_us)
{
}

mplane_status RealtekDeviceBackend::restart(std::optional<WinjectMode> mode)
{
    if (mode.has_value() && *mode == WINJECT_MODE_OTA)
    {
        return mplane_status::invalid;
    }
    reset_pending_ = true;
    return mplane_status::ok;
}

bool RealtekDeviceBackend::consume_reset_pending()
{
    const bool v = reset_pending_;
    reset_pending_ = false;
    return v;
}

network_config RealtekDeviceBackend::network() const
{
    return network_config{};
}

mplane_status RealtekDeviceBackend::set_network(const network_config&)
{
    return mplane_status::unsupported;
}

tune_config RealtekDeviceBackend::tune() const
{
    return tune_config{};
}

mplane_status RealtekDeviceBackend::set_tune(const tune_config&)
{
    return mplane_status::unsupported;
}

mplane_status RealtekDeviceBackend::save(uint8_t slot)
{
    SlotData data;
    data.radio = radio_->current();
    data.rx_filter = radio_->rx_filter();
    return settings_->save_slot(slot, data);
}

mplane_status RealtekDeviceBackend::load(uint8_t slot)
{
    SlotData data;
    const mplane_status st = settings_->load_slot(slot, &data);
    if (st != mplane_status::ok)
    {
        return st;
    }
    if (!radio_->apply_full(data.radio))
    {
        return mplane_status::io_error;
    }
    return radio_->set_rx_filter(data.rx_filter);
}

int64_t RealtekDeviceBackend::uptime_us() const
{
    timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t now =
        ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
    return now - start_us_;
}

const char* RealtekDeviceBackend::version() const
{
    return WINJECT_VERSION_STRING;
}

RealtekRadioBackend::RealtekRadioBackend(SharedRadioState* state,
                                         RealtekRadio* radio, Injector* injector)
    : state_(state), radio_(radio), injector_(injector)
{
}

uint8_t RealtekRadioBackend::tx_queue_size() const
{
    return injector_ != nullptr
               ? static_cast<uint8_t>(injector_->ring_occupancy())
               : 0;
}

uint8_t RealtekRadioBackend::tx_in_flight() const
{
    return 0;
}

uint8_t RealtekRadioBackend::rx_queue_size() const
{
    return 0;
}

uint32_t RealtekRadioBackend::tx_dropped_invalid_frame() const
{
    return state_->tx.dropped_invalid_frame.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::tx_dropped_tx_queue() const
{
    return state_->tx.dropped_tx_queue.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::tx_dropped_wifi() const
{
    return state_->tx.dropped_wifi.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::rx_dropped_filter_mismatched() const
{
    return state_->rx.dropped_filter_mismatched.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::rx_dropped_rx_queue() const
{
    return state_->rx.dropped_rx_queue.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::rx_dropped_no_peer() const
{
    return state_->rx.dropped_no_peer.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::rx_dropped_send_failed() const
{
    return state_->rx.dropped_send_failed.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::tx_ether_pkt() const
{
    return state_->tx.ether_pkt.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::rx_ether_pkt() const
{
    return state_->rx.ether_pkt.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::tx_air_pkt() const
{
    return state_->tx.air_pkt.load(std::memory_order_relaxed);
}

uint32_t RealtekRadioBackend::rx_air_pkt() const
{
    return state_->rx.air_pkt.load(std::memory_order_relaxed);
}

radio_config RealtekRadioBackend::radio() const
{
    return radio_->current();
}

radio_caps RealtekRadioBackend::caps() const
{
    radio_caps c;
    c.fcs = fcs_mode::actual;
    return c;
}

bool RealtekRadioBackend::rx_rssi(int8_t* dbm) const
{
    const int16_t v = state_->last_rssi_dbm.load(std::memory_order_relaxed);
    if (v == INT16_MIN)
    {
        return false;
    }
    *dbm = static_cast<int8_t>(v);
    return true;
}

mplane_status RealtekRadioBackend::set_radio(const radio_patch& patch)
{
    return radio_->set_radio(patch);
}

mac_filter RealtekRadioBackend::rx_filter_addr3() const
{
    return mac_filter_unpack(
        state_->rx_filter_packed.load(std::memory_order_relaxed));
}

mplane_status RealtekRadioBackend::set_rx_filter_addr3(const mac_filter& filter)
{
    return radio_->set_rx_filter(filter);
}

const char* RealtekRadioBackend::modulation_list() const
{
    return mod_list_.c_str();
}

}  // namespace winject
