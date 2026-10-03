#ifndef WINJECT_RADIO_POWER_CAL_H_
#define WINJECT_RADIO_POWER_CAL_H_

#include <array>
#include <cstdint>
#include <string>

namespace winject
{

// Measured TX power per rtw_tx_pwr_idx_override index (0-63), loaded from
// txpower.csv (radio.txpower). Index 0 means "no override" to the driver
// (default power), so it is never chosen.
struct PowerCal
{
    static constexpr int k_min_idx = 1;
    static constexpr int k_max_idx = 63;

    std::array<double, k_max_idx + 1> dbm = {};
    std::array<bool, k_max_idx + 1> have = {};

    // CSV rows "idx,tx_power" (dBm). A non-numeric first line is a header;
    // blank lines and lines starting with '#' are skipped. Row idx 0 is
    // accepted and ignored. Needs at least 2 rows with idx 1-63.
    bool load_csv(const std::string& path, std::string* error);

    // Index whose measured power is closest to dbm; ties go to the lower
    // index. *clamped is set when dbm lies more than 0.5 dB outside the
    // measured range.
    int dbm_to_idx(int dbm, bool* clamped) const;
};

}  // namespace winject

#endif  // WINJECT_RADIO_POWER_CAL_H_
