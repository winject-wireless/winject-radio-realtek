#include "DriverTune.h"

#include "Log.h"

#include <fstream>

namespace winject
{

namespace
{

bool exists(const std::string& path)
{
    return std::ifstream(path).good();
}

bool write_param(const std::string& path, unsigned value)
{
    std::ofstream out(path);
    out << value << "\n";
    out.flush();
    return out.good();
}

// Returns true when the value is in effect or the parameter is absent and
// nothing was requested.
bool set_param(const std::string& dir, const char* name, unsigned value,
               bool* missing)
{
    const std::string path = dir + "/" + name;
    if (!exists(path))
    {
        if (value != 0)
        {
            LOG_WRN("driver has no %s parameter; running with automatic gain",
                    name);
            *missing = true;
        }
        return false;
    }
    if (!write_param(path, value))
    {
        LOG_WRN("cannot write %s to %s", name, path.c_str());
        return false;
    }
    if (value != 0)
    {
        LOG_INF("%s = 0x%x", name, value);
    }
    return value != 0;
}

}  // namespace

DriverTuneResult apply_rx_gain(const RadioConfig& radio,
                               const std::string& usb_drivers_root)
{
    DriverTuneResult r;
    const std::string dir =
        usb_drivers_root + "/" + radio.driver + "/module/parameters";
    r.igi_applied =
        set_param(dir, "rtw_monitor_igi", radio.rx_igi, &r.params_missing);
    r.cck_pd_applied = set_param(dir, "rtw_monitor_cck_pd", radio.rx_cck_pd,
                                 &r.params_missing);
    return r;
}

}  // namespace winject
