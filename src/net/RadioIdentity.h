#ifndef MELONDS_RADIO_IDENTITY_H
#define MELONDS_RADIO_IDENTITY_H

#include <array>
#include <mutex>
#include <optional>
#include "InstanceMask.h"

namespace melonDS
{
class RadioIdentityRegistry
{
public:
    using MAC = std::array<u8, 6>;

    bool Reserve(int instance, MAC& address)
    {
        if (instance < 0 || instance >= kMaxLocalInstances) return false;
        std::lock_guard<std::mutex> lock(Mutex);
        auto& owned = Addresses[instance];
        if (!owned)
        {
            MAC candidate = address;
            auto used = [&] {
                for (const auto& other : Addresses)
                    if (other && *other == candidate) return true;
                return false;
            };
            // A per-instance firmware copy can already contain a MAC offset.
            while (used())
                for (int byte = 5; byte >= 3; --byte)
                    if (++candidate[byte] != 0) break;
            owned = candidate;
        }
        address = *owned;
        return true;
    }

    void Release(int instance)
    {
        if (instance < 0 || instance >= kMaxLocalInstances) return;
        std::lock_guard<std::mutex> lock(Mutex);
        Addresses[instance].reset();
    }

private:
    std::mutex Mutex;
    std::array<std::optional<MAC>, kMaxLocalInstances> Addresses {};
};
}
#endif
