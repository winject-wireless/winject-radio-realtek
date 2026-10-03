#include "DeviceSelector.h"

#include <cstring>
#include <dirent.h>
#include <fstream>
#include <strings.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

namespace winject
{

DeviceSelector::DeviceSelector(std::string sysfs_root)
    : sysfs_root_(std::move(sysfs_root))
{
}

bool DeviceSelector::read_driver(const std::string& ifname,
                                 std::string* driver) const
{
    const std::string path =
        sysfs_root_ + "/class/net/" + ifname + "/device/driver";
    char link[512];
    const ssize_t n = readlink(path.c_str(), link, sizeof(link) - 1);
    if (n <= 0)
    {
        return false;
    }
    link[n] = '\0';
    const char* base = strrchr(link, '/');
    *driver = base != nullptr ? base + 1 : link;
    return true;
}

bool DeviceSelector::read_mac(const std::string& ifname, std::string* mac) const
{
    std::ifstream in(sysfs_root_ + "/class/net/" + ifname + "/address");
    if (!in)
    {
        return false;
    }
    std::getline(in, *mac);
    return !mac->empty();
}

bool DeviceSelector::ifindex_of(const std::string& ifname, int* out) const
{
    unsigned idx = if_nametoindex(ifname.c_str());
    if (idx == 0)
    {
        return false;
    }
    *out = static_cast<int>(idx);
    return true;
}

bool DeviceSelector::resolve(const RadioConfig& radio, DeviceMatch* out,
                             std::string* error,
                             std::vector<std::string>* seen_ifaces)
{
    std::vector<std::string> candidates;
    if (!radio.device.empty())
    {
        candidates.push_back(radio.device);
    }
    else if (!radio.usb_port.empty())
    {
        const std::string net_glob =
            sysfs_root_ + "/bus/usb/devices/" + radio.usb_port + ":1.0/net";
        DIR* dir = opendir(net_glob.c_str());
        if (dir == nullptr)
        {
            *error = "usb port " + radio.usb_port + " has no net device";
            return false;
        }
        dirent* ent;
        while ((ent = readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.')
            {
                continue;
            }
            candidates.emplace_back(ent->d_name);
        }
        closedir(dir);
    }
    else if (!radio.mac.empty())
    {
        DIR* dir = opendir((sysfs_root_ + "/class/net").c_str());
        if (dir == nullptr)
        {
            *error = "cannot list net devices";
            return false;
        }
        dirent* ent;
        while ((ent = readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.')
            {
                continue;
            }
            std::string mac;
            if (read_mac(ent->d_name, &mac) &&
                strcasecmp(mac.c_str(), radio.mac.c_str()) == 0)
            {
                candidates.emplace_back(ent->d_name);
            }
        }
        closedir(dir);
    }

    if (seen_ifaces != nullptr)
    {
        *seen_ifaces = candidates;
    }

    for (const std::string& ifname : candidates)
    {
        std::string driver;
        if (!read_driver(ifname, &driver))
        {
            continue;
        }
        if (driver != radio.driver)
        {
            continue;
        }
        int ifindex = -1;
        if (!ifindex_of(ifname, &ifindex))
        {
            continue;
        }
        out->ifname = ifname;
        out->ifindex = ifindex;
        out->driver = driver;
        read_mac(ifname, &out->mac);
        return true;
    }

    std::ostringstream os;
    os << "no interface with driver " << radio.driver;
    if (!candidates.empty())
    {
        os << " (saw";
        for (const auto& n : candidates)
        {
            std::string drv;
            if (read_driver(n, &drv))
            {
                os << ' ' << n << '=' << drv;
            }
            else
            {
                os << ' ' << n;
            }
        }
        os << ')';
    }
    *error = os.str();
    return false;
}

}  // namespace winject
