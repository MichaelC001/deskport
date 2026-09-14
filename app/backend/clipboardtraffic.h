#pragma once
#include <atomic>
#include <cstdint>
namespace DeskPortTraffic {
inline std::atomic<uint64_t>& clipboardReceived() { static std::atomic<uint64_t> value{0}; return value; }
inline std::atomic<uint64_t>& clipboardSent() { static std::atomic<uint64_t> value{0}; return value; }
}
