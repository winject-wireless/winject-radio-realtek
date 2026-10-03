#ifndef WINJECT_RADIO_TX_PROFILE_H_
#define WINJECT_RADIO_TX_PROFILE_H_

#include <atomic>
#include <cstdint>
#include <cstring>

namespace winject
{

struct TxProfile
{
    uint8_t bytes[16] = {};
    uint8_t len = 0;
    uint32_t version = 0;
};

class TxProfileStore
{
public:
    void publish(const TxProfile& profile)
    {
        const uint8_t inactive = 1 - active_.load(std::memory_order_relaxed);
        slots_[inactive] = profile;
        active_.store(inactive, std::memory_order_release);
    }

    TxProfile load() const
    {
        const uint8_t idx = active_.load(std::memory_order_acquire);
        return slots_[idx];
    }

private:
    TxProfile slots_[2] = {};
    std::atomic<uint8_t> active_{0};
};

}  // namespace winject

#endif  // WINJECT_RADIO_TX_PROFILE_H_
