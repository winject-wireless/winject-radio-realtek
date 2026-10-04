#ifndef WINJECT_RADIO_APP_H_
#define WINJECT_RADIO_APP_H_

#include "Config.h"
#include "Counters.h"
#include "DataPlane.h"
#include "DeviceSelector.h"
#include "DeviceWatch.h"
#include "IOReactor.h"
#include "MplaneServer.h"
#include "NetLink.h"
#include "Nl80211.h"
#include "PacketSocket.h"
#include "PowerCal.h"
#include "RealtekBackends.h"
#include "RealtekRadio.h"
#include "Settings.h"

#include <memory>
#include <vector>

namespace winject
{

class App
{
public:
    int run(int argc, char** argv);

private:
    bool bring_up();
    void shutdown();
    void restart();
    bool fail_bring_up(const char* what);

    AppConfig cfg_;
    IOReactor reactor_;
    SharedRadioState state_;
    std::unique_ptr<INl80211> nl_;
    std::unique_ptr<PacketSocket> pkt_;
    std::unique_ptr<Settings> settings_;
    std::unique_ptr<RealtekRadio> radio_;
    std::unique_ptr<DataPlane> data_;
    std::unique_ptr<RealtekDeviceBackend> device_backend_;
    std::unique_ptr<RealtekRadioBackend> radio_backend_;
    std::unique_ptr<MplaneServer> mplane_;
    std::unique_ptr<DeviceWatch> device_watch_;
    DeviceMatch dev_;
    PowerCal power_cal_;
    std::vector<uint8_t> channels_;
    std::vector<char*> argv_copy_;
    int64_t start_us_ = 0;
    int shutdown_fd_ = -1;
};

}  // namespace winject

#endif  // WINJECT_RADIO_APP_H_
