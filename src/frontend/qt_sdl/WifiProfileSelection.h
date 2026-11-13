// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MELONDS_WIFI_PROFILE_SELECTION_H
#define MELONDS_WIFI_PROFILE_SELECTION_H

#include <string_view>
#include "WifiBackend.h"

namespace melonDS
{
constexpr WifiProfile WifiProfileForGameCode(std::string_view code)
{
    const auto game = code.substr(0, 3);
    return game == "ARG" || game == "YP2" || game == "B3R"
        ? WifiProfile::Ranger : WifiProfile::Legacy;
}
}
#endif
