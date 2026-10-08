// SPDX-License-Identifier: GPL-3.0-or-later
#include "browser.h"
#include "input-codec.h"
#include "rtc-session.h"
#include "video-profile.h"
#include "src/globals.h"
#include "src/input.h"
#include "src/rtsp.h"
#include "src/deskport/common/inputactivity.h"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <thread>

namespace deskport::browser {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
static int64_t now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
static Json result(bool ok, const std::string& code = {}) {
    Json value{{"version", 1}, {"status", ok}};
    if (!code.empty()) value["code"] = code;
    return value;
}
struct Queues {
    // Video and input share one mail: capture publishes the touch port that maps
    // absolute pointer positions. Audio owns its mail so video can be restarted
    // at a new size without interrupting audio, input or the WebRTC transport.
    safe::mail_t mail = std::make_shared<safe::mail_raw_t>();
    safe::mail_t audioMail = std::make_shared<safe::mail_raw_t>();
    safe::mail_raw_t::queue_t<video::packet_t> video =
        std::make_shared<safe::mail_raw_t::queue_t<video::packet_t>::element_type>(mail, 4);
    safe::mail_raw_t::queue_t<audio::packet_t> audio =
        std::make_shared<safe::mail_raw_t::queue_t<audio::packet_t>::element_type>(mail, 32);
};
static std::mutex routes_mutex;
static std::map<void*, std::weak_ptr<Queues>> routes;
static std::shared_ptr<Queues> route(void* channel) {
    if (!channel) return {};
    std::lock_guard lock(routes_mutex);
    auto it = routes.find(channel);
    return it == routes.end() ? nullptr : it->second.lock();
}
safe::mail_raw_t::queue_t<video::packet_t> video_queue(void* channel) {
    if (auto queues = route(channel)) return queues->video;
    return mail::man->queue<video::packet_t>(mail::video_packets);
}
safe::mail_raw_t::queue_t<audio::packet_t> audio_queue(void* channel) {
    if (auto queues = route(channel)) return queues->audio;
    return mail::man->queue<audio::packet_t>(mail::audio_packets);
}
void publish_video(safe::mail_raw_t::queue_t<video::packet_t>& queue, video::packet_t packet) {
    if (route(packet->channel_data)) {
        // Freeze encoder-owned SPS substitutions before crossing the async boundary.
        // No AVPacket, SPS view, or encoder-owned pointer escapes into WebRTC.
        std::vector<uint8_t> bytes(packet->data(), packet->data() + packet->data_size());
        if (packet->is_idr() && packet->replacements) {
            for (const auto& replacement : *packet->replacements) {
                if (replacement.old.empty()) continue;
                const auto found = std::search(bytes.begin(), bytes.end(), replacement.old.begin(), replacement.old.end(),
                    [](uint8_t left, char right) { return left == uint8_t(right); });
                if (found == bytes.end()) continue;
                const auto offset = std::distance(bytes.begin(), found);
                bytes.erase(found, found + replacement.old.size());
                bytes.insert(bytes.begin() + offset, replacement._new.begin(), replacement._new.end());
            }
        }
        auto owned = std::make_unique<video::packet_raw_generic>(std::move(bytes), packet->frame_index(), packet->is_idr());
        owned->frame_timestamp = packet->frame_timestamp;
        owned->channel_data = packet->channel_data;
        packet = std::move(owned);
    }
    queue->raise(std::move(packet));
}

class Session final : public std::enable_shared_from_this<Session> {
public:
    explicit Session(std::string value) : id(std::move(value)) {}
    ~Session() { stop(); }
    std::string id;
    std::atomic<int64_t> lastActivity{now_ms()};
    std::atomic<bool> stopped{false}, failed{false};
    std::atomic<uint64_t> frames{0}, audioPackets{0};
    int64_t stoppedAt = 0, startedAt = 0;
    std::shared_ptr<RtcSession> rtc;
    // Encode the workspace at its own resolution so desktop text stays sharp,
    // within the host H.264 encoder limit and Level 5.1 (about 4K at 30 fps).
    // The SDP keeps advertising 42e01f like browsers' own WebRTC senders; the
    // bitstream carries the level matching the encoded size.
    static std::pair<int, int> encodedSize(const Json& body) {
        const int width = integer(body, "width", 320, 7680), height = integer(body, "height", 200, 4320);
        if ((width | height) & 1) throw std::invalid_argument("Video dimensions must be even");
        const double scale = std::min({1.0, 4096.0 / width, 2304.0 / height,
                                       std::sqrt(3840.0 * 2160.0 / (double(width) * height))});
        return {std::max(320, int(width * scale) / 4 * 4), std::max(200, int(height * scale) / 4 * 4)};
    }
    // The page asks for a bitrate at 720p; larger frames need more for the same
    // text quality. Scale with the linear size, bounded for LAN and tailnet use.
    static int scaledBitrate(int base, int width, int height) {
        const double factor = std::clamp(std::sqrt(double(width) * height / (1280.0 * 720.0)), 1.0, 2.5);
        return std::min(30000, int(base * factor));
    }
    void start(const Json& body) {
        const auto [width, height] = encodedSize(body);
        const int fps = std::min(30, integer(body, "fps", 1, 60));
        baseBitrate = std::min(14000, integer(body, "bitrateKbps", 1000, 40000));
        const int bitrate = scaledBitrate(baseBitrate, width, height);
        streamWidth = width; streamHeight = height; streamFps = fps; streamBitrate = bitrate;
        inputEnabled = body.value("input", true);
        const bool audioEnabled = body.value("audio", true);
        if (video::probe_encoders() != 0) throw std::runtime_error("capture-unavailable");
        queues = std::make_shared<Queues>();
        { std::lock_guard lock(routes_mutex); routes[queues.get()] = queues; }
        std::weak_ptr<Session> weak = shared_from_this();
        rtc = RtcSession::create(audioEnabled, {
            [weak](std::string message) {
                if (auto self = weak.lock()) {
                    try { self->input(Json::parse(message)); } catch (...) { /* bounded invalid input is discarded */ }
                }
            },
            [weak] { if (auto self = weak.lock(); self && !self->stopped) self->idr(); },
            [weak] { if (auto self = weak.lock()) self->failed = true; }
        });
#ifdef DESKPORT_BROWSER_INPUT_IDENTITY
        if (inputEnabled) inputContext = input::alloc(queues->mail, "browser:" + id);
#else
        if (inputEnabled) inputContext = input::alloc(queues->mail);
#endif
        video::config_t videoConfig{};
        videoConfig.width = width; videoConfig.height = height; videoConfig.framerate = fps;
#ifdef DESKPORT_BROWSER_FRAMERATE_X100
        videoConfig.framerateX100 = fps * 100;
#endif
        videoConfig.bitrate = bitrate; videoConfig.slicesPerFrame = 1;
        videoConfig.numRefFrames = 1; videoConfig.encoderCscMode = 0;
        videoConfig.videoFormat = 0; videoConfig.dynamicRange = 0; videoConfig.chromaSamplingType = 0;
        videoConfig.deskport_browser_baseline = true;
#ifdef DESKPORT_BROWSER_SMART
        videoConfig.deskport_smart = true;
#endif
        audio::config_t audioConfig{};
        audioConfig.channels = 2; audioConfig.mask = 3; audioConfig.packetDuration = 5;
        audioConfig.deskport_audio = audioEnabled;
        audioConfig.flags[audio::config_t::HOST_AUDIO] = true;
        startedAt = now_ms(); epoch = Clock::now();
        platf::streaming_will_start(); platformStarted = true;
        videoSender = std::jthread([this] { sendVideo(); });
        if (audioEnabled) audioSender = std::jthread([this] { sendAudio(); });
        currentVideo = videoConfig;
        startVideo();
        if (audioEnabled) audioCapture = std::jthread([this, audioConfig] {
            try { audio::capture(queues->audioMail, audioConfig, queues.get()); } catch (...) {}
            // Video-only continuation remains possible if the audio backend is unavailable.
        });
    }
    bool input(const Json& event) {
        auto decoded = decodeInput(event);
        if (stopped) return false;
        lastActivity = now_ms();
        if (decoded.heartbeat) return true;
        std::lock_guard lock(inputMutex);
        if (stopped || !inputContext) return false;
        if (decoded.release) { input::reset(inputContext); return true; }
        if (!inputEnabled) return false;
        const auto now = now_ms();
        if (now - inputWindow >= 1000) { inputWindow = now; inputCount = 0; }
        const auto packetCount = unsigned(decoded.packets.size());
        if (inputCount > 500 || packetCount > 500 - inputCount) {
            // Dropping an up event must never leave a held key or button behind.
            // Reset only once per budget window to keep floods off the input worker.
            if (inputCount <= 500) input::reset(inputContext);
            inputCount = 501;
            return false;
        }
        inputCount += packetCount;
        for (auto& packet : decoded.packets) {
            const auto boost = activity.observe(packet.data(), packet.size(), now);
            if (boost.untilMs > now) queues->mail->event<deskport::ActivityBoost>("deskport_activity")->raise(boost);
            input::passthrough(inputContext, std::move(packet));
        }
        return true;
    }
    // Restart only video capture/encoding at the size of a resized workspace.
    // Audio, input, the data channel and the WebRTC transport stay connected;
    // the new encoder starts with an IDR frame at the new resolution.
    bool resize(const Json& body) {
        const auto [width, height] = encodedSize(body);
        if (stopped || !queues || !rtc) return false;
        if (width == streamWidth && height == streamHeight) return true;
        ++videoGeneration; // The retiring capture thread must not mark the session failed.
        queues->mail->event<bool>(mail::shutdown)->raise(true);
        if (videoCapture.joinable()) videoCapture.join();
        if (stopped) return false;
        queues->mail->event<bool>(mail::shutdown)->reset();
        currentVideo.width = width; currentVideo.height = height;
        currentVideo.bitrate = streamBitrate = scaledBitrate(baseBitrate, width, height);
        streamWidth = width; streamHeight = height;
        startVideo();
        return true;
    }
    void stop() {
        if (stopped.exchange(true)) return;
        stoppedAt = now_ms();
        { std::lock_guard lock(inputMutex); if (inputContext) input::reset(inputContext); }
        if (queues) {
            queues->mail->event<bool>(mail::shutdown)->raise(true);
            queues->audioMail->event<bool>(mail::shutdown)->raise(true);
            queues->video->stop(); queues->audio->stop();
        }
        if (rtc) rtc->close();
        if (videoSender.joinable()) videoSender.join();
        if (audioSender.joinable()) audioSender.join();
        if (videoCapture.joinable()) videoCapture.join();
        if (audioCapture.joinable()) audioCapture.join();
        if (inputContext) {
            // The single native input worker is fenced before another owner is admitted.
            task_pool.push([] {}).wait(); inputContext.reset();
        }
        if (queues) { std::lock_guard lock(routes_mutex); routes.erase(queues.get()); }
        if (platformStarted) { platf::streaming_will_stop(); platformStarted = false; }
    }
    Json status() const {
        auto value = result(true);
        value["id"] = id; value["state"] = stopped ? "stopped" : rtc ? rtc->state() : "reserved";
        value["connected"] = !stopped && rtc && rtc->connected();
        value["frames"] = frames.load(); value["audioPackets"] = audioPackets.load();
        if (streamWidth) {
            value["width"] = streamWidth; value["height"] = streamHeight;
            value["fps"] = streamFps; value["bitrateKbps"] = streamBitrate;
            value["profileLevelId"] = browserProfileLevelId;
        }
        return value;
    }
private:
    std::shared_ptr<Queues> queues;
    std::shared_ptr<input::input_t> inputContext;
    std::jthread videoCapture, audioCapture, videoSender, audioSender;
    std::mutex inputMutex;
    bool inputEnabled = true, platformStarted = false;
    int64_t inputWindow = 0; unsigned inputCount = 0;
    int streamWidth = 0, streamHeight = 0, streamFps = 0, streamBitrate = 0, baseBitrate = 0;
    video::config_t currentVideo{};
    std::atomic<uint64_t> videoGeneration{0};
    void startVideo() {
        const auto generation = ++videoGeneration;
        videoCapture = std::jthread([this, config = currentVideo, generation] {
            try { video::capture(queues->mail, config, queues.get()); } catch (...) {}
            if (!stopped && generation == videoGeneration) failed = true;
        });
    }
    deskport::InputActivity activity;
    Clock::time_point epoch;
    void idr() { if (queues) queues->mail->event<bool>(mail::idr)->raise(true); }
    void sendVideo() {
        int64_t previous = -1;
        bool needsIdr = true;
        while (auto packet = queues->video->pop()) {
            if (stopped) break;
            if (!rtc->connected()) { needsIdr = true; continue; }
            if (previous >= 0 && packet->frame_index() != previous + 1) needsIdr = true;
            previous = packet->frame_index();
            if (needsIdr && !packet->is_idr()) { idr(); continue; }
            const auto timestamp = packet->frame_timestamp.value_or(Clock::now());
            try {
                if (rtc->video(packet->data(), packet->data_size(), std::max(0.0, std::chrono::duration<double>(timestamp - epoch).count()))) {
                    ++frames; needsIdr = false;
                } else { needsIdr = true; idr(); }
            } catch (...) { failed = true; break; }
        }
    }
    void sendAudio() {
        double timestamp = 0;
        while (auto packet = queues->audio->pop()) {
            if (stopped) break;
            // Anchor the first captured audio packet to the same monotonic media epoch.
            if (!timestamp) timestamp = std::max(0.0, std::chrono::duration<double>(Clock::now() - epoch).count());
            try {
                const auto& bytes = packet->second;
                if (rtc->audio(bytes.begin(), bytes.size(), timestamp)) ++audioPackets;
            } catch (...) { break; }
            timestamp += 0.005;
        }
    }
};

class Manager {
public:
    std::shared_ptr<Session> session;
    std::jthread watchdog{[this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(250ms);
            std::lock_guard lock(deskport_admission::mutex);
            if (!session) continue;
            const auto now = now_ms();
            if (!session->stopped && (session->failed || now - session->lastActivity > 30000 ||
                (session->startedAt && now - session->startedAt > 15000 && session->rtc && !session->rtc->connected()))) session->stop();
            // The supervisor normally restores the display then releases. Bound an
            // orphaned reservation when the supervisor itself disappeared.
            if (session->stopped && now - session->stoppedAt > 15000) release();
        }
    }};
    void release() {
        if (!session) return;
        session->stop();
        if (deskport_admission::lease == session->id) {
            deskport_admission::certificate.clear(); deskport_admission::lease.clear();
            ++deskport_admission::generation;
        }
        session.reset();
    }
};
static Manager& manager() { static Manager value; return value; }
bool reserved() {
    std::lock_guard lock(deskport_admission::mutex);
    return bool(manager().session);
}
void stop_all() {
    std::lock_guard lock(deskport_admission::mutex);
    if (manager().session) manager().session->stop();
}
void shutdown() {
    auto& owner = manager();
    owner.watchdog.request_stop();
    if (owner.watchdog.joinable()) owner.watchdog.join();
    std::lock_guard lock(deskport_admission::mutex);
    owner.release();
}
Json request(const Json& body) {
    std::lock_guard lock(deskport_admission::mutex);
    auto& owner = manager();
    try {
        const auto action = body.at("action").get<std::string>();
        const auto id = body.at("id").get<std::string>();
        if (id.size() < 16 || id.size() > 64 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        })) return result(false, "invalid-id");
        if (action == "reserve") {
            if (owner.session && owner.session->id == id) return owner.session->status();
            if (owner.session || !deskport_admission::lease.empty() || rtsp_stream::session_count() != 0) return result(false, "busy");
            owner.session = std::make_shared<Session>(id);
            deskport_admission::certificate = "browser:" + id; deskport_admission::lease = id;
            ++deskport_admission::generation;
            return owner.session->status();
        }
        if (!owner.session) {
            if (action == "stop" || action == "release") return result(true);
            return result(false, "not-found");
        }
        if (owner.session->id != id) return result(false, "unauthorized");
        auto session = owner.session;
        if (action == "start") {
            if (session->rtc || session->stopped) return result(false, "already-started");
            if (body.contains("videoCapabilities")) {
                const auto& codecs = body.at("videoCapabilities");
                if (!codecs.is_array() || codecs.size() > 64) return result(false, "invalid-request");
                if (!supportsBrowserVideo(codecs)) return result(false, "unsupported-codec");
            }
            session->start(body);
            auto value = session->status(); value["type"] = "offer"; value["sdp"] = session->rtc->offer();
            return value;
        }
        if (action == "answer") {
            if (session->stopped || !session->rtc || body.value("type", "") != "answer") return result(false, "invalid-state");
            session->rtc->answer(body.at("sdp").get<std::string>()); session->lastActivity = now_ms();
        } else if (action == "input") {
            if (!session->input(body.at("event"))) return result(false, "input-disabled");
        } else if (action == "resize") {
            if (!session->resize(body)) return result(false, "invalid-state");
            session->lastActivity = now_ms();
        } else if (action == "heartbeat") {
            if (!session->stopped) session->lastActivity = now_ms();
        } else if (action == "stop") {
            session->stop();
            if (!body.value("keepReservation", true)) { owner.release(); return result(true); }
        } else if (action == "release") {
            owner.release(); return result(true);
        } else if (action != "status") return result(false, "invalid-action");
        return session->status();
    } catch (const std::invalid_argument&) {
        if (owner.session && owner.session->rtc) owner.session->stop();
        return result(false, "invalid-request");
    } catch (const std::exception&) {
        if (owner.session) owner.session->stop();
        return result(false, "media-unavailable");
    }
}
}
