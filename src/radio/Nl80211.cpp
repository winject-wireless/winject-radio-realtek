#include "Nl80211.h"

#include <errno.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/netlink.h>
#include <string.h>

namespace winject
{

namespace
{

struct HandlerCtx
{
    int err = 0;
    std::vector<ChannelInfo>* channels = nullptr;
    InterfaceInfo* iface = nullptr;
    uint32_t* wiphy = nullptr;
};

int ack_handler(struct nl_msg*, void* arg)
{
    auto* ctx = static_cast<HandlerCtx*>(arg);
    ctx->err = 0;
    return NL_STOP;
}

int finish_handler(struct nl_msg*, void* arg)
{
    auto* ctx = static_cast<HandlerCtx*>(arg);
    ctx->err = 0;
    return NL_STOP;
}

int error_handler(struct sockaddr_nl*, struct nlmsgerr* err, void* arg)
{
    auto* ctx = static_cast<HandlerCtx*>(arg);
    ctx->err = err->error;
    return NL_STOP;
}

int valid_handler(struct nl_msg* msg, void* arg)
{
    auto* ctx = static_cast<HandlerCtx*>(arg);
    struct genlmsghdr* gnl = static_cast<genlmsghdr*>(nlmsg_data(nlmsg_hdr(msg)));
    struct nlattr* tb[NL80211_ATTR_MAX + 1];
    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnl, 0),
              genlmsg_attrlen(gnl, 0), nullptr);
    if (ctx->channels != nullptr && tb[NL80211_ATTR_WIPHY_BANDS])
    {
        struct nlattr* band;
        int rem;
        nla_for_each_nested(band, tb[NL80211_ATTR_WIPHY_BANDS], rem)
        {
            struct nlattr* freqs[NL80211_BAND_ATTR_MAX + 1];
            nla_parse(freqs, NL80211_BAND_ATTR_MAX, static_cast<nlattr*>(nla_data(band)),
                      nla_len(band), nullptr);
            if (!freqs[NL80211_BAND_ATTR_FREQS])
            {
                continue;
            }
            struct nlattr* freq;
            int rem2;
            nla_for_each_nested(freq, freqs[NL80211_BAND_ATTR_FREQS], rem2)
            {
                struct nlattr* ftb[NL80211_FREQUENCY_ATTR_MAX + 1];
                nla_parse(ftb, NL80211_FREQUENCY_ATTR_MAX,
                          static_cast<nlattr*>(nla_data(freq)), nla_len(freq),
                          nullptr);
                if (!ftb[NL80211_FREQUENCY_ATTR_FREQ])
                {
                    continue;
                }
                const uint32_t f = nla_get_u32(ftb[NL80211_FREQUENCY_ATTR_FREQ]);
                if (ftb[NL80211_FREQUENCY_ATTR_DISABLED] ||
                    ftb[NL80211_FREQUENCY_ATTR_NO_IR])
                {
                    continue;
                }
                ChannelInfo ch;
                ch.freq_mhz = f;
                ch.number = freq_mhz_to_wifi_channel(f);
                if (ch.number != 0)
                {
                    ctx->channels->push_back(ch);
                }
            }
        }
    }
    if (ctx->iface != nullptr)
    {
        if (tb[NL80211_ATTR_IFTYPE])
        {
            ctx->iface->iftype = nla_get_u32(tb[NL80211_ATTR_IFTYPE]);
        }
        if (tb[NL80211_ATTR_WIPHY_FREQ])
        {
            ctx->iface->freq_mhz = nla_get_u32(tb[NL80211_ATTR_WIPHY_FREQ]);
        }
        if (tb[NL80211_ATTR_WIPHY_TX_POWER_LEVEL])
        {
            ctx->iface->txpower_mbm =
                static_cast<int32_t>(nla_get_u32(tb[NL80211_ATTR_WIPHY_TX_POWER_LEVEL]));
        }
    }
    if (ctx->wiphy != nullptr && tb[NL80211_ATTR_WIPHY])
    {
        *ctx->wiphy = nla_get_u32(tb[NL80211_ATTR_WIPHY]);
    }
    return NL_OK;
}

int send_msg(struct nl_sock* sock, struct nl_msg* msg, HandlerCtx* ctx)
{
    struct nl_cb* cb = nl_cb_alloc(NL_CB_DEFAULT);
    if (cb == nullptr)
    {
        nlmsg_free(msg);
        return -ENOMEM;
    }
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, valid_handler, ctx);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, ctx);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, ctx);
    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, ctx);
    ctx->err = 1;
    const int sent = nl_send_auto(sock, msg);
    if (sent < 0)
    {
        nl_cb_put(cb);
        nlmsg_free(msg);
        return sent;
    }
    while (ctx->err > 0)
    {
        const int rc = nl_recvmsgs(sock, cb);
        if (rc != 0)
        {
            break;
        }
    }
    nl_cb_put(cb);
    nlmsg_free(msg);
    return ctx->err == 0 ? 0 : -ctx->err;
}

}  // namespace

uint32_t wifi_channel_to_freq_mhz(uint8_t channel)
{
    if (channel >= 1 && channel <= 13)
    {
        return 2407 + channel * 5;
    }
    if (channel == 14)
    {
        return 2484;
    }
    if (channel >= 36 && channel <= 177)
    {
        return 5000 + channel * 5;
    }
    return 0;
}

uint8_t freq_mhz_to_wifi_channel(uint32_t freq_mhz)
{
    if (freq_mhz >= 2412 && freq_mhz <= 2472)
    {
        return static_cast<uint8_t>((freq_mhz - 2407) / 5);
    }
    if (freq_mhz == 2484)
    {
        return 14;
    }
    if (freq_mhz >= 5180 && freq_mhz <= 5885)
    {
        return static_cast<uint8_t>((freq_mhz - 5000) / 5);
    }
    return 0;
}

Nl80211::~Nl80211()
{
    close();
}

int Nl80211::open()
{
    if (sock_ != nullptr)
    {
        return 0;
    }
    auto* sock = nl_socket_alloc();
    if (sock == nullptr)
    {
        return -ENOMEM;
    }
    if (genl_connect(sock) != 0)
    {
        nl_socket_free(sock);
        return -EIO;
    }
    nl_socket_set_msg_buf_size(sock, 8192);
    family_ = genl_ctrl_resolve(sock, "nl80211");
    if (family_ < 0)
    {
        nl_socket_free(sock);
        return family_;
    }
    sock_ = sock;
    return 0;
}

void Nl80211::close()
{
    if (sock_ != nullptr)
    {
        nl_socket_free(static_cast<nl_sock*>(sock_));
        sock_ = nullptr;
    }
}

int Nl80211::set_monitor(int ifindex)
{
    auto* sock = static_cast<nl_sock*>(sock_);
    struct nl_msg* msg = nlmsg_alloc();
    if (msg == nullptr)
    {
        return -ENOMEM;
    }
    genlmsg_put(msg, 0, 0, family_, 0, 0, NL80211_CMD_SET_INTERFACE, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, static_cast<uint32_t>(ifindex));
    nla_put_u32(msg, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_MONITOR);
    HandlerCtx ctx;
    return send_msg(sock, msg, &ctx);
}

int Nl80211::set_channel(int ifindex, uint32_t freq_mhz, ChannelWidth width)
{
    auto* sock = static_cast<nl_sock*>(sock_);
    struct nl_msg* msg = nlmsg_alloc();
    if (msg == nullptr)
    {
        return -ENOMEM;
    }
    genlmsg_put(msg, 0, 0, family_, 0, 0, NL80211_CMD_SET_CHANNEL, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, static_cast<uint32_t>(ifindex));
    nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq_mhz);
    const uint32_t ch_type =
        width == ChannelWidth::ht40plus ? NL80211_CHAN_HT40PLUS : NL80211_CHAN_HT20;
    nla_put_u32(msg, NL80211_ATTR_WIPHY_CHANNEL_TYPE, ch_type);
    HandlerCtx ctx;
    return send_msg(sock, msg, &ctx);
}

int Nl80211::set_tx_power_fixed_mbm(int ifindex, int32_t mbm)
{
    auto* sock = static_cast<nl_sock*>(sock_);
    struct nl_msg* msg = nlmsg_alloc();
    if (msg == nullptr)
    {
        return -ENOMEM;
    }
    genlmsg_put(msg, 0, 0, family_, 0, 0, NL80211_CMD_SET_WIPHY, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, static_cast<uint32_t>(ifindex));
    nla_put_u32(msg, NL80211_ATTR_WIPHY_TX_POWER_SETTING, NL80211_TX_POWER_FIXED);
    nla_put_u32(msg, NL80211_ATTR_WIPHY_TX_POWER_LEVEL, static_cast<uint32_t>(mbm));
    HandlerCtx ctx;
    return send_msg(sock, msg, &ctx);
}

int Nl80211::set_regdom(const char alpha2[2])
{
    auto* sock = static_cast<nl_sock*>(sock_);
    struct nl_msg* msg = nlmsg_alloc();
    if (msg == nullptr)
    {
        return -ENOMEM;
    }
    genlmsg_put(msg, 0, 0, family_, 0, 0, NL80211_CMD_REQ_SET_REG, 0);
    nla_put(msg, NL80211_ATTR_REG_ALPHA2, 2, alpha2);
    HandlerCtx ctx;
    return send_msg(sock, msg, &ctx);
}

int Nl80211::get_channels(uint32_t wiphy, std::vector<ChannelInfo>* out)
{
    auto* sock = static_cast<nl_sock*>(sock_);
    struct nl_msg* msg = nlmsg_alloc();
    if (msg == nullptr)
    {
        return -ENOMEM;
    }
    // genlmsg_put(msg, port, seq, family, hdrlen, flags, cmd, version)
    genlmsg_put(msg, 0, 0, family_, 0, NLM_F_DUMP, NL80211_CMD_GET_WIPHY, 0);
    nla_put_u32(msg, NL80211_ATTR_WIPHY, wiphy);
    nla_put_flag(msg, NL80211_ATTR_SPLIT_WIPHY_DUMP);
    HandlerCtx ctx;
    ctx.channels = out;
    ctx.err = 1;
    struct nl_cb* cb = nl_cb_alloc(NL_CB_DEFAULT);
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, valid_handler, &ctx);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &ctx);
    const int sent = nl_send_auto(sock, msg);
    nlmsg_free(msg);
    if (sent < 0)
    {
        nl_cb_put(cb);
        return sent;
    }
    while (ctx.err > 0)
    {
        if (nl_recvmsgs(sock, cb) != 0)
        {
            break;
        }
    }
    nl_cb_put(cb);
    return ctx.err == 0 ? 0 : -ctx.err;
}

int Nl80211::get_interface(int ifindex, InterfaceInfo* out)
{
    auto* sock = static_cast<nl_sock*>(sock_);
    struct nl_msg* msg = nlmsg_alloc();
    if (msg == nullptr)
    {
        return -ENOMEM;
    }
    genlmsg_put(msg, 0, 0, family_, 0, 0, NL80211_CMD_GET_INTERFACE, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, static_cast<uint32_t>(ifindex));
    HandlerCtx ctx;
    ctx.iface = out;
    return send_msg(sock, msg, &ctx);
}

int Nl80211::get_wiphy(int ifindex, uint32_t* wiphy)
{
    auto* sock = static_cast<nl_sock*>(sock_);
    struct nl_msg* msg = nlmsg_alloc();
    if (msg == nullptr)
    {
        return -ENOMEM;
    }
    genlmsg_put(msg, 0, 0, family_, 0, 0, NL80211_CMD_GET_INTERFACE, 0);
    nla_put_u32(msg, NL80211_ATTR_IFINDEX, static_cast<uint32_t>(ifindex));
    HandlerCtx ctx;
    ctx.wiphy = wiphy;
    return send_msg(sock, msg, &ctx);
}

}  // namespace winject
