// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic, per-monitor-aware visual/input fixture; never reads the clipboard.
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include "../host/windows/display-dpi.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
static HWND edit=nullptr,button=nullptr;static HFONT font=nullptr;static WNDPROC editProc=nullptr;
static unsigned frame=0,downs=0,ups=0,clicks=0,dpi=96;static ULONGLONG started=0;
static std::wstring directory,instance;static RECT last{};
static int px(int value){return MulDiv(value,int(dpi),96);}
static void save(HWND window) {
    wchar_t text[256]{};GetWindowTextW(edit,text,256);std::string safe;
    for(const auto c:std::wstring(text)){if(c>=32&&c<127&&c!=L'"'&&c!=L'\\')safe+=char(c);}
    RECT bounds{};GetWindowRect(window,&bounds);
    std::ofstream file(std::filesystem::path(directory)/L"fixture.json",std::ios::binary|std::ios::trunc);
    file<<"{\"frame\":"<<frame<<",\"elapsedMs\":"<<(GetTickCount64()-started)<<",\"windowDpi\":"<<GetDpiForWindow(window)
        <<",\"width\":"<<bounds.right-bounds.left<<",\"height\":"<<bounds.bottom-bounds.top
        <<",\"keyDown\":"<<downs<<",\"keyUp\":"<<ups<<",\"clicks\":"<<clicks<<",\"text\":\""<<safe<<"\"}\n";
}
static void layout(HWND window) {
    dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
    auto next=CreateFontW(-px(24),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
    if(font)DeleteObject(font);font=next;
    MoveWindow(edit,px(40),px(180),px(580),px(44),TRUE);MoveWindow(button,px(40),px(250),px(280),px(70),TRUE);
    InvalidateRect(window,nullptr,FALSE);
}
static void position(HWND window) {
    deskport::display_dpi::Source source{};if(deskport::display_dpi::ownedSource(instance,source))return;
    DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    name.header.size=sizeof(name);name.header.adapterId=source.adapter;name.header.id=source.id;
    if(DisplayConfigGetDeviceInfo(&name.header))return;
    DEVMODEW mode{};mode.dmSize=sizeof(mode);if(!EnumDisplaySettingsExW(name.viewGdiDeviceName,ENUM_CURRENT_SETTINGS,&mode,0))return;
    RECT next{mode.dmPosition.x,mode.dmPosition.y,mode.dmPosition.x+LONG(mode.dmPelsWidth),mode.dmPosition.y+LONG(mode.dmPelsHeight)};
    if(!EqualRect(&last,&next)){last=next;SetWindowPos(window,HWND_TOPMOST,next.left,next.top,next.right-next.left,next.bottom-next.top,SWP_NOACTIVATE);layout(window);}
}
static LRESULT CALLBACK editMessages(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_KEYDOWN)++downs;if(message==WM_KEYUP)++ups;
    return CallWindowProcW(editProc,window,message,wp,lp);
}
static LRESULT CALLBACK messages(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_CREATE) {
        edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,window,reinterpret_cast<HMENU>(1),GetModuleHandleW(nullptr),nullptr);
        button=CreateWindowExW(0,L"BUTTON",L"POINTER TEST",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,window,reinterpret_cast<HMENU>(2),GetModuleHandleW(nullptr),nullptr);
        editProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editMessages)));
        position(window);layout(window);SetFocus(edit);SetTimer(window,1,33,nullptr);return 0;
    }
    if(message==WM_DPICHANGED){layout(window);save(window);return 0;}
    if(message==WM_COMMAND&&LOWORD(wp)==2&&HIWORD(wp)==BN_CLICKED){++clicks;const auto title=L"CLICKS "+std::to_wstring(clicks);SetWindowTextW(button,title.c_str());return 0;}
    if(message==WM_TIMER) {
        ++frame;if(frame%15==0){position(window);save(window);}InvalidateRect(window,nullptr,FALSE);
        if(GetFileAttributesW((directory+L"\\stop-fixture").c_str())!=INVALID_FILE_ATTRIBUTES||GetTickCount64()-started>900000)DestroyWindow(window);
        return 0;
    }
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT client{};GetClientRect(window,&client);
        auto bg=CreateSolidBrush(RGB(12,24,48));FillRect(dc,&client,bg);DeleteObject(bg);SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);SetTextColor(dc,RGB(240,245,255));
        const std::wstring title=L"DESKPORT WINDOWS DPI TEST - SYNTHETIC";TextOutW(dc,px(40),px(36),title.c_str(),int(title.size()));
        SYSTEMTIME now{};GetSystemTime(&now);std::wostringstream clock;clock<<L"DPI "<<dpi<<L" / "<<dpi*100/96<<L"%   FRAME "<<frame<<L"   "<<std::setfill(L'0')<<std::setw(2)<<now.wHour<<L":"<<std::setw(2)<<now.wMinute<<L":"<<std::setw(2)<<now.wSecond<<L" UTC";
        const auto label=clock.str();SetTextColor(dc,RGB(0,235,225));TextOutW(dc,px(40),px(100),label.c_str(),int(label.size()));
        auto bar=CreateSolidBrush(RGB(255,170,0));const int span=int(client.right)-px(240)>20?int(client.right)-px(240):20;const int left=px(40)+int(frame*7)%span;RECT moving{left,px(360),left+px(160),px(410)};FillRect(dc,&moving,bar);DeleteObject(bar);
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_DESTROY){save(window);KillTimer(window,1);if(font)DeleteObject(font);PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2||!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))return 2;
    directory=argv[1];wchar_t value[1024]{};DWORD bytes=sizeof(value);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\DeskPort",L"VirtualDisplayDevice",RRF_RT_REG_SZ,nullptr,value,&bytes))return 3;instance=value;
    started=GetTickCount64();WNDCLASSW cls{};cls.lpfnWndProc=messages;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"DeskPortSyntheticDpi";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&cls))return 4;
    const auto window=CreateWindowExW(WS_EX_TOPMOST,cls.lpszClassName,L"DeskPort synthetic DPI/input fixture",WS_POPUP|WS_VISIBLE,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window)return 5;MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}
