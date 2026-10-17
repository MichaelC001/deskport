// SPDX-License-Identifier: GPL-3.0-or-later
// Native acceptance probe, read-only by default. The explicit test-baseline
// switch changes only the exact installer-owned VDD source, never a physical one.
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include "../host/windows/display-dpi.h"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t instance[1024]{};DWORD bytes=sizeof(instance);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\DeskPort",L"VirtualDisplayDevice",RRF_RT_REG_SZ,nullptr,instance,&bytes))return 2;
    if(argc!=1) {
        if(argc!=3||std::wstring(argv[1])!=L"--test-set-owned")return 10;
        wchar_t* end=nullptr;const auto percentage=wcstol(argv[2],&end,10);if(*end)return 10;
        deskport::display_dpi::Source source{};deskport::display_dpi::Range range{};int relative=0;
        auto code=deskport::display_dpi::ownedSource(instance,source);if(!code)code=deskport::display_dpi::query(source,range);
        if(code||!deskport::display_dpi::relative(range,int(percentage),relative))return 11;
        if(deskport::display_dpi::set(source,relative))return 12;
    }
    UINT32 np=0,nm=0;
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,&nm)||np>64||nm>512)return 3;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,paths.data(),&nm,modes.data(),nullptr))return 4;
    std::cout<<"[";
    for(UINT32 i=0;i<np;++i) {
        const auto& path=paths[i];deskport::display_dpi::Range range{};
        const auto code=deskport::display_dpi::query({path.sourceInfo.adapterId,path.sourceInfo.id},range);
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        name.header.size=sizeof(name);name.header.adapterId=path.sourceInfo.adapterId;name.header.id=path.sourceInfo.id;
        if(DisplayConfigGetDeviceInfo(&name.header))return 5;
        DEVMODEW mode{};mode.dmSize=sizeof(mode);if(!EnumDisplaySettingsExW(name.viewGdiDeviceName,ENUM_CURRENT_SETTINGS,&mode,0))return 6;
        auto window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"DeskPort synthetic DPI probe",WS_POPUP,
            mode.dmPosition.x+10,mode.dmPosition.y+10,20,20,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        const auto dpi=window?GetDpiForWindow(window):0;if(window)DestroyWindow(window);
        if(i)std::cout<<",";
        std::cout<<"{\"adapterLow\":"<<path.targetInfo.adapterId.LowPart<<",\"adapterHigh\":"<<path.targetInfo.adapterId.HighPart
            <<",\"target\":"<<path.targetInfo.id<<",\"source\":"<<path.sourceInfo.id
            <<",\"owned\":"<<(deskport::display_dpi::ownedAdapter(path.targetInfo.adapterId,instance)?"true":"false")
            <<",\"percent\":"<<(code?0:deskport::display_dpi::percent(range))<<",\"relative\":"<<range.current
            <<",\"minimum\":"<<range.minimum<<",\"maximum\":"<<range.maximum<<",\"queryError\":"<<code
            <<",\"windowDpi\":"<<dpi<<",\"width\":"<<mode.dmPelsWidth<<",\"height\":"<<mode.dmPelsHeight<<"}";
    }
    std::cout<<"]\n";return 0;
}
