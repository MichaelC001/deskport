// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace deskport::display_dpi {
// Windows' per-source DPI packets express offsets from the recommended entry,
// not percentages. Reject unknown ranges rather than guessing or clamping.
constexpr int percentages[]{100,125,150,175,200,225,250,300,350,400,450,500};
struct Range { int minimum, current, maximum; };
inline bool valid(Range r) {
    return r.minimum <= 0 && r.minimum >= -11 && r.maximum >= 0 &&
        r.maximum <= 11 && r.current >= r.minimum && r.current <= r.maximum &&
        r.maximum-r.minimum < int(sizeof(percentages)/sizeof(percentages[0]));
}
inline int percent(Range r) { return valid(r) ? percentages[r.current-r.minimum] : 0; }
inline bool relative(Range r,int percentage,int& result) {
    if(!valid(r))return false;
    for(int i=0;i<int(sizeof(percentages)/sizeof(percentages[0]));++i)
        if(percentages[i]==percentage) {result=i+r.minimum;return result>=r.minimum&&result<=r.maximum;}
    return false;
}
enum Operation : std::uint32_t { ResizeMode=1, CaptureDpi=2, SetDpi=3, RestoreDpi=4, InspectDpi=5, ResetDpi=6 };
inline bool validRequest(std::uint32_t operation,std::uint32_t width,std::uint32_t height,std::uint32_t scale) {
    if(operation==ResizeMode)return width>=640&&width<=7680&&height>=360&&height<=4320&&!(width%4)&&!(height%4)&&scale==0;
    if(width||height)return false;
    if(operation==SetDpi)return scale==1||scale==2;
    return !scale&&(operation==CaptureDpi||operation==RestoreDpi||operation==InspectDpi||operation==ResetDpi);
}
}
