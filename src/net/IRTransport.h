// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELONDS_IR_TRANSPORT_H
#define MELONDS_IR_TRANSPORT_H
#include <memory>
#include <string>
#include "types.h"

namespace melonDS
{
enum class IRMode { Local, Serial, TCP, ENet };
struct IRSettings
{
    IRMode Mode = IRMode::Local;
    int LocalSlot = -1;
    bool Server = true;
    u16 SelfPort = 8081;
    u16 HostPort = 8081;
    std::string HostIP = "127.0.0.1";
    std::string SerialPath;
};
class IRTransport
{
public:
    explicit IRTransport(const IRSettings& settings);
    ~IRTransport();
    IRTransport(const IRTransport&) = delete;
    IRTransport& operator=(const IRTransport&) = delete;
    u8 Send(const char* data, int length);
    u8 Receive(char* data, int capacity);
private:
    struct State;
    std::unique_ptr<State> state;
};
}
#endif
