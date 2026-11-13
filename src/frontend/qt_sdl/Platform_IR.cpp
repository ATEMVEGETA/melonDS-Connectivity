// SPDX-License-Identifier: GPL-3.0-or-later
#include "Platform.h"
#include "EmuInstance.h"
#include "IRTransport.h"
#include <memory>
#include <mutex>
#include <unordered_map>

namespace melonDS::Platform
{
namespace
{
std::mutex ContextMutex;
std::unordered_map<void*, std::unique_ptr<IRTransport>> Contexts;

IRTransport& GetTransport(void* userdata)
{
    auto& transport = Contexts[userdata];
    if (!transport)
    {
        auto* instance = static_cast<EmuInstance*>(userdata);
        auto& cfg = instance->getLocalConfig();
        IRSettings settings;
        settings.Mode = static_cast<IRMode>(cfg.GetInt("IR.Mode"));
        settings.LocalSlot = instance->getInstanceID();
        settings.Server = cfg.GetBool("IR.Network.IsServer");
        settings.SelfPort = cfg.GetInt("IR.Network.SelfPort");
        settings.HostPort = cfg.GetInt("IR.Network.HostPort");
        settings.HostIP = cfg.GetString("IR.Network.HostIP");
        settings.SerialPath = cfg.GetString("IR.SerialPortPath");
        transport = std::make_unique<IRTransport>(settings);
    }
    return *transport;
}
}

u8 IRSendPacket(char* data, int length, void* userdata)
{
    if (!userdata) return 0;
    u8 sent;
    {
        std::lock_guard lock(ContextMutex);
        sent = GetTransport(userdata).Send(data, length);
    }
    auto& cfg = static_cast<EmuInstance*>(userdata)->getLocalConfig();
    if (sent && cfg.GetInt("IR.Mode") == 1) Sleep(cfg.GetInt("IR.Serial.SendDelayUs"));
    return sent;
}
u8 IRReceivePacket(char* data, int length, void* userdata)
{
    if (!userdata) return 0;
    auto& cfg = static_cast<EmuInstance*>(userdata)->getLocalConfig();
    const int timeout = cfg.GetInt("IR.Mode") == 1 ? cfg.GetInt("IR.Serial.ReadTimeoutUs") : 0;
    const auto deadline = GetUSCount() + timeout;
    do
    {
        {
            std::lock_guard lock(ContextMutex);
            const u8 count = GetTransport(userdata).Receive(data, length);
            if (count || !timeout) return count;
        }
        Sleep(0);
    } while (GetUSCount() < deadline);
    return 0;
}
void IRClose(void* userdata)
{
    std::lock_guard lock(ContextMutex);
    Contexts.erase(userdata);
}
}
