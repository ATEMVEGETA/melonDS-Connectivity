/*
    Copyright 2016-2023 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "NDS.h"
#include "Wifi.h"
#include "WifiLegacy.h"
#include "WifiRanger.h"

namespace melonDS
{
bool MACEqual(const u8* a, const u8* b)
{
    return (*(u32*)&a[0] == *(u32*)&b[0]) && (*(u16*)&a[4] == *(u16*)&b[4]);
}

bool MACIsBroadcast(const u8* a)
{
    return (*(u32*)&a[0] == 0xFFFFFFFF) && (*(u16*)&a[4] == 0xFFFF);
}



Wifi::Wifi(melonDS::NDS& nds) : NDS(nds), Backend(std::make_unique<WifiLegacy>(nds)) {}
Wifi::~Wifi() = default;

void Wifi::Reset()
{
    if (RequestedProfile != ActiveProfile)
    {
        // Destroy the old radio before registering the replacement's timer.
        NDS.CancelEvent(Event_Wifi);
        Backend.reset();
        if (RequestedProfile == WifiProfile::Ranger)
            Backend = std::make_unique<WifiRanger>(NDS);
        else
            Backend = std::make_unique<WifiLegacy>(NDS);
        ActiveProfile = RequestedProfile;
    }
    Backend->Reset();
}

void Wifi::DoSavestate(Savestate* file) { Backend->DoSavestate(file); }
void Wifi::SetPowerCnt(u32 val) { Backend->SetPowerCnt(val); }
void Wifi::USTimer(u32 param) { Backend->USTimer(param); }
u16 Wifi::Read(u32 addr) { return Backend->Read(addr); }
void Wifi::Write(u32 addr, u16 val) { Backend->Write(addr, val); }
const u8* Wifi::GetMAC() const { return Backend->GetMAC(); }
const u8* Wifi::GetBSSID() const { return Backend->GetBSSID(); }
}
