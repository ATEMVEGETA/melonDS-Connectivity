// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELONDS_IR_SERIAL_H
#define MELONDS_IR_SERIAL_H
#include <memory>
#include <string>
#include "types.h"

namespace melonDS
{
class IRSerial
{
public:
    explicit IRSerial(const std::string& path);
    ~IRSerial();
    u8 Send(const char* data, int length);
    u8 Receive(char* data, int capacity);
private:
    struct State;
    std::unique_ptr<State> state;
};
}
#endif
