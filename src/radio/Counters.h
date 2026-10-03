#ifndef WINJECT_RADIO_COUNTERS_H_
#define WINJECT_RADIO_COUNTERS_H_

#include "TxProfile.h"

#include <atomic>
#include <climits>
#include <cstdint>

namespace winject
{

struct TxCounters
{
    std::atomic<uint32_t> ether_pkt{0};
    std::atomic<uint32_t> air_pkt{0};
    std::atomic<uint32_t> dropped_invalid_frame{0};
    std::atomic<uint32_t> dropped_tx_queue{0};
    std::atomic<uint32_t> dropped_wifi{0};
};

struct RxCounters
{
    std::atomic<uint32_t> air_pkt{0};
    std::atomic<uint32_t> ether_pkt{0};
    std::atomic<uint32_t> dropped_filter_mismatched{0};
    std::atomic<uint32_t> dropped_rx_queue{0};
    std::atomic<uint32_t> dropped_no_peer{0};
    std::atomic<uint32_t> dropped_send_failed{0};
};

struct SharedRadioState
{
    TxProfileStore tx_profile;
    std::atomic<uint64_t> rx_filter_packed{0};
    std::atomic<int16_t> last_rssi_dbm{INT16_MIN};
    TxCounters tx;
    RxCounters rx;
};

}  // namespace winject

#endif  // WINJECT_RADIO_COUNTERS_H_
