#ifndef MELONDS_INSTANCE_MASK_H
#define MELONDS_INSTANCE_MASK_H

#include <array>
#include <type_traits>
#include "types.h"

namespace melonDS
{
// Local radio capacity, not the guest's 16-bit MP AID mask.
inline constexpr int kMaxLocalInstances = 100;

struct InstanceMask
{
    std::array<u64, (kMaxLocalInstances + 63) / 64> Words;

    bool Test(int slot) const noexcept
    {
        return slot >= 0 && slot < kMaxLocalInstances &&
            (Words[slot / 64] & (u64(1) << (slot % 64)));
    }
    void Set(int slot) noexcept
    {
        if (slot >= 0 && slot < kMaxLocalInstances)
            Words[slot / 64] |= u64(1) << (slot % 64);
    }
    void Reset(int slot) noexcept
    {
        if (slot >= 0 && slot < kMaxLocalInstances)
            Words[slot / 64] &= ~(u64(1) << (slot % 64));
    }
    bool Contains(const InstanceMask& other) const noexcept
    {
        for (std::size_t i = 0; i < Words.size(); ++i)
            if ((Words[i] & other.Words[i]) != other.Words[i]) return false;
        return true;
    }
    bool Intersects(const InstanceMask& other) const noexcept
    {
        for (std::size_t i = 0; i < Words.size(); ++i)
            if (Words[i] & other.Words[i]) return true;
        return false;
    }
    unsigned Count() const noexcept
    {
        unsigned count = 0;
        for (auto word : Words)
            for (; word; word &= word - 1) ++count;
        return count;
    }
    static InstanceMask All() noexcept
    {
        InstanceMask mask {};
        for (int i = 0; i < kMaxLocalInstances; ++i) mask.Set(i);
        return mask;
    }
};
static_assert(std::is_trivial_v<InstanceMask> && std::is_standard_layout_v<InstanceMask>);
}
#endif
