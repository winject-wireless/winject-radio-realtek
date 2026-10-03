#ifndef WINJECT_RADIO_IO_REACTOR_H_
#define WINJECT_RADIO_IO_REACTOR_H_

#include <bfcext/epoll_reactor.hpp>
#include <functional>

namespace winject
{

using IOReactor = bfcext::epoll_reactor<std::function<void()>>;

}  // namespace winject

#endif  // WINJECT_RADIO_IO_REACTOR_H_
