#ifndef WINJECT_RADIO_RX_FILTER_BPF_H_
#define WINJECT_RADIO_RX_FILTER_BPF_H_

#include "config_types.h"

#include <linux/filter.h>
#include <vector>

namespace winject
{

std::vector<sock_filter> rx_filter_bpf_program(const mac_filter& filter);
bool bpf_interpret(const sock_filter* prog, unsigned prog_len,
                   const uint8_t* frame, size_t len);

}  // namespace winject

#endif  // WINJECT_RADIO_RX_FILTER_BPF_H_
