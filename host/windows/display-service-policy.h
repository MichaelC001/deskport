// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace deskport::display_service {
constexpr std::uint32_t Magic = 0x44504453, Version = 1, Acquire = 1;
struct Request {
    std::uint32_t magic = Magic, version = Version, operation = Acquire;
    char16_t nonce[37]{};
    std::uint16_t reserved = 0;
};
struct Response {
    std::uint32_t magic = Magic, version = Version, error = 0, pid = 0;
};
static_assert(sizeof(Request) == 88);
static_assert(sizeof(Response) == 16);

inline bool validNonce(const char16_t* nonce) {
    for (unsigned i = 0; i < 36; ++i) {
        const auto c = nonce[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != u'-') return false;
        } else if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') ||
                     (c >= u'A' && c <= u'F'))) return false;
    }
    return nonce[36] == 0;
}
inline bool validRequest(const Request& request) {
    return request.magic == Magic && request.version == Version &&
        request.operation == Acquire && !request.reserved && validNonce(request.nonce);
}
// Facts come from kernel process/pipe tokens, never from the request payload.
struct Caller {
    std::uint32_t session = 0, console = 0;
    bool installed = false, alive = false, medium = false;
    bool sameUser = false, sameLogon = false, consoleUser = false;
};
inline bool authorized(const Caller& caller) {
    return caller.session && caller.console != 0xffffffffu && caller.session == caller.console &&
        caller.installed && caller.alive && caller.medium && caller.sameUser &&
        caller.sameLogon && caller.consoleUser;
}
inline bool validResponse(const Response& response) {
    return response.magic == Magic && response.version == Version &&
        (response.error ? response.pid == 0 : response.pid != 0);
}
}
