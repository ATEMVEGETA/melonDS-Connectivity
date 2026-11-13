// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELONDS_WIFI_BACKEND_H
#define MELONDS_WIFI_BACKEND_H

#include "Savestate.h"

namespace melonDS
{
enum class WifiProfile : u32 { Legacy = 0, Ranger = 1 };

class WifiBackend
{
public:
    virtual ~WifiBackend() = default;
    virtual void Reset() = 0;
    virtual void DoSavestate(Savestate* file) = 0;
    virtual void SetPowerCnt(u32 val) = 0;
    virtual void USTimer(u32 param) = 0;
    virtual u16 Read(u32 addr) = 0;
    virtual void Write(u32 addr, u16 val) = 0;
    virtual const u8* GetMAC() const = 0;
    virtual const u8* GetBSSID() const = 0;
};
}
#endif
