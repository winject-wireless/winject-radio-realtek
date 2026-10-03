#include "RealtekRadio.h"

#include "Log.h"
#include "Modulation.h"

#include <climits>
#include <cstring>

namespace winject
{

RealtekRadio::RealtekRadio(INl80211* nl, IPacketSocket* pkt,
                           SharedRadioState* state, int ifindex,
                           const RadioConfig& hw, PowerCal power_cal,
                           std::vector<uint8_t> channels)
    : nl_(nl), pkt_(pkt), state_(state), hw_(hw), power_cal_(std::move(power_cal)),
      channels_(std::move(channels)), ifindex_(ifindex)
{
    current_ = radio_config{};
}

radio_config RealtekRadio::current() const
{
    return current_;
}

mac_filter RealtekRadio::rx_filter() const
{
    return filter_;
}

void RealtekRadio::publish_modulation(const char* modulation)
{
    const ModulationEntry* entry = modulation_find(modulation);
    if (entry == nullptr)
    {
        return;
    }
    RadioOptions opt;
    opt.bandwidth = hw_.bandwidth;
    TxProfile profile = radiotap_build_tx(*entry, opt, profile_version_++);
    state_->tx_profile.publish(profile);
}

bool RealtekRadio::apply_full(const radio_config& cfg)
{
    radio_patch patch;
    patch.channel = cfg.channel;
    patch.tx_power_dbm = cfg.tx_power_dbm;
    patch.modulation = cfg.modulation;
    patch.cca_enabled = cfg.cca_enabled;
    return set_radio(patch) == mplane_status::ok;
}

mplane_status RealtekRadio::apply_channel_power_mod(const radio_config& cfg,
                                                     const radio_config& prev)
{
    if (cfg.channel != prev.channel)
    {
        const uint32_t freq = wifi_channel_to_freq_mhz(cfg.channel);
        const ChannelWidth w =
            hw_.bandwidth == 40 ? ChannelWidth::ht40plus : ChannelWidth::ht20;
        if (nl_->set_channel(ifindex_, freq, w) != 0)
        {
            return mplane_status::io_error;
        }
    }
    if (cfg.tx_power_dbm != prev.tx_power_dbm)
    {
        bool clamped = false;
        const int idx = power_cal_.dbm_to_idx(cfg.tx_power_dbm, &clamped);
        if (clamped && !power_clamp_logged_)
        {
            LOG_INF("tx power clamped for requested %d dBm", cfg.tx_power_dbm);
            power_clamp_logged_ = true;
        }
        LOG_INF("tx_power %d dBm -> idx %d (measured %.1f dBm)",
                cfg.tx_power_dbm, idx, power_cal_.dbm[idx]);
        const int32_t mbm = -idx * 100;
        if (nl_->set_tx_power_fixed_mbm(ifindex_, mbm) != 0)
        {
            return mplane_status::io_error;
        }
        InterfaceInfo info = {};
        if (nl_->get_interface(ifindex_, &info) != 0 ||
            info.txpower_mbm != mbm)
        {
            return mplane_status::io_error;
        }
    }
    if (strncmp(cfg.modulation, prev.modulation, sizeof(prev.modulation)) != 0)
    {
        publish_modulation(cfg.modulation);
    }
    return mplane_status::ok;
}

mplane_status RealtekRadio::set_radio(const radio_patch& patch)
{
    radio_config next = current_;
    if (patch.channel)
    {
        next.channel = *patch.channel;
    }
    if (patch.tx_power_dbm)
    {
        next.tx_power_dbm = *patch.tx_power_dbm;
    }
    if (patch.modulation != nullptr)
    {
        snprintf(next.modulation, sizeof(next.modulation), "%s", patch.modulation);
    }
    if (patch.cca_enabled)
    {
        if (!*patch.cca_enabled)
        {
            return mplane_status::invalid;
        }
        next.cca_enabled = true;
    }

    if (modulation_find(next.modulation) == nullptr ||
        modulation_short_preamble_rejected(next.modulation))
    {
        return mplane_status::invalid;
    }
    if (!modulation_valid_for_channel(next.modulation, next.channel, channels_,
                                      hw_.bandwidth))
    {
        return mplane_status::invalid;
    }

    radio_config prev = current_;
    if (!hw_applied_)
    {
        // The device state is unknown until the first successful apply, so
        // compare against values that differ from any valid setting.
        prev.channel = 0;
        prev.tx_power_dbm = INT8_MIN;
        prev.modulation[0] = '\0';
    }
    if (apply_channel_power_mod(next, prev) != mplane_status::ok)
    {
        if (hw_applied_)
        {
            apply_channel_power_mod(prev, next);
        }
        return mplane_status::io_error;
    }
    current_ = next;
    hw_applied_ = true;
    return mplane_status::ok;
}

mplane_status RealtekRadio::set_rx_filter(const mac_filter& filter)
{
    filter_ = filter;
    state_->rx_filter_packed.store(mac_filter_pack(filter),
                                   std::memory_order_release);
    if (hw_.rx_bpf)
    {
        const auto prog = rx_filter_bpf_program(filter);
        if (prog.empty())
        {
            pkt_->detach_bpf();
        }
        else
        {
            sock_fprog fp = {};
            fp.len = static_cast<unsigned short>(prog.size());
            fp.filter = const_cast<sock_filter*>(prog.data());
            if (pkt_->attach_bpf(&fp) != 0)
            {
                return mplane_status::io_error;
            }
        }
    }
    return mplane_status::ok;
}

}  // namespace winject
