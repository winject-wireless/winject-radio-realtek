#ifndef WINJECT_RADIO_RADIOTAP_H_
#define WINJECT_RADIO_RADIOTAP_H_

#include "Modulation.h"
#include "TxProfile.h"

#include <cstddef>
#include <cstdint>

namespace winject
{

struct RxRadiotapInfo
{
    bool ok = false;
    uint16_t rt_len = 0;
    uint8_t flags = 0;
    bool has_tx_flags = false;
    int8_t dbm_antsignal = 0;
};

TxProfile radiotap_build_tx(const ModulationEntry& entry, const RadioOptions& opt,
                            uint32_t version);
RxRadiotapInfo radiotap_parse_rx(const uint8_t* buf, size_t len);

constexpr uint8_t kRadiotapFcs = 0x10;
constexpr uint8_t kRadiotapBadFcs = 0x40;

}  // namespace winject

#endif  // WINJECT_RADIO_RADIOTAP_H_
