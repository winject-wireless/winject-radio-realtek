#ifndef WINJECT_RADIO_MPLANE_SERVER_H_
#define WINJECT_RADIO_MPLANE_SERVER_H_

#include "Config.h"
#include "IOReactor.h"
#include "mplane_commands.h"

#include <functional>
#include <memory>
#include <vector>

namespace winject
{

class MplaneServer
{
public:
    using ResetHook = std::function<void(uint8_t id)>;

    MplaneServer(mplane_device_backend& device, mplane_radio_backend& radio,
                 const NetConfig& net);

    bool start(IOReactor& reactor, ResetHook on_reset);
    void stop(IOReactor& reactor);

private:
    void on_readable();

    NetConfig net_;
    mplane_commands commands_;
    ResetHook on_reset_;
    int fd_ = -1;
    std::vector<char> buf_;
};

}  // namespace winject

#endif  // WINJECT_RADIO_MPLANE_SERVER_H_
