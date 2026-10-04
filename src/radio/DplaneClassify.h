#ifndef WINJECT_RADIO_DPLANE_CLASSIFY_H_
#define WINJECT_RADIO_DPLANE_CLASSIFY_H_

#include <cstdint>

namespace winject
{

enum class DplaneKind
{
    registration,
    mpdu,
    invalid,
};

// Payload length on the d-plane UDP port (decision 8).
DplaneKind dplane_classify(uint32_t payload_len, bool truncated);

}  // namespace winject

#endif  // WINJECT_RADIO_DPLANE_CLASSIFY_H_
