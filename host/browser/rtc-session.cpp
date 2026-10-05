// SPDX-License-Identifier: GPL-3.0-or-later
#include "rtc-session.h"
#include "video-profile.h"
#include <stdexcept>

namespace deskport::browser {
RtcSession::RtcSession(Callbacks callbacks) : callbacks_(std::move(callbacks)) {}
std::shared_ptr<RtcSession> RtcSession::create(bool audio, Callbacks callbacks,
                                             uint16_t firstPort, uint16_t lastPort) {
    auto result = std::shared_ptr<RtcSession>(new RtcSession(std::move(callbacks)));
    result->initialize(audio, firstPort, lastPort);
    return result;
}
void RtcSession::initialize(bool audioEnabled, uint16_t firstPort, uint16_t lastPort) {
    if (!firstPort || lastPort < firstPort) throw std::invalid_argument("Invalid ICE port range");
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    config.portRangeBegin = firstPort;
    config.portRangeEnd = lastPort;
    config.maxMessageSize = 8192;
    config.mtu = 1200;
    // No implicit third-party STUN/TURN service or outbound account dependency.
    pc_ = std::make_shared<rtc::PeerConnection>(config);
    std::weak_ptr<RtcSession> weak = shared_from_this();
    pc_->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState state) {
        if (state != rtc::PeerConnection::GatheringState::Complete) return;
        if (auto self = weak.lock()) {
            { std::lock_guard lock(self->gatherMutex_); self->gathered_ = true; }
            self->gatherCondition_.notify_all();
        }
    });
    pc_->onStateChange([weak](rtc::PeerConnection::State state) {
        if (auto self = weak.lock()) {
            if (!self->closed_ && (state == rtc::PeerConnection::State::Disconnected ||
                state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)) {
                if (self->callbacks_.disconnected) self->callbacks_.disconnected();
            }
        }
    });
    rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
    video.addH264Codec(102, std::string("profile-level-id=") + browserProfileLevelId + ";packetization-mode=1;level-asymmetry-allowed=1");
    video.addSSRC(1, "deskport", "deskport-stream", "deskport-video");
    video_ = pc_->addTrack(video);
    auto videoConfig = std::make_shared<rtc::RtpPacketizationConfig>(1, "deskport", 102, 90000);
    auto videoPacketizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, videoConfig, 1100);
    videoPacketizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(videoConfig));
    videoPacketizer->addToChain(std::make_shared<rtc::RtcpNackResponder>(512));
    videoPacketizer->addToChain(std::make_shared<rtc::PliHandler>([weak] {
        if (auto self = weak.lock(); self && !self->closed_ && self->callbacks_.keyframe) self->callbacks_.keyframe();
    }));
    video_->setMediaHandler(videoPacketizer);
    video_->onOpen([weak] {
        if (auto self = weak.lock(); self && !self->closed_ && self->callbacks_.keyframe) self->callbacks_.keyframe();
    });
    if (audioEnabled) {
        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(111, "minptime=5;useinbandfec=1;stereo=1;sprop-stereo=1");
        audio.addSSRC(2, "deskport", "deskport-stream", "deskport-audio");
        audio_ = pc_->addTrack(audio);
        auto audioConfig = std::make_shared<rtc::RtpPacketizationConfig>(2, "deskport", 111, 48000);
        auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(audioConfig);
        packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(audioConfig));
        audio_->setMediaHandler(packetizer);
    }
    input_ = pc_->createDataChannel("input");
    input_->onMessage(nullptr, [weak](std::string message) {
        if (message.size() > 8192) return;
        if (auto self = weak.lock(); self && !self->closed_ && self->callbacks_.input) self->callbacks_.input(std::move(message));
    });
    input_->onClosed([weak] {
        if (auto self = weak.lock(); self && !self->closed_ && self->callbacks_.disconnected) self->callbacks_.disconnected();
    });
    pc_->setLocalDescription(rtc::Description::Type::Offer);
}
RtcSession::~RtcSession() { close(); }
std::string RtcSession::offer(std::chrono::milliseconds timeout) {
    std::unique_lock lock(gatherMutex_);
    if (!gatherCondition_.wait_for(lock, timeout, [&] { return gathered_ || closed_; }) || closed_)
        throw std::runtime_error("ICE gathering did not finish");
    auto description = pc_->localDescription();
    if (!description) throw std::runtime_error("ICE offer unavailable");
    return std::string(*description);
}
void RtcSession::answer(const std::string& sdp) {
    if (closed_ || sdp.empty() || sdp.size() > 65536) throw std::invalid_argument("Invalid SDP answer");
    pc_->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
}
bool RtcSession::video(const uint8_t* data, size_t size, double seconds) {
    if (closed_ || !video_->isOpen() || !size || size > 8 * 1024 * 1024 || video_->bufferedAmount() > 256 * 1024) return false;
    video_->sendFrame(reinterpret_cast<const std::byte*>(data), size, std::chrono::duration<double>(seconds));
    return true;
}
bool RtcSession::audio(const uint8_t* data, size_t size, double seconds) {
    if (closed_ || !audio_ || !audio_->isOpen() || !size || size > 4096 || audio_->bufferedAmount() > 64 * 1024) return false;
    audio_->sendFrame(reinterpret_cast<const std::byte*>(data), size, std::chrono::duration<double>(seconds));
    return true;
}
bool RtcSession::connected() const { return !closed_ && pc_->state() == rtc::PeerConnection::State::Connected; }
std::string RtcSession::state() const {
    if (closed_) return "stopped";
    if (connected()) return "connected";
    return "connecting";
}
void RtcSession::close() {
    if (closed_.exchange(true)) return;
    gatherCondition_.notify_all();
    if (input_) input_->resetCallbacks();
    if (video_) video_->resetCallbacks();
    if (audio_) audio_->resetCallbacks();
    if (pc_) { pc_->resetCallbacks(); pc_->close(); }
}
}
