#ifndef WINJECT_RADIO_NET_LINK_H_
#define WINJECT_RADIO_NET_LINK_H_

#include <string>

namespace winject
{

bool netlink_set_up(const std::string& ifname, bool up);
bool netlink_release_nm(const std::string& ifname);

}  // namespace winject

#endif  // WINJECT_RADIO_NET_LINK_H_
