#include "DplaneClassify.h"

namespace winject
{

DplaneKind dplane_classify(uint32_t payload_len, bool truncated)
{
    if (truncated)
    {
        return DplaneKind::invalid;
    }
    if (payload_len >= 1 && payload_len <= 23)
    {
        return DplaneKind::registration;
    }
    if (payload_len >= 24 && payload_len <= 1472)
    {
        return DplaneKind::mpdu;
    }
    return DplaneKind::invalid;
}

}  // namespace winject
