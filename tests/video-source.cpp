#include "streaming/video/queuedframesource.h"
#include <QCoreApplication>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>

static QByteArray packet(int number, bool idr = false, quint64 generation = 7) {
    char sps[] = "parameter-set";
    char picture[] = "encoded-picture";
    LENTRY second{nullptr, picture, sizeof(picture), BUFFER_TYPE_PICDATA};
    LENTRY first{&second, sps, sizeof(sps), BUFFER_TYPE_SPS};
    DECODE_UNIT unit{};
    unit.frameNumber = number; unit.frameType = idr ? FRAME_TYPE_IDR : FRAME_TYPE_PFRAME;
    unit.bufferList = &first; unit.fullLength = sizeof(sps) + sizeof(picture);
    unit.receiveTimeMs = 100; unit.enqueueTimeMs = 105; unit.presentationTimeMs = 19;
    unit.hdrActive = true; unit.colorspace = COLORSPACE_REC_2020;
    unit.frameHostProcessingLatency = 28;
    return EncodedVideoFrame::encode(unit, generation);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto bytes = packet(42, true);
    auto frame = EncodedVideoFrame::decode(bytes, 1000);
    assert(frame && frame->generation == 7 && frame->unit.frameNumber == 42);
    assert(frame->unit.receiveTimeMs == 995 && frame->unit.enqueueTimeMs == 1000);
    assert(frame->unit.frameHostProcessingLatency == 28 && frame->unit.hdrActive);
    assert(frame->unit.colorspace == COLORSPACE_REC_2020 && frame->unit.presentationTimeMs == 19);
    assert(frame->unit.bufferList->bufferType == BUFFER_TYPE_SPS);
    assert(QByteArray(frame->unit.bufferList->next->data) == "encoded-picture");
    assert(EncodedVideoFrame::encode(frame->unit, frame->generation) == bytes);
    for (int length = 0; length < bytes.size(); ++length)
        assert(!EncodedVideoFrame::decode(bytes.left(length), 1000));
    assert(!EncodedVideoFrame::decode(bytes + 'x', 1000));
    auto invalid = bytes; invalid[0] = 0;
    assert(!EncodedVideoFrame::decode(invalid, 1000));
    // Never loop forever on a cyclic or overlong producer chain.
    frame->unit.bufferList->next = frame->unit.bufferList;
    assert(EncodedVideoFrame::encode(frame->unit, 7).isEmpty());

    int requests = 0;
    QueuedVideoSource source(7, [&] { ++requests; });
    auto push = [&](int number, bool idr = false, quint64 generation = 7) {
        return source.push(EncodedVideoFrame::decode(packet(number, idr, generation), 1000));
    };
    assert(!push(1)); // startup requires a key frame
    assert(requests == 1);
    assert(!push(1) && requests == 1); // no request storm while waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(510));
    assert(!push(1) && requests == 2); // recover a request lost during host pause
    assert(!push(1, true, 6)); // stale generation
    assert(push(2, true) && push(3) && push(4));
    assert(!push(5) && requests == 3); // dependent chain dropped on overflow
    VIDEO_FRAME_HANDLE handle = nullptr;
    PDECODE_UNIT unit = nullptr;
    assert(!source.poll(&handle, &unit));
    assert(!push(6) && requests == 3);
    assert(push(7, true) && source.wait(&handle, &unit));
    assert(unit->frameNumber == 7);
    assert(push(8));
    source.complete(handle, DR_NEED_IDR);
    assert(requests == 4 && !source.poll(&handle, &unit));
    // A completion error on one source cannot flush a different session.
    QueuedVideoSource other(7, [] {});
    assert(other.push(EncodedVideoFrame::decode(packet(9, true), 1000)));
    source.wake(); // wake before wait must not deadlock
    assert(!source.wait(&handle, &unit));
    assert(other.poll(&handle, &unit) && unit->frameNumber == 9);
    other.complete(handle, DR_OK);
    auto waiting = std::async(std::launch::async, [&] {
        VIDEO_FRAME_HANDLE h; PDECODE_UNIT u;
        return source.wait(&h, &u);
    });
    source.stop();
    assert(waiting.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    assert(!waiting.get() && !push(10, true));
    std::cout << "PASS: owned encoded frames, malformed input, backpressure, generation isolation, wake and stop\n";
}
