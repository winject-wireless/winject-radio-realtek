#ifndef WINJECT_RADIO_REALTEK_RADIO_H_
#define WINJECT_RADIO_REALTEK_RADIO_H_

#include "Config.h"
#include "Counters.h"
#include "Nl80211.h"
#include "PacketSocket.h"
#include "PowerCal.h"
#include "Radiotap.h"
#include "RxFilterBpf.h"
#include "config_types.h"
#include "mplane_backend.h"

#include <vector>

namespace winject
{

class RealtekRadio
{
public:
    RealtekRadio(INl80211* nl, IPacketSocket* pkt, SharedRadioState* state,
                 int ifindex, const RadioConfig& hw, PowerCal power_cal,
                 std::vector<uint8_t> channels);

    mplane_status set_radio(const radio_patch& patch);
    mplane_status set_rx_filter(const mac_filter& filter);
    radio_config current() const;
    mac_filter rx_filter() const;

    bool apply_full(const radio_config& cfg);

private:
    mplane_status apply_channel_power_mod(const radio_config& cfg,
                                           const radio_config& prev);
    void publish_modulation(const char* modulation);

    INl80211* nl_;
    IPacketSocket* pkt_;
    SharedRadioState* state_;
    RadioConfig hw_;
    PowerCal power_cal_;
    std::vector<uint8_t> channels_;
    int ifindex_;
    radio_config current_;
    mac_filter filter_;
    uint32_t profile_version_ = 1;
    bool power_clamp_logged_ = false;
    bool hw_applied_ = false;  // channel/power not yet written to the device
};

}  // namespace winject

#endif  // WINJECT_RADIO_REALTEK_RADIO_H_
