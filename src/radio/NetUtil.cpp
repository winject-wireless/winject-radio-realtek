#include "NetUtil.h"

#include <arpa/inet.h>

namespace winject
{

bool parse_ipv4(const std::string& text, in_addr* out)
{
    return inet_pton(AF_INET, text.c_str(), out) == 1;
}

bool sockaddr_ipv4_trusted(const sockaddr_in& peer,
                           const std::optional<std::string>& trusted)
{
    if (!trusted || trusted->empty())
    {
        return true;
    }
    in_addr want = {};
    if (!parse_ipv4(*trusted, &want))
    {
        return false;
    }
    return peer.sin_addr.s_addr == want.s_addr;
}

}  // namespace winject
