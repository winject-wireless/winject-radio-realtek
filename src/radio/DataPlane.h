#ifndef WINJECT_RADIO_DATA_PLANE_H_
#define WINJECT_RADIO_DATA_PLANE_H_

#include "Config.h"
#include "Counters.h"
#include "Forwarder.h"
#include "Injector.h"
#include "PacketSocket.h"

#include <atomic>
#include <memory>
#include <thread>

namespace winject
{

class DataPlane
{
public:
    DataPlane(SharedRadioState* state, IPacketSocket* pkt, const AppConfig& cfg);
    ~DataPlane();

    bool start();
    void stop();
    void join();

    Injector* injector();

private:
    void thread_main();

    SharedRadioState* state_;
    IPacketSocket* pkt_;
    AppConfig cfg_;
    std::unique_ptr<Injector> injector_;
    std::unique_ptr<Forwarder> forwarder_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int shutdown_fd_ = -1;
    int inject_fd_ = -1;
    int reg_fd_ = -1;
    int fwd_fd_ = -1;
};

}  // namespace winject

#endif  // WINJECT_RADIO_DATA_PLANE_H_
