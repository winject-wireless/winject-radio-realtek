#include "RxFilterBpf.h"

#include <cstring>

namespace winject
{

std::vector<sock_filter> rx_filter_bpf_program(const mac_filter& filter)
{
    if (!filter.enabled)
    {
        return {};
    }
    const uint32_t a =
        (static_cast<uint32_t>(filter.addr[0]) << 24) |
        (static_cast<uint32_t>(filter.addr[1]) << 16) |
        (static_cast<uint32_t>(filter.addr[2]) << 8) |
        static_cast<uint32_t>(filter.addr[3]);
    const uint32_t b =
        (static_cast<uint32_t>(filter.addr[4]) << 8) |
        static_cast<uint32_t>(filter.addr[5]);
    return {
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 3),
        BPF_STMT(BPF_ALU | BPF_LSH | BPF_K, 8),
        BPF_STMT(BPF_MISC | BPF_TAX, 0),
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 2),
        BPF_STMT(BPF_ALU | BPF_OR | BPF_X, 0),
        BPF_STMT(BPF_MISC | BPF_TAX, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_IND, 16),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, a, 0, 3),
        BPF_STMT(BPF_LD | BPF_H | BPF_IND, 20),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, b, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, 65535),
        BPF_STMT(BPF_RET | BPF_K, 0),
    };
}

bool bpf_interpret(const sock_filter* prog, unsigned prog_len,
                   const uint8_t* frame, size_t len)
{
    uint32_t a = 0;
    uint32_t x = 0;
    for (unsigned pc = 0; pc < prog_len; ++pc)
    {
        const sock_filter& ins = prog[pc];
        const uint16_t code = ins.code;
        const uint32_t k = ins.k;
        if (BPF_CLASS(code) == BPF_LD)
        {
            if (BPF_MODE(code) == BPF_ABS)
            {
                const uint32_t off = ins.k;
                if (off >= len)
                {
                    return false;
                }
                if (BPF_SIZE(code) == BPF_B)
                {
                    a = frame[off];
                }
                else if (BPF_SIZE(code) == BPF_H)
                {
                    if (off + 1 >= len)
                    {
                        return false;
                    }
                    a = (static_cast<uint32_t>(frame[off]) << 8) | frame[off + 1];
                }
                else if (BPF_SIZE(code) == BPF_W)
                {
                    if (off + 3 >= len)
                    {
                        return false;
                    }
                    a = (static_cast<uint32_t>(frame[off]) << 24) |
                        (static_cast<uint32_t>(frame[off + 1]) << 16) |
                        (static_cast<uint32_t>(frame[off + 2]) << 8) |
                        frame[off + 3];
                }
            }
            else if (BPF_MODE(code) == BPF_IND)
            {
                const uint32_t off = x + ins.k;
                if (off + 3 >= len)
                {
                    return false;
                }
                if (BPF_SIZE(code) == BPF_W)
                {
                    a = (static_cast<uint32_t>(frame[off]) << 24) |
                        (static_cast<uint32_t>(frame[off + 1]) << 16) |
                        (static_cast<uint32_t>(frame[off + 2]) << 8) |
                        frame[off + 3];
                }
                else if (BPF_SIZE(code) == BPF_H)
                {
                    a = (static_cast<uint32_t>(frame[off]) << 8) | frame[off + 1];
                }
            }
        }
        else if (BPF_CLASS(code) == BPF_ALU)
        {
            if (BPF_OP(code) == BPF_LSH)
            {
                a <<= k;
            }
            else if (BPF_OP(code) == BPF_OR)
            {
                if (BPF_SRC(code) == BPF_X)
                {
                    a |= x;
                }
            }
        }
        else if (BPF_CLASS(code) == BPF_MISC && BPF_OP(code) == BPF_TAX)
        {
            x = a;
        }
        else if (BPF_CLASS(code) == BPF_JMP)
        {
            if (BPF_OP(code) == BPF_JEQ)
            {
                if (a == k)
                {
                    pc += ins.jt;
                }
                else
                {
                    pc += ins.jf;
                }
            }
        }
        else if (BPF_CLASS(code) == BPF_RET)
        {
            return k != 0;
        }
    }
    return false;
}

}  // namespace winject
