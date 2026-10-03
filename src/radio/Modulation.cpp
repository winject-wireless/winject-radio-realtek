#include "Modulation.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace winject
{

namespace
{

struct Row
{
    const char* name;
    bool legacy;
    uint8_t rate;
    bool ht;
    uint8_t mcs;
    bool sgi;
    bool reject;
    bool dsss_only;
};

static const Row k_table[] = {
    {"DSS_1M_L", true, 2, false, 0, false, false, true},
    {"DSS_2M_L", true, 4, false, 0, false, false, true},
    {"CCK_5M_L", true, 11, false, 0, false, false, true},
    {"CCK_11M_L", true, 22, false, 0, false, false, true},
    {"DSS_2M_S", false, 0, false, 0, false, true, true},
    {"CCK_5M_S", false, 0, false, 0, false, true, true},
    {"CCK_11M_S", false, 0, false, 0, false, true, true},
    {"OFDM_6M", true, 12, false, 0, false, false, false},
    {"OFDM_9M", true, 18, false, 0, false, false, false},
    {"OFDM_12M", true, 24, false, 0, false, false, false},
    {"OFDM_18M", true, 36, false, 0, false, false, false},
    {"OFDM_24M", true, 48, false, 0, false, false, false},
    {"OFDM_36M", true, 72, false, 0, false, false, false},
    {"OFDM_48M", true, 96, false, 0, false, false, false},
    {"OFDM_54M", true, 108, false, 0, false, false, false},
    {"OFDM_MCS0_LGI", false, 0, true, 0, false, false, false},
    {"OFDM_MCS1_LGI", false, 0, true, 1, false, false, false},
    {"OFDM_MCS2_LGI", false, 0, true, 2, false, false, false},
    {"OFDM_MCS3_LGI", false, 0, true, 3, false, false, false},
    {"OFDM_MCS4_LGI", false, 0, true, 4, false, false, false},
    {"OFDM_MCS5_LGI", false, 0, true, 5, false, false, false},
    {"OFDM_MCS6_LGI", false, 0, true, 6, false, false, false},
    {"OFDM_MCS7_LGI", false, 0, true, 7, false, false, false},
    {"OFDM_MCS0_SGI", false, 0, true, 0, true, false, false},
    {"OFDM_MCS1_SGI", false, 0, true, 1, true, false, false},
    {"OFDM_MCS2_SGI", false, 0, true, 2, true, false, false},
    {"OFDM_MCS3_SGI", false, 0, true, 3, true, false, false},
    {"OFDM_MCS4_SGI", false, 0, true, 4, true, false, false},
    {"OFDM_MCS5_SGI", false, 0, true, 5, true, false, false},
    {"OFDM_MCS6_SGI", false, 0, true, 6, true, false, false},
    {"OFDM_MCS7_SGI", false, 0, true, 7, true, false, false},
};

const Row* find_row(const char* name)
{
    if (name == nullptr)
    {
        return nullptr;
    }
    for (const Row& r : k_table)
    {
        if (strcasecmp(r.name, name) == 0)
        {
            return &r;
        }
    }
    return nullptr;
}

bool channel_in_list(uint8_t ch, const std::vector<uint8_t>& channels)
{
    return std::find(channels.begin(), channels.end(), ch) != channels.end();
}

}  // namespace

const ModulationEntry* modulation_find(const char* name)
{
    static ModulationEntry out;
    const Row* r = find_row(name);
    if (r == nullptr)
    {
        return nullptr;
    }
    out.name = r->name;
    out.legacy_rate = r->legacy;
    out.rate = r->rate;
    out.ht_mcs = r->ht;
    out.mcs_index = r->mcs;
    out.short_gi = r->sgi;
    out.reject = r->reject;
    return &out;
}

bool modulation_short_preamble_rejected(const char* name)
{
    const Row* r = find_row(name);
    return r != nullptr && r->reject;
}

std::string modulation_list_string()
{
    std::ostringstream os;
    bool first = true;
    for (const Row& r : k_table)
    {
        if (r.reject)
        {
            continue;
        }
        if (!first)
        {
            os << ' ';
        }
        first = false;
        os << r.name;
    }
    return os.str();
}

bool modulation_valid_for_channel(const char* name, uint8_t channel,
                                   const std::vector<uint8_t>& channels,
                                   unsigned bandwidth)
{
    const Row* r = find_row(name);
    if (r == nullptr || r->reject)
    {
        return false;
    }
    if (!channel_in_list(channel, channels))
    {
        return false;
    }
    if (channel == 14)
    {
        return r->dsss_only;
    }
    if (r->dsss_only && channel > 14)
    {
        return false;
    }
    if (channel <= 14 && !r->dsss_only && channel == 14)
    {
        return false;
    }
    if (bandwidth == 40)
    {
        if (!channel_in_list(static_cast<uint8_t>(channel + 4), channels))
        {
            return false;
        }
    }
    return true;
}

}  // namespace winject
