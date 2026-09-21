#include "host/common/smartstream.h"
#include "host/common/inputactivity.h"
#include "host/common/framecadence.h"
#include "app/backend/streambudget.h"
#include <cassert>
#include <iostream>
#include <vector>
int main() {
    using namespace DeskPortStream;
    assert(initialBitrate(25000,2560,1440,60,false)==15000);
    assert(initialBitrate(7000,3840,2160,60,false)==7000);
    assert(initialBitrate(40000,1280,720,30,false)==5000);
    assert(initialBitrate(40000,2560,1440,60,true)==30000);
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
    assert(!cadence.admit(2350000,false,false)); // expired hold returns to idle
    assert(cadence.admit(2350001,false,true)); // recovery does not wait for idle
    deskport::FrameCadence pending(60);
    pending.policy.loss(8000); pending.policy.loss(9500); pending.policy.loss(11000);
    assert(pending.admit(12000000,true,false));
    assert(!pending.admit(12005000,true,false));
    assert(pending.admit(12034000,false,false)); // final update survives denied slot
    assert(!pending.admit(12068000,false,false)); // no healthy-frame recovery from idle
    deskport::FrameCadence other(30);
    assert(other.floor(12068000)==1); // no cross-session input state
    std::cout << "PASS: exact pixels, malformed/repaired FEC, burst hysteresis, frame cadence, recovery bypass, idle recovery guard, bandwidth ceilings\n";
}
