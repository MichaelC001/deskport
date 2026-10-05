// SPDX-License-Identifier: GPL-3.0-or-later
// Serialization tests only. No platform input backend is linked or called.
#include "../input-codec.h"
#include <cassert>
#include <iostream>
using nlohmann::json;
using deskport::browser::decodeInput;
int main() {
    auto move = decodeInput({{"type","move"},{"x",1024},{"y",576},{"width",1920},{"height",1080}}).packets.at(0);
    assert(move.size() == sizeof(NV_ABS_MOUSE_MOVE_PACKET) && move[3] == 14 && move[4] == MOUSE_MOVE_ABS_MAGIC);
    assert(move[8] == 4 && move[9] == 0 && move[14] == 7 && move[15] == 128);
    auto relative = decodeInput({{"type","relative"},{"dx",-200},{"dy",300}}).packets.at(0);
    assert(relative[8] == 0xff && relative[9] == 0x38 && relative[10] == 1 && relative[11] == 44);
    auto key = decodeInput({{"type","key"},{"key",65},{"down",true},{"modifiers",3}}).packets.at(0);
    assert(key.size() == sizeof(NV_KEYBOARD_PACKET) && key[4] == KEY_DOWN_EVENT_MAGIC && key[9] == 65 && key[11] == 3);
    auto scroll = decodeInput({{"type","scroll"},{"x",120},{"y",-120}}).packets;
    assert(scroll.size() == 2 && scroll[0][8] == 255 && scroll[0][9] == 136 && scroll[1][7] == 0x55);
    const std::string text = "012345678901234567890123456789世界🌏";
    auto chunks = decodeInput({{"type","text"},{"text",text}}).packets;
    std::string rebuilt;
    for (const auto& packet : chunks) {
        assert(packet.size() <= 40 && packet[3] == packet.size() - 4);
        std::string piece(packet.begin()+8,packet.end());
        // JSON dumping validates each piece as complete UTF-8.
        (void)json(piece).dump(); rebuilt += piece;
    }
    assert(rebuilt == text && chunks.size() == 2);
    assert(decodeInput({{"type","release"}}).release);
    assert(decodeInput({{"type","heartbeat"}}).heartbeat);
    for (const auto& event : std::vector<json>{
        {{"type","move"},{"x",9},{"y",0},{"width",0},{"height",10}},
        {{"type","key"},{"key",65.5},{"down",true},{"modifiers",0}},
        {{"type","button"},{"button",0},{"down",true}},
        {{"type","relative"},{"dx",32768},{"dy",0}},
        {{"type","text"},{"text",std::string(4097,'x')}},
        {{"type","unknown"}},
        {{"type","key"},{"key",65},{"down","true"},{"modifiers",0}}
    }) {
        bool refused = false; try { (void)decodeInput(event); } catch (...) { refused = true; }
        assert(refused);
    }
    std::cout << "Browser native input serialization, bounds, UTF-8 and release fixtures passed\n";
}
