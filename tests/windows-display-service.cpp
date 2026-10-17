// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/windows/display-service-policy.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace deskport::display_service;
int main() {
    Request request;
    const char16_t nonce[] = u"01234567-89ab-cdef-0123-456789abcdef";
    std::memcpy(request.nonce, nonce, sizeof(nonce));
    assert(validRequest(request));
    for (unsigned i = 0; i < 37; ++i) {
        auto bad = request;
        bad.nonce[i] = i == 36 ? u'f' : u'\0';
        assert(!validRequest(bad));
    }
    for (unsigned i = 0; i < 36; ++i) {
        auto bad = request; bad.nonce[i] = u'/'; assert(!validRequest(bad));
        bad.nonce[i] = u'\u2212'; assert(!validRequest(bad));
    }
    for (std::uint32_t operation = 0; operation < 1024; ++operation) {
        auto candidate = request; candidate.operation = operation;
        assert(validRequest(candidate) == (operation == Acquire));
    }
    auto bad = request; bad.magic ^= 1; assert(!validRequest(bad));
    bad = request; ++bad.version; assert(!validRequest(bad));
    bad = request; bad.reserved = 1; assert(!validRequest(bad));

    // Exhaust every combination of the kernel-derived authorization facts.
    // A foreign process, logon, user, low IL, dead PID or missing console
    // identity must fail even when every other fact is true.
    for (unsigned facts = 0; facts < 64; ++facts) {
        Caller caller{1, 1, bool(facts & 1), bool(facts & 2), bool(facts & 4),
            bool(facts & 8), bool(facts & 16), bool(facts & 32)};
        assert(authorized(caller) == (facts == 63));
    }
    Caller caller{1, 1, true, true, true, true, true, true};
    assert(authorized(caller));
    caller.session = 0; caller.console = 0; assert(!authorized(caller));
    caller.session = 1; caller.console = 0xffffffffu; assert(!authorized(caller));
    caller.console = 2; assert(!authorized(caller));
    caller.session = 0xffffffffu; assert(!authorized(caller));
    caller.console = 0xffffffffu; assert(!authorized(caller));

    Response response; assert(!validResponse(response));
    response.pid = 123; assert(validResponse(response));
    response.error = 5; assert(!validResponse(response));
    response.pid = 0; assert(validResponse(response));
    response.magic ^= 1; assert(!validResponse(response));
    response = {}; response.version = Version + 1; response.pid = 1; assert(!validResponse(response));
    std::cout << "PASS display-service framing, nonce bounds, operations, authorization truth table and response validation\n";
}
