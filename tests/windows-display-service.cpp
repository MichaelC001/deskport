// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/windows/display-service-policy.h"
#include "host/windows/display-service-environment.h"
#include "host/windows/display-dpi-policy.h"
#include <cassert>
#include <cstring>
#include <cwchar>
#include <iostream>

using namespace deskport::display_service;
int main() {
    // Exhaust every documented percentage and recommended-scale position.
    // A range the OS does not expose must never silently clamp to another DPI.
    namespace dpi=deskport::display_dpi;
    for(int recommended=0;recommended<12;++recommended)for(int index=0;index<12;++index) {
        dpi::Range range{-recommended,index-recommended,11-recommended};
        assert(dpi::valid(range));assert(dpi::percent(range)==dpi::percentages[index]);
        int relative=123;assert(dpi::relative(range,dpi::percentages[index],relative));assert(relative==range.current);
    }
    for(const auto range:{dpi::Range{1,1,2},dpi::Range{-12,0,0},dpi::Range{-1,2,1},dpi::Range{-1,-2,1},dpi::Range{-2,0,10}}) {
        assert(!dpi::valid(range));assert(!dpi::percent(range));int ignored=0;assert(!dpi::relative(range,200,ignored));
    }
    int relative=0;assert(!dpi::relative({0,0,2},200,relative));assert(!dpi::relative({-2,0,2},110,relative));
    assert(dpi::relative({-2,0,2},100,relative)&&relative==-2);
    assert(dpi::relative({-2,0,2},200,relative)&&relative==2);
    for(unsigned operation=0;operation<32;++operation)for(unsigned scale=0;scale<4;++scale) {
        const bool expected=operation==dpi::SetDpi?(scale==1||scale==2):
            scale==0&&(operation==dpi::CaptureDpi||operation==dpi::RestoreDpi||operation==dpi::InspectDpi);
        assert(dpi::validRequest(operation,0,0,scale)==expected);
        if(operation!=dpi::ResizeMode)assert(!dpi::validRequest(operation,640,360,scale));
    }
    assert(dpi::validRequest(dpi::ResizeMode,1920,1080,0));
    assert(!dpi::validRequest(dpi::ResizeMode,1920,1080,1));
    assert(!dpi::validRequest(dpi::ResizeMode,640,361,0));
    std::cout<<"PASS per-source DPI range conversion, unsupported-scale rejection and bounded lease operations\n";
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
    const auto environment = systemEnvironment(L"D:\\Windows", L"E:\\Shared Data");
    std::vector<std::wstring> variables;
    for (const auto* entry = environment.data(); *entry; entry += std::wcslen(entry) + 1) variables.emplace_back(entry);
    assert((variables == std::vector<std::wstring>{L"ALLUSERSPROFILE=E:\\Shared Data", L"ProgramData=E:\\Shared Data",
        L"SystemDrive=D:", L"SystemRoot=D:\\Windows", L"WINDIR=D:\\Windows"}));
    assert(environment.size() >= 2 && environment[environment.size()-2] == 0 && environment.back() == 0);
    for (const auto& path : {L"", L"relative", L"\\\\server\\share", L"C:\\bad\npath"}) {
        assert(systemEnvironment(path, L"C:\\ProgramData").empty());
        assert(systemEnvironment(L"C:\\Windows", path).empty());
    }
    std::cout << "PASS display-service framing, nonce bounds, operations, authorization truth table and response validation\n";
    std::cout << "PASS canonical SYSTEM common-data environment without inherited user variables\n";
}
