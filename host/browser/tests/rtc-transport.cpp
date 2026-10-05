// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated ICE/DTLS/SRTP/DataChannel integration test. Never captures a display
// or instantiates a native input backend. Optional file signaling tests browsers.
#include "../rtc-session.h"
#include "../video-profile.h"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>

using namespace std::chrono_literals;
using deskport::browser::RtcSession;
using Json = nlohmann::json;
template<class Predicate> void await(Predicate done, const char* what) {
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (!done() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(10ms);
    if (!done()) throw std::runtime_error(what);
}
int main(int argc, char** argv) try {
    const auto capabilities = [](const std::string& profile, int packetization = 1) {
        return Json::array({{{"sdpFmtpLine", "profile-level-id=" + profile + ";packetization-mode=" + std::to_string(packetization)}}});
    };
    assert(deskport::browser::supportsBrowserVideo(capabilities("42e01f")));
    assert(deskport::browser::supportsBrowserVideo(capabilities("42002a")));
    assert(!deskport::browser::supportsBrowserVideo(capabilities("42e01e")));
    assert(!deskport::browser::supportsBrowserVideo(capabilities("64002a")));
    assert(!deskport::browser::supportsBrowserVideo(capabilities("42e01f", 0)));
    assert(!deskport::browser::supportsBrowserVideo(capabilities("42zzzz")));
    std::atomic<unsigned> inputs{0}, keyframes{0}, videoPackets{0}, audioPackets{0};
    std::atomic<bool> disconnected{false}, gathered{false};
    auto source = RtcSession::create(true, {
        [&](std::string message) { if (Json::parse(message).value("type", "") == "heartbeat") ++inputs; },
        [&] { ++keyframes; }, [&] { disconnected = true; }
    }, 48200, 48215);
    const auto offer = source->offer();
    if (offer.find("a=candidate:") == std::string::npos || offer.find("H264/90000") == std::string::npos ||
        offer.find("profile-level-id=42e01f") == std::string::npos || offer.find("opus/48000/2") == std::string::npos)
        throw std::runtime_error("Offer missing ICE/H264/Opus");
    if (argc == 4 && std::string(argv[1]) == "--browser") {
        const std::filesystem::path directory(argv[2]);
        std::filesystem::create_directories(directory);
        { std::ofstream file(directory / "offer.json"); file << Json{{"sdp", offer}, {"type", "offer"}}; }
        const auto deadline = std::chrono::steady_clock::now() + 45s;
        while (!std::filesystem::exists(directory / "answer.json") && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(100ms);
        Json answer; { std::ifstream file(directory / "answer.json"); file >> answer; }
        source->answer(answer.at("sdp")); await([&] { return source->connected(); }, "Browser ICE connection failed");
        std::ifstream file(argv[3], std::ios::binary);
        const std::vector<uint8_t> stream((std::istreambuf_iterator<char>(file)), {});
        std::vector<size_t> offsets;
        for (size_t i = 0; i + 4 < stream.size(); ++i)
            if (stream[i] == 0 && stream[i+1] == 0 && stream[i+2] == 0 && stream[i+3] == 1 && (stream[i+4] & 31) == 9) offsets.push_back(i);
        offsets.push_back(stream.size());
        if (offsets.size() < 2) throw std::runtime_error("Test H264 requires AUD-delimited frames");
        for (unsigned i = 0; i < 600 && source->connected(); ++i) {
            const auto frame = i % (offsets.size() - 1);
            source->video(stream.data() + offsets[frame], offsets[frame + 1] - offsets[frame], i / 30.0);
            const uint8_t silence[] = {0xfc, 0xff, 0xfe};
            source->audio(silence, sizeof(silence), i / 30.0);
            std::this_thread::sleep_for(33ms);
        }
        { std::ofstream output(directory / "server-result.json"); output << Json{{"connected", source->connected()}, {"inputMessages", inputs.load()}, {"keyframeRequests", keyframes.load()}}; }
        source->close(); return 0;
    }
    rtc::Configuration config; config.disableAutoNegotiation = true;
    config.portRangeBegin = 48216; config.portRangeEnd = 48231;
    auto receiver = std::make_shared<rtc::PeerConnection>(config);
    std::vector<std::shared_ptr<rtc::Track>> tracks;
    std::shared_ptr<rtc::DataChannel> input;
    receiver->onTrack([&](std::shared_ptr<rtc::Track> track) {
        const bool video = track->description().type() == "video";
        track->onMessage([&, video](rtc::binary packet) { if (!packet.empty()) { if (video) ++videoPackets; else ++audioPackets; } }, nullptr);
        tracks.push_back(std::move(track));
    });
    receiver->onDataChannel([&](std::shared_ptr<rtc::DataChannel> channel) {
        input = channel; channel->onOpen([channel] { channel->send(std::string("{\"type\":\"heartbeat\"}")); });
    });
    receiver->onGatheringStateChange([&](rtc::PeerConnection::GatheringState state) { if (state == rtc::PeerConnection::GatheringState::Complete) gathered = true; });
    receiver->setRemoteDescription(rtc::Description(offer, rtc::Description::Type::Offer));
    receiver->setLocalDescription(rtc::Description::Type::Answer);
    await([&] { return gathered.load(); }, "Receiver ICE gathering failed");
    source->answer(std::string(*receiver->localDescription()));
    await([&] { return source->connected() && inputs.load() > 0; }, "DataChannel did not connect");
    const uint8_t frame[] = {0,0,0,1,0x65,0x88,0x84,0x21,0xa0};
    const uint8_t opus[] = {0xfc,0xff,0xfe};
    for (int i = 0; i < 20 && (!videoPackets || !audioPackets); ++i) {
        source->video(frame, sizeof(frame), i / 30.0); source->audio(opus, sizeof(opus), i * 0.02);
        std::this_thread::sleep_for(25ms);
    }
    await([&] { return videoPackets.load() && audioPackets.load(); }, "SRTP media not received");
    source->close(); source->close(); receiver->close();
    if (source->connected()) throw std::runtime_error("Closed transport remained connected");
    std::cout << "Real loopback ICE/DTLS/SRTP H264+Opus and DataChannel passed; native capture/input were not used\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
