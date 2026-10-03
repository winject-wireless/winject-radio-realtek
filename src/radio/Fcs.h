#ifndef WINJECT_RADIO_FCS_H_
#define WINJECT_RADIO_FCS_H_

#include <cstddef>
#include <cstdint>

namespace winject
{

uint32_t wifi_fcs_compute(const uint8_t* mpdu, size_t len);
bool wifi_fcs_matches(const uint8_t* frame, size_t len);
void wifi_fcs_store(const uint8_t* mpdu, size_t len, uint8_t out[4]);

}  // namespace winject

#endif  // WINJECT_RADIO_FCS_H_
