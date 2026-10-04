#ifndef WINJECT_RADIO_REALTEK_BACKENDS_H_
#define WINJECT_RADIO_REALTEK_BACKENDS_H_

#include "DataPlane.h"
#include "RealtekRadio.h"
#include "Settings.h"
#include "mplane_backend.h"

#include <vector>

namespace winject
{

class RealtekDeviceBackend : public mplane_device_backend
{
public:
    RealtekDeviceBackend(Settings* settings, RealtekRadio* radio,
                         DataPlane* data, std::vector<char*> argv,
                         int64_t start_us);

    mplane_status restart(std::optional<WinjectMode> mode) override;
    network_config network() const override;
    mplane_status set_network(const network_config& cfg) override;
    tune_config tune() const override;
    mplane_status set_tune(const tune_config& cfg) override;
    mplane_status save(uint8_t slot) override;
    mplane_status load(uint8_t slot) override;
    int64_t uptime_us() const override;

    bool consume_reset_pending();

private:
    Settings* settings_;
    RealtekRadio* radio_;
    DataPlane* data_;
    std::vector<char*> argv_;
    int64_t start_us_;
    bool reset_pending_ = false;
};

class RealtekRadioBackend : public mplane_radio_backend
{
public:
    RealtekRadioBackend(SharedRadioState* state, RealtekRadio* radio,
                        Injector* injector);

    uint8_t tx_queue_size() const override;
    uint8_t tx_in_flight() const override;
    uint8_t rx_queue_size() const override;
    uint32_t tx_dropped_invalid_frame() const override;
    uint32_t tx_dropped_tx_queue() const override;
    uint32_t tx_dropped_wifi() const override;
    uint32_t rx_dropped_filter_mismatched() const override;
    uint32_t rx_dropped_rx_queue() const override;
    uint32_t rx_dropped_no_peer() const override;
    uint32_t rx_dropped_send_failed() const override;
    uint32_t tx_ether_pkt() const override;
    uint32_t rx_ether_pkt() const override;
    uint32_t tx_air_pkt() const override;
    uint32_t rx_air_pkt() const override;
    radio_config radio() const override;
    radio_caps caps() const override;
    bool rx_rssi(int8_t* dbm) const override;
    mplane_status set_radio(const radio_patch& patch) override;
    mac_filter rx_filter_addr3() const override;
    mplane_status set_rx_filter_addr3(const mac_filter& filter) override;
    const char* modulation_list() const override;

private:
    SharedRadioState* state_;
    RealtekRadio* radio_;
    Injector* injector_;
    static std::string mod_list_;
};

}  // namespace winject

#endif  // WINJECT_RADIO_REALTEK_BACKENDS_H_
