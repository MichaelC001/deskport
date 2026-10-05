// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <string>

namespace deskport::browser {
inline constexpr const char* browserProfileLevelId = "42e01f";
inline bool supportsBrowserVideo(const nlohmann::json& codecs) {
    if (!codecs.is_array() || codecs.size() > 64) return false;
    for (const auto& codec : codecs) {
        if (!codec.is_object() || !codec.contains("sdpFmtpLine") || !codec["sdpFmtpLine"].is_string()) continue;
        auto format = codec["sdpFmtpLine"].get<std::string>();
        if (format.size() > 1024) continue;
        std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        constexpr auto prefix = "profile-level-id=";
        const auto offset = format.find(prefix);
        if (offset == std::string::npos || format.find("packetization-mode=1") == std::string::npos) continue;
        const auto profile = format.substr(offset + std::char_traits<char>::length(prefix), 6);
        if (profile.size() != 6 || profile.substr(0, 2) != "42" ||
            !std::all_of(profile.begin(), profile.end(), [](unsigned char c) { return std::isxdigit(c); })) continue;
        // No level-asymmetry shortcut: the receive capability must cover our 3.1 stream.
        if (std::stoul(profile.substr(4), nullptr, 16) >= 31) return true;
    }
    return false;
}
}
