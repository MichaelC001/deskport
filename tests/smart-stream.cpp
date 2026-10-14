#include "host/common/smartstream.h"
#include "host/common/encoderpolicy.h"
#include "host/common/inputactivity.h"
#include "host/common/framecadence.h"
#include "host/common/ratecontrol.h"
#include "host/macos/scroll-pacer.h"
#include "app/backend/streambudget.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
int main() {
    deskport::KeyframeRequests keys;
    keys.submitted(10, true);
    keys.submitted(11, false);
    assert(!keys.take(7)); // Delayed packet must not consume the new request.
    assert(keys.take(10));
    assert(!keys.take(10));
    assert(!keys.take(11));
    keys.submitted(20, true);
    keys.submitted(21, true);
    assert(keys.take(21)); // Match even when a backend reorders outputs.
    assert(keys.take(20));

    using namespace DeskPortStream;
    for (bool smart : {false, true}) for (bool qualified : {false, true})
        for (int mode : {0, 1, 2, 4}) for (int attempt : {0, 1}) {
            const auto rate = deskport::rateControl(smart, qualified, mode, attempt);
            const bool expected = smart && qualified && mode == 2 && attempt == 0;
            assert(rate.boundedVbr == expected && rate.retryOriginalOnFailure == expected);
        }
    deskport::EncodedWindow encoded, otherSession;
    assert(!encoded.add(1000, 1000, true));
    assert(!encoded.add(5999, 2000, false));
    assert(encoded.add(6000, 7000, false));
    assert(encoded.bytes == 10000 && encoded.packets == 3 && encoded.keys == 1);
    assert(encoded.maxPacketBytes == 7000 && encoded.mbps(6000) == 0.016);
    assert(otherSession.bytes == 0);
    encoded.reset(6000);
    assert(encoded.bytes == 0 && encoded.keys == 0 && encoded.maxPacketBytes == 0);
    assert(!encoded.add(6000, 100, false) && encoded.mbps(6000) == 0.0);

    assert(initialBitrate(25000,2560,1440,60,false)==15000);
    assert(initialBitrate(7000,3840,2160,60,false)==7000);
    assert(initialBitrate(40000,1280,720,30,false)==5000);
    assert(initialBitrate(40000,2560,1440,60,true)==30000);

    // Remote classification: on-link/private paths keep LAN behavior.
    auto v4 = [](unsigned a, unsigned b, unsigned c, unsigned d) {
        const unsigned char bytes[4] = {(unsigned char)a, (unsigned char)b, (unsigned char)c, (unsigned char)d};
        return deskport::remotePeer(bytes, 4);
    };
    assert(!v4(192,168,0,9) && !v4(10,1,2,3) && !v4(172,16,0,1) && !v4(172,31,255,1));
    assert(!v4(127,0,0,1) && !v4(169,254,1,1));
    assert(v4(172,32,0,1) && v4(100,64,0,9) && v4(202,104,119,10));
    auto v6 = [](std::initializer_list<unsigned> prefix, unsigned last) {
        unsigned char bytes[16]{}; int i = 0;
        for (auto b : prefix) bytes[i++] = (unsigned char)b;
        bytes[15] = (unsigned char)last;
        return deskport::remotePeer(bytes, 16);
    };
    assert(!v6({}, 1) && !v6({0xfe,0x80}, 5) && !v6({0xfd,0x12}, 5));
    assert(v6({0xfd,0x7a,0x11,0x5c,0xa1,0xe0}, 0x0e) && v6({0x24,0x08}, 1));
    assert(!v6({0,0,0,0,0,0,0,0,0,0,0xff,0xff,192,168,0}, 1));
    assert(v6({0,0,0,0,0,0,0,0,0,0,0xff,0xff,100,64,0}, 9));
    assert(!deskport::remotePeer(nullptr, 4) && !deskport::remotePeer((const unsigned char*)"abc", 3));

    // Remote rate: queueing delay or unrecoverable loss lowers the target,
    // quiet periods restore it gradually, bounds always hold.
    deskport::BitrateController rate(8000, 0);
    assert(rate.floorKbps() == 1000 && rate.targetKbps(0) == 8000 && !rate.constrained());
    for (int t = 0; t <= 4000; t += 500) rate.rtt(t, 330);
    assert(rate.baselineMs() == 330 && rate.targetKbps(4000) == 8000);
    rate.rtt(4500, 420); // One delayed sample is jitter, not congestion.
    rate.rtt(5000, 330);
    assert(rate.targetKbps(5000) == 8000);
    rate.rtt(5500, 420); rate.rtt(6000, 430);
    assert(rate.targetKbps(6000) == 5600);
    rate.rtt(6500, 440); rate.rtt(7000, 450);
    assert(rate.targetKbps(7000) == 5600); // Let the queue drain first.
    rate.rtt(8000, 450); // Still queued two seconds after the first decrease.
    assert(rate.targetKbps(8000) == 3920 && rate.constrained());
    for (int t = 8500; t < 13000; t += 500) rate.rtt(t, 330);
    assert(rate.targetKbps(12999) == 3920);
    assert(rate.targetKbps(13000) == 4320 && !rate.constrained());
    assert(rate.targetKbps(15999) == 4320); // One step per three seconds.
    for (int t = 16500; t < 80000; t += 3000) rate.targetKbps(t);
    assert(rate.targetKbps(80000) == 8000);
    for (int t = 90000; t < 200000; t += 2000) rate.loss(t);
    assert(rate.targetKbps(200000) == 1000); // Floor holds.
    deskport::BitrateController slow(2000, 0);
    assert(slow.floorKbps() == 1000);
    deskport::BitrateController tiny(600, 0);
    assert(tiny.floorKbps() == 600 && tiny.targetKbps(0) == 600);
    // A stale minimum expires, so a lasting route change becomes the baseline.
    deskport::BitrateController route(8000, 0);
    route.rtt(0, 20);
    for (int t = 40000; t < 41000; t += 500) route.rtt(t, 330);
    assert(route.baselineMs() == 330);
    deskport::BitrateController resumed(8000, 50000);
    resumed.resume(500); assert(resumed.targetKbps(50000) == 1000);
    resumed.resume(3000); assert(resumed.targetKbps(52999) == 3000 && resumed.targetKbps(53000) == 3400);
    resumed.resume(20000); assert(resumed.targetKbps(53000) == 8000);
    deskport::RateMemory memory;
    assert(memory.resumeFor(8000, 0) == 0);
    memory.store(8000, 3000, 1000);
    assert(memory.resumeFor(8000, 301000) == 3000);
    assert(memory.resumeFor(8000, 301001) == 0 && memory.resumeFor(15000, 2000) == 0);
    assert(deskport::remotePacketsPerMs(3000, 1040) == 2);   // 20 Mbps floor
    assert(deskport::remotePacketsPerMs(15000, 1040) == 7);  // 4x target
    assert(deskport::remotePacketsPerMs(1, 1 << 20) == 1);

    // Scroll pacing: bounded steps, total and direction preserved.
    deskport::ScrollPacer pacer;
    pacer.add(-50, 0);
    auto step = pacer.take();
    assert(step.vertical == -40 && step.horizontal == 0 && pacer.pending());
    step = pacer.take();
    assert(step.vertical == -10 && !pacer.pending());
    assert(pacer.take().empty());
    pacer.add(250, -90);
    int v = 0, h = 0, steps = 0;
    while (pacer.pending()) {
        step = pacer.take(); ++steps;
        assert(std::abs(step.vertical) <= deskport::kMaxScrollStep && std::abs(step.horizontal) <= deskport::kMaxScrollStep);
        v += step.vertical; h += step.horizontal;
    }
    assert(v == 250 && h == -90 && steps == 7);
    pacer.add(120, 0); pacer.take(); pacer.add(-80, 0); // reversal cancels pending distance
    assert(!pacer.pending());
    pacer.add(30, 0); pacer.clear(); assert(!pacer.pending());
    // Distinct strides/padding; a one-byte change anywhere must be detected.
    std::vector<unsigned char> a(128*80,42), b(160*80,42);
    for(int y=0;y<80;++y) b[y*160+120]=99;
    assert(deskport::samePlane(a.data(),128,b.data(),160,120,80));
    for(int y=0;y<80;++y) for(int x=0;x<120;++x) {
        b[y*160+x]^=1;
        assert(!deskport::samePlane(a.data(),128,b.data(),160,120,80));
        b[y*160+x]^=1;
    }
    assert(!deskport::samePlane(nullptr,128,b.data(),160,120,80));
    assert(!deskport::samePlane(a.data(),100,b.data(),160,120,80));
    unsigned char fec[22]{}; fec[11]=100;fec[13]=20;fec[15]=90;fec[17]=5;fec[20]=1;
    assert(deskport::unrecoverableFec(fec,21));
    for(unsigned n=0;n<21;++n) assert(!deskport::unrecoverableFec(fec,n));
    assert(!deskport::unrecoverableFec(fec,22));
    fec[17]=10;assert(!deskport::unrecoverableFec(fec,21)); // repaired
    fec[17]=21;assert(!deskport::unrecoverableFec(fec,21)); // malformed
    deskport::StreamPolicy p(60);
    for(int n=0;n<100;++n)p.loss(8000+n); // one burst, no change
    assert(p.frameRate(8200)==60);
    p.loss(9500);p.loss(11000);assert(p.frameRate(11000)==30);
    int admitted=0;
    for(int n=0;n<600;++n) admitted+=p.admit(12000000LL+n*1000000LL/60);
    assert(admitted>=299 && admitted<=302);
    // A denied update stays pending until the next slot; no new capture is required.
    assert(!p.admit(21984000));
    assert(p.admit(22000000,true)); // recovery never waits
    assert(p.frameRate(40999)==30);assert(p.frameRate(42000)==60);
    deskport::StreamPolicy idle(60);
    idle.loss(8000);idle.loss(9500);idle.loss(11000);
    assert(idle.frameRate(100000)==30); // idle must not count as a healthy trial
    idle.loss(101000);idle.loss(102500);idle.loss(104000);
    assert(idle.frameRate(104000)==15);
    idle.loss(113000);idle.loss(114500);idle.loss(116000);
    assert(idle.frameRate(116000)==15);
    deskport::StreamPolicy low(10);
    low.loss(8000);low.loss(9500);low.loss(11000);assert(low.frameRate(11000)==10);
    // Input packets are classified without unaligned reads or retaining key data.
    auto packet = [](unsigned type, unsigned size) {
        std::vector<unsigned char> p(size);
        p[3] = size - 4;
        for (int i=0;i<4;++i) p[4+i] = (type >> (8*i)) & 255;
        return p;
    };
    deskport::InputActivity input;
    auto key = packet(3,14);
    auto boost = input.observe(key.data(),key.size(),1000);
    assert(deskport::activityFrameRate(boost,1000,120)==60);
    assert(deskport::activityFrameRate(boost,1000,15)==15); // loss cap wins
    assert(deskport::activityFrameRate(boost,1350,60)==1);
    input.observe(key.data(),key.size(),1100);
    boost=input.observe(key.data(),key.size(),1200);
    assert(boost.untilMs==1900); // continued typing holds activity
    assert(deskport::activityFrameRate(boost,1900,60)==1);
    for(unsigned n=0;n<14;++n) assert(input.observe(key.data(),n,1300).untilMs==0);
    key[3]=11;assert(input.observe(key.data(),key.size(),1300).untilMs==0);
    deskport::InputActivity pointer;
    auto move=packet(7,12);
    for(int i=0;i<1000;++i) {
        move[8]=(i%2)?255:0;move[9]=(i%2)?255:1;
        assert(pointer.observe(move.data(),move.size(),i*10).untilMs==0);
    }
    move[8]=0;move[9]=8;
    boost=pointer.observe(move.data(),move.size(),11000);
    assert(boost.fps==30 && boost.untilMs==11180);
    pointer.observe(move.data(),move.size(),11050);
    boost=pointer.observe(move.data(),move.size(),11100);
    assert(boost.fps==60 && boost.untilMs==11800);
    auto abs=packet(5,18);abs[15]=100;abs[17]=100;abs[9]=50;abs[11]=50;
    deskport::InputActivity absolute;
    assert(absolute.observe(abs.data(),abs.size(),1000).untilMs==0);
    for(int i=0;i<1000;++i) {
        abs[9]=50+(i%2);
        assert(absolute.observe(abs.data(),abs.size(),1010+i*10).untilMs==0);
    }
    abs[9]=60;assert(absolute.observe(abs.data(),abs.size(),12000).fps==30);
    assert(deskport::activityFrameRate({},0,60)==1);
    assert(deskport::activityFrameRate(boost,11100,1)==1);
    deskport::StreamPolicy absoluteClock(60, 100000);
    absoluteClock.loss(100000); absoluteClock.loss(101500); absoluteClock.loss(103000);
    assert(absoluteClock.frameRate(103000)==60); // same startup guard for both clocks
    absoluteClock.loss(105000); absoluteClock.loss(108000);
    assert(absoluteClock.frameRate(108000)==30);
    deskport::FrameCadence cadence(60);
    assert(cadence.admit(1000000,true,false));
    assert(!cadence.admit(1999999,false,false));
    assert(cadence.admit(2000000,false,false)); // one-second idle refresh
    assert(cadence.admit(2001000,true,false)); // changed content bypasses idle timer
    cadence.boost = {2350,60};
    assert(cadence.admit(2020000,false,false)); // input wakes duplicates immediately
    assert(!cadence.admit(2030000,false,false));
    assert(cadence.admit(2040000,false,false));
    assert(cadence.admit(2350000,false,false)); // still refining the last change
    assert(!cadence.admit(2600000,false,false)); // refinement over: idle floor
    assert(cadence.admit(2600001,false,true)); // recovery does not wait for idle
    assert(deskport::refineFrameRate(1499,1000,60)==60 && deskport::refineFrameRate(1500,1000,60)==1);
    assert(deskport::refineFrameRate(1200,1000,15)==15 && deskport::refineFrameRate(1200,-1,60)==1);
    deskport::FrameCadence pending(60);
    pending.policy.loss(8000); pending.policy.loss(9500); pending.policy.loss(11000);
    assert(pending.admit(12000000,true,false));
    assert(!pending.admit(12005000,true,false));
    assert(pending.admit(12034000,false,false)); // final update survives denied slot
    assert(pending.admit(12068000,false,false)); // refinement at the congestion ceiling
    assert(!pending.admit(12090000,false,false));
    assert(!pending.admit(12600000,false,false)); // then the idle floor
    deskport::FrameCadence other(30);
    assert(other.floor(12068000)==1); // no cross-session input state
    std::cout << "PASS: exact pixels, malformed/repaired FEC, burst hysteresis, frame cadence, recovery bypass, idle recovery guard, bandwidth ceilings, remote rate control, scroll pacing\n";
}
