// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/audio.h"
#include "src/video.h"
#include <nlohmann/json.hpp>

namespace deskport::browser {
// Only the authenticated loopback management handler may call request().
nlohmann::json request(const nlohmann::json& body);
bool reserved();
void stop_all();
void shutdown();
safe::mail_raw_t::queue_t<video::packet_t> video_queue(void* channel);
safe::mail_raw_t::queue_t<audio::packet_t> audio_queue(void* channel);
void publish_video(safe::mail_raw_t::queue_t<video::packet_t>& queue, video::packet_t packet);
}
