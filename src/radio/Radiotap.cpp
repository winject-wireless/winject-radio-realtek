#include "Radiotap.h"

extern "C"
{
#include "radiotap.h"
#include "radiotap_iter.h"
}

#include <cstring>

namespace winject
{

namespace
{

constexpr uint16_t kTxFlagsNoAck = 0x0008;

}  // namespace

TxProfile radiotap_build_tx(const ModulationEntry& entry, const RadioOptions& opt,
                            uint32_t version)
{
    TxProfile profile;
    profile.version = version;
    if (entry.legacy_rate)
    {
        profile.bytes[0] = 0x00;
        profile.bytes[1] = 0x00;
        profile.bytes[2] = 0x0c;
        profile.bytes[3] = 0x00;
        profile.bytes[4] = 0x04;
        profile.bytes[5] = 0x80;
        profile.bytes[6] = 0x00;
        profile.bytes[7] = 0x00;
        profile.bytes[8] = entry.rate;
        profile.bytes[9] = 0x00;
        profile.bytes[10] = 0x08;
        profile.bytes[11] = 0x00;
        profile.len = 12;
    }
    else if (entry.ht_mcs)
    {
        // rtw_monitor_xmit_entry ignores each flag unless its HAVE_* bit is set.
        // HAVE_FEC and HAVE_STBC stay set with the flags clear, so the driver
        // always sends BCC without STBC: the 8812AU cannot receive LDPC and
        // its STBC output is not decodable (implementation.md §6.2).
        const uint8_t known =
            IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_MCS |
            IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_FEC |
            IEEE80211_RADIOTAP_MCS_HAVE_STBC;
        uint8_t flags = 0;
        if (opt.bandwidth == 40)
        {
            flags |= IEEE80211_RADIOTAP_MCS_BW_40;
        }
        if (entry.short_gi)
        {
            flags |= IEEE80211_RADIOTAP_MCS_SGI;
        }
        profile.bytes[0] = 0x00;
        profile.bytes[1] = 0x00;
        profile.bytes[2] = 0x0d;
        profile.bytes[3] = 0x00;
        profile.bytes[4] = 0x00;
        profile.bytes[5] = 0x80;
        profile.bytes[6] = 0x08;
        profile.bytes[7] = 0x00;
        profile.bytes[8] = 0x08;
        profile.bytes[9] = 0x00;
        profile.bytes[10] = known;
        profile.bytes[11] = flags;
        profile.bytes[12] = entry.mcs_index;
        profile.len = 13;
    }
    return profile;
}

RxRadiotapInfo radiotap_parse_rx(const uint8_t* buf, size_t len)
{
    RxRadiotapInfo info;
    if (buf == nullptr || len < 8)
    {
        return info;
    }
    struct ieee80211_radiotap_header* hdr =
        reinterpret_cast<struct ieee80211_radiotap_header*>(const_cast<uint8_t*>(buf));
    if (hdr->it_version != 0)
    {
        return info;
    }
    info.rt_len = le16toh(hdr->it_len);
    if (info.rt_len > len)
    {
        return info;
    }
    struct ieee80211_radiotap_iterator iter = {};
    if (ieee80211_radiotap_iterator_init(&iter, hdr, static_cast<int>(len),
                                         nullptr) != 0)
    {
        return info;
    }
    while (ieee80211_radiotap_iterator_next(&iter) >= 0)
    {
        switch (iter.this_arg_index)
        {
        case IEEE80211_RADIOTAP_FLAGS:
            info.flags = *iter.this_arg;
            break;
        case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
            info.dbm_antsignal = static_cast<int8_t>(*iter.this_arg);
            break;
        case IEEE80211_RADIOTAP_TX_FLAGS:
            info.has_tx_flags = true;
            break;
        default:
            break;
        }
    }
    info.ok = true;
    return info;
}

}  // namespace winject
