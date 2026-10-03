#ifndef WINJECT_RADIO_DEVICE_SELECTOR_H_
#define WINJECT_RADIO_DEVICE_SELECTOR_H_

#include "Config.h"

#include <string>
#include <vector>

namespace winject
{

struct DeviceMatch
{
    std::string ifname;
    int ifindex = -1;
    std::string driver;
    std::string mac;
};

class DeviceSelector
{
public:
    explicit DeviceSelector(std::string sysfs_root = "/sys");

    bool resolve(const RadioConfig& radio, DeviceMatch* out,
                 std::string* error, std::vector<std::string>* seen_ifaces);

private:
    std::string sysfs_root_;
    bool read_driver(const std::string& ifname, std::string* driver) const;
    bool read_mac(const std::string& ifname, std::string* mac) const;
    bool ifindex_of(const std::string& ifname, int* out) const;
};

}  // namespace winject

#endif  // WINJECT_RADIO_DEVICE_SELECTOR_H_
