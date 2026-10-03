#ifndef WINJECT_RADIO_MODULATION_H_
#define WINJECT_RADIO_MODULATION_H_

#include <cstdint>
#include <string>
#include <vector>

namespace winject
{

struct RadioOptions
{
    unsigned bandwidth = 20;
};

struct ModulationEntry
{
    const char* name = nullptr;
    bool legacy_rate = false;
    uint8_t rate = 0;
    bool ht_mcs = false;
    uint8_t mcs_index = 0;
    bool short_gi = false;
    bool reject = false;
};

const ModulationEntry* modulation_find(const char* name);
bool modulation_short_preamble_rejected(const char* name);
std::string modulation_list_string();
bool modulation_valid_for_channel(const char* name, uint8_t channel,
                                   const std::vector<uint8_t>& channels,
                                   unsigned bandwidth);

}  // namespace winject

#endif  // WINJECT_RADIO_MODULATION_H_
