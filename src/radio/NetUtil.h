#ifndef WINJECT_RADIO_NET_UTIL_H_
#define WINJECT_RADIO_NET_UTIL_H_

#include <netinet/in.h>
#include <optional>
#include <string>

namespace winject
{

bool parse_ipv4(const std::string& text, in_addr* out);
bool sockaddr_ipv4_trusted(const sockaddr_in& peer,
                           const std::optional<std::string>& trusted);

}  // namespace winject

#endif  // WINJECT_RADIO_NET_UTIL_H_
