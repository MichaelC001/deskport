// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <taskschd.h>
#include <oleauto.h>
#include <string>
#include <cwchar>
#include "protected-path.h"
template<class T> struct ComObject {
    T* p=nullptr;
    ~ComObject(){if(p)p->Release();}
    T* operator->() const{return p;}
    T** out(){return &p;}
};
struct BString {
    BSTR p;
    explicit BString(const wchar_t* text):p(SysAllocString(text)){}
    ~BString(){SysFreeString(p);}
    operator BSTR() const{return p;}
};
static HRESULT recoveryTask(const std::wstring& root,bool remove) {
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(initialized))return initialized;
    struct Uninitialize{~Uninitialize(){CoUninitialize();}} uninitialize;
    ComObject<ITaskService> service;
    HRESULT hr=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,reinterpret_cast<void**>(service.out()));
    if(FAILED(hr))return hr;
    VARIANT empty;VariantInit(&empty);
    if(FAILED(hr=service->Connect(empty,empty,empty,empty)))return hr;
    ComObject<ITaskFolder> folder;
    if(FAILED(hr=service->GetFolder(BString(L"\\"),folder.out())))return hr;
    BString name(L"DeskPortDisplayRecovery");
    const auto executable=root+L"\\host\\deskport-display-recovery.exe";
    if(!remove&&!protectedExecutable(executable))return E_ACCESSDENIED;
    ComObject<IRegisteredTask> existing;
    hr=folder->GetTask(name,existing.out());
    if(SUCCEEDED(hr)) {
        DWORD owned=0,size=sizeof(owned);
        if(RegGetValueW(HKEY_LOCAL_MACHINE,L"Software\\DeskPort",L"RecoveryTaskOwned",RRF_RT_REG_DWORD,nullptr,&owned,&size)||owned!=1)return E_ACCESSDENIED;
        ComObject<ITaskDefinition> definition;
        ComObject<IActionCollection> actions;
        ComObject<IAction> action;
        ComObject<IExecAction> exec;
        LONG count=0;BSTR path=nullptr,arguments=nullptr;
        if(FAILED(existing->get_Definition(definition.out()))||FAILED(definition->get_Actions(actions.out()))||FAILED(actions->get_Count(&count))||count!=1||FAILED(actions->get_Item(1,action.out()))||FAILED(action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(exec.out())))||FAILED(exec->get_Path(&path)))return E_ACCESSDENIED;
        bool matches=path&&!_wcsicmp(path,executable.c_str());SysFreeString(path);
        if(FAILED(exec->get_Arguments(&arguments)))return E_ACCESSDENIED;
        matches=matches&&arguments&&!wcscmp(arguments,L"boot-disable");SysFreeString(arguments);
        if(!matches)return E_ACCESSDENIED;
    }else if(hr!=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)&&hr!=HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))return hr;
    if(remove){
        if(existing.p&&FAILED(hr=folder->DeleteTask(name,0)))return hr;
        HKEY key;if(!RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"Software\\DeskPort",0,KEY_SET_VALUE,&key)){RegDeleteValueW(key,L"RecoveryTaskOwned");RegCloseKey(key);}
        return S_OK;
    }
    ComObject<ITaskDefinition> definition;
    ComObject<IRegistrationInfo> registration;
    ComObject<IPrincipal> principal;
    ComObject<ITaskSettings> settings;
    ComObject<ITriggerCollection> triggers;
    ComObject<ITrigger> trigger;
    ComObject<IActionCollection> actions;
    ComObject<IAction> action;
    ComObject<IExecAction> exec;
    if(FAILED(hr=service->NewTask(0,definition.out()))||FAILED(hr=definition->get_RegistrationInfo(registration.out()))||FAILED(hr=registration->put_Author(BString(L"DeskPort")))||FAILED(hr=definition->get_Principal(principal.out()))||FAILED(hr=principal->put_UserId(BString(L"SYSTEM")))||FAILED(hr=principal->put_LogonType(TASK_LOGON_SERVICE_ACCOUNT))||FAILED(hr=principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST))||FAILED(hr=definition->get_Settings(settings.out()))||FAILED(hr=settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE))||FAILED(hr=settings->put_StopIfGoingOnBatteries(VARIANT_FALSE))||FAILED(hr=settings->put_ExecutionTimeLimit(BString(L"PT1M")))||FAILED(hr=definition->get_Triggers(triggers.out()))||FAILED(hr=triggers->Create(TASK_TRIGGER_BOOT,trigger.out()))||FAILED(hr=definition->get_Actions(actions.out()))||FAILED(hr=actions->Create(TASK_ACTION_EXEC,action.out()))||FAILED(hr=action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(exec.out())))||FAILED(hr=exec->put_Path(BString(executable.c_str())))||FAILED(hr=exec->put_Arguments(BString(L"boot-disable"))))return hr;
    VARIANT user;VariantInit(&user);user.vt=VT_BSTR;user.bstrVal=SysAllocString(L"SYSTEM");
    ComObject<IRegisteredTask> registered;
    hr=folder->RegisterTaskDefinition(name,definition.p,existing.p?TASK_UPDATE:TASK_CREATE,user,empty,TASK_LOGON_SERVICE_ACCOUNT,empty,registered.out());
    VariantClear(&user);
    if(FAILED(hr))return hr;
    HKEY key;DWORD error=RegCreateKeyExW(HKEY_LOCAL_MACHINE,L"Software\\DeskPort",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(!error){DWORD one=1;error=RegSetValueExW(key,L"RecoveryTaskOwned",0,REG_DWORD,reinterpret_cast<const BYTE*>(&one),sizeof(one));RegCloseKey(key);}
    if(error&&!existing.p)folder->DeleteTask(name,0);
    return HRESULT_FROM_WIN32(error);
}
