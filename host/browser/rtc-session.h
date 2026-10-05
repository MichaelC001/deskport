// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <rtc/rtc.hpp>

namespace deskport::browser {
// Transport only: capture, authorization and native input belong to the host adapter.
class RtcSession final : public std::enable_shared_from_this<RtcSession> {
public:
    struct Callbacks {
        std::function<void(std::string)> input;
        std::function<void()> keyframe;
        std::function<void()> disconnected;
    };
    static std::shared_ptr<RtcSession> create(bool audio, Callbacks callbacks,
                                             uint16_t firstPort = 48100, uint16_t lastPort = 48115);
    ~RtcSession();
    std::string offer(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    void answer(const std::string& sdp);
    bool video(const uint8_t* data, size_t size, double seconds);
    bool audio(const uint8_t* data, size_t size, double seconds);
    void close();
    bool connected() const;
    std::string state() const;
private:
    explicit RtcSession(Callbacks callbacks);
    void initialize(bool audio, uint16_t firstPort, uint16_t lastPort);
    Callbacks callbacks_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> video_, audio_;
    std::shared_ptr<rtc::DataChannel> input_;
    std::atomic<bool> closed_{false};
    std::mutex gatherMutex_;
    std::condition_variable gatherCondition_;
    bool gathered_ = false;
};
}
