#ifndef WINJECT_RADIO_NL80211_H_
#define WINJECT_RADIO_NL80211_H_

#include <cstdint>
#include <string>
#include <vector>

namespace winject
{

enum class ChannelWidth
{
    ht20,
    ht40plus,
};

struct ChannelInfo
{
    uint8_t number = 0;
    uint32_t freq_mhz = 0;
};

struct InterfaceInfo
{
    int iftype = 0;
    uint32_t freq_mhz = 0;
    int32_t txpower_mbm = 0;
};

class INl80211
{
public:
    virtual ~INl80211() = default;
    virtual int open() = 0;
    virtual void close() = 0;
    virtual int set_monitor(int ifindex) = 0;
    virtual int set_channel(int ifindex, uint32_t freq_mhz, ChannelWidth width) = 0;
    virtual int set_tx_power_fixed_mbm(int ifindex, int32_t mbm) = 0;
    virtual int set_regdom(const char alpha2[2]) = 0;
    virtual int get_channels(uint32_t wiphy, std::vector<ChannelInfo>* out) = 0;
    virtual int get_interface(int ifindex, InterfaceInfo* out) = 0;
    virtual int get_wiphy(int ifindex, uint32_t* wiphy) = 0;
};

class Nl80211 : public INl80211
{
public:
    ~Nl80211() override;
    int open() override;
    void close() override;
    int set_monitor(int ifindex) override;
    int set_channel(int ifindex, uint32_t freq_mhz, ChannelWidth width) override;
    int set_tx_power_fixed_mbm(int ifindex, int32_t mbm) override;
    int set_regdom(const char alpha2[2]) override;
    int get_channels(uint32_t wiphy, std::vector<ChannelInfo>* out) override;
    int get_interface(int ifindex, InterfaceInfo* out) override;
    int get_wiphy(int ifindex, uint32_t* wiphy) override;

private:
    void* sock_ = nullptr;
    int family_ = 0;
};

uint32_t wifi_channel_to_freq_mhz(uint8_t channel);
uint8_t freq_mhz_to_wifi_channel(uint32_t freq_mhz);

}  // namespace winject

#endif  // WINJECT_RADIO_NL80211_H_
