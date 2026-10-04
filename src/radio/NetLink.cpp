#include "NetLink.h"

#include "Log.h"

#include <cstdlib>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace winject
{

bool netlink_set_up(const std::string& ifname, bool up)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        return false;
    }
    struct ifreq ifr = {};
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname.c_str());
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
    {
        close(fd);
        return false;
    }
    if (up)
    {
        ifr.ifr_flags |= IFF_UP;
    }
    else
    {
        ifr.ifr_flags &= ~IFF_UP;
    }
    const bool ok = ioctl(fd, SIOCSIFFLAGS, &ifr) == 0;
    close(fd);
    return ok;
}

bool netlink_release_nm(const std::string& ifname)
{
    const std::string cmd =
        "nmcli device set " + ifname + " managed no 2>/dev/null";
    const int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        LOG_WRN("nmcli managed no failed for %s", ifname.c_str());
    }
    return true;
}

}  // namespace winject
