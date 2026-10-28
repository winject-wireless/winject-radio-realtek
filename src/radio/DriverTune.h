#ifndef WINJECT_RADIO_DRIVER_TUNE_H_
#define WINJECT_RADIO_DRIVER_TUNE_H_

#include "Config.h"

#include <string>

namespace winject
{

struct DriverTuneResult
{
    bool igi_applied = false;
    bool cck_pd_applied = false;
    bool params_missing = false;  // a requested parameter does not exist in the loaded driver
};

// Writes radio.rx_igi / radio.rx_cck_pd to the driver's module parameters
// (<usb_drivers_root>/<radio.driver>/module/parameters/rtw_monitor_*). The
// config is the source of truth: a value of 0 is written back so the driver
// returns to its automatic gain. A driver without the parameters (stock build)
// is not an error; it only matters when a non-zero value was requested.
DriverTuneResult apply_rx_gain(const RadioConfig& radio,
                               const std::string& usb_drivers_root =
                                   "/sys/bus/usb/drivers");

}  // namespace winject

#endif  // WINJECT_RADIO_DRIVER_TUNE_H_
