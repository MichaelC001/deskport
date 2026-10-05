// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <moonlight-common-c/src/Input.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace deskport::browser {
struct InputEvent {
    bool release = false, heartbeat = false;
    std::vector<std::vector<uint8_t>> packets;
};
inline int integer(const nlohmann::json& value, const char* name, int low, int high) {
    auto it = value.find(name);
    if (it == value.end() || !it->is_number_integer()) throw std::invalid_argument("Input integer missing");
    const auto number = it->get<int64_t>();
    if (number < low || number > high) throw std::invalid_argument("Input integer out of bounds");
    return int(number);
}
inline bool boolean(const nlohmann::json& value, const char* name) {
    auto it = value.find(name);
    if (it == value.end() || !it->is_boolean()) throw std::invalid_argument("Input boolean missing");
    return it->get<bool>();
}
inline std::vector<uint8_t> inputPacket(uint32_t magic, size_t length) {
    std::vector<uint8_t> packet(length, 0);
    for (unsigned i = 0; i < 4; ++i) {
        packet[i] = uint8_t((length - 4) >> ((3 - i) * 8));
        packet[4 + i] = uint8_t(magic >> (i * 8));
    }
    return packet;
}
inline void bigWord(std::vector<uint8_t>& packet, size_t offset, int value) {
    packet[offset] = uint8_t(uint16_t(value) >> 8);
    packet[offset + 1] = uint8_t(value);
}
inline InputEvent decodeInput(const nlohmann::json& event) {
    if (!event.is_object()) throw std::invalid_argument("Input must be an object");
    InputEvent result;
    const auto type = event.at("type").get<std::string>();
    if (type == "release") result.release = true;
    else if (type == "heartbeat") result.heartbeat = true;
    else if (type == "move") {
        const auto width = integer(event, "width", 1, 32767), height = integer(event, "height", 1, 32767);
        auto packet = inputPacket(MOUSE_MOVE_ABS_MAGIC, sizeof(NV_ABS_MOUSE_MOVE_PACKET));
        bigWord(packet, 8, integer(event, "x", 0, width));
        bigWord(packet, 10, integer(event, "y", 0, height));
        bigWord(packet, 14, width); bigWord(packet, 16, height);
        result.packets.push_back(std::move(packet));
    } else if (type == "relative") {
        auto packet = inputPacket(MOUSE_MOVE_REL_MAGIC_GEN5, sizeof(NV_REL_MOUSE_MOVE_PACKET));
        bigWord(packet, 8, integer(event, "dx", -32767, 32767));
        bigWord(packet, 10, integer(event, "dy", -32767, 32767));
        result.packets.push_back(std::move(packet));
    } else if (type == "button") {
        auto packet = inputPacket(boolean(event, "down") ? MOUSE_BUTTON_DOWN_EVENT_MAGIC_GEN5 : MOUSE_BUTTON_UP_EVENT_MAGIC_GEN5, sizeof(NV_MOUSE_BUTTON_PACKET));
        packet[8] = uint8_t(integer(event, "button", 1, 5));
        result.packets.push_back(std::move(packet));
    } else if (type == "key") {
        auto packet = inputPacket(boolean(event, "down") ? KEY_DOWN_EVENT_MAGIC : KEY_UP_EVENT_MAGIC, sizeof(NV_KEYBOARD_PACKET));
        const auto key = integer(event, "key", 1, 255);
        packet[9] = uint8_t(key); packet[10] = 0; // keyCode is little endian.
        packet[11] = uint8_t(integer(event, "modifiers", 0, 15));
        result.packets.push_back(std::move(packet));
    } else if (type == "scroll") {
        const auto x = integer(event, "x", -32767, 32767), y = integer(event, "y", -32767, 32767);
        if (y) {
            auto packet = inputPacket(SCROLL_MAGIC_GEN5, sizeof(NV_SCROLL_PACKET));
            bigWord(packet, 8, y); bigWord(packet, 10, y);
            result.packets.push_back(std::move(packet));
        }
        if (x) {
            auto packet = inputPacket(SS_HSCROLL_MAGIC, sizeof(SS_HSCROLL_PACKET));
            bigWord(packet, 8, x); result.packets.push_back(std::move(packet));
        }
    } else if (type == "text") {
        const auto text = event.at("text").get<std::string>();
        if (text.size() > 4096 || text.find('\0') != std::string::npos) throw std::invalid_argument("Text exceeds input limit");
        size_t begin = 0;
        while (begin < text.size()) {
            auto end = std::min(begin + size_t(UTF8_TEXT_EVENT_MAX_COUNT), text.size());
            while (end < text.size() && end > begin && (uint8_t(text[end]) & 0xc0) == 0x80) --end;
            if (end == begin) throw std::invalid_argument("Malformed UTF-8 text");
            auto packet = inputPacket(UTF8_TEXT_EVENT_MAGIC, 8 + end - begin);
            std::copy(text.begin() + begin, text.begin() + end, packet.begin() + 8);
            result.packets.push_back(std::move(packet)); begin = end;
        }
    } else throw std::invalid_argument("Unsupported input type");
    return result;
}
}
