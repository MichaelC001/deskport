// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <string>
// A SYSTEM task must never execute code replaceable by an unelevated account.
// Check every existing component, including owners, ACLs and junctions. This is
// deliberately conservative for custom installation locations.
static bool protectedExecutable(const std::wstring& input) {
    wchar_t full[32768]{};
    const DWORD length=GetFullPathNameW(input.c_str(),32768,full,nullptr);
    if(!length||length>=32768||full[1]!=L':'||full[2]!=L'\\')return false;
    const std::wstring path(full);
    if(path.find(L':',2)!=std::wstring::npos)return false;
    PSID system=nullptr,admins=nullptr,installer=nullptr;
    if(!ConvertStringSidToSidW(L"S-1-5-18",&system)||
       !ConvertStringSidToSidW(L"S-1-5-32-544",&admins)||
       !ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",&installer)) {
        if(system)LocalFree(system);if(admins)LocalFree(admins);if(installer)LocalFree(installer);return false;
    }
    const auto trusted=[&](PSID sid){return sid&&(EqualSid(sid,system)||EqualSid(sid,admins)||EqualSid(sid,installer));};
    bool safe=true;
    for(size_t end=3;safe;) {
        const auto component=path.substr(0,end);
        const DWORD attrs=GetFileAttributesW(component.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT)){safe=false;break;}
        PSECURITY_DESCRIPTOR descriptor=nullptr;PSID owner=nullptr;PACL acl=nullptr;
        const DWORD error=GetNamedSecurityInfoW(const_cast<wchar_t*>(component.c_str()),SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&descriptor);
        if(error||!acl||!trusted(owner)){if(descriptor)LocalFree(descriptor);safe=false;break;}
        for(DWORD i=0;i<acl->AceCount&&safe;++i){
            void* raw=nullptr;if(!GetAce(acl,i,&raw)){safe=false;break;}
            auto header=static_cast<ACE_HEADER*>(raw);
            if(header->AceFlags&INHERIT_ONLY_ACE)continue;
            if(header->AceType==ACCESS_ALLOWED_ACE_TYPE){
                auto ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);
                DWORD mask=ace->Mask;GENERIC_MAPPING mapping{FILE_GENERIC_READ,FILE_GENERIC_WRITE,FILE_GENERIC_EXECUTE,FILE_ALL_ACCESS};MapGenericMask(&mask,&mapping);
                const DWORD dangerous=WRITE_DAC|WRITE_OWNER|DELETE|FILE_DELETE_CHILD|FILE_WRITE_DATA|
                    (end>3?FILE_APPEND_DATA:0)|
                    (end==path.size()?(FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES):0);
                if((mask&dangerous)&&!trusted(reinterpret_cast<PSID>(&ace->SidStart)))safe=false;
            }else if(header->AceType!=ACCESS_DENIED_ACE_TYPE){safe=false;}
        }
        LocalFree(descriptor);
        if(end==path.size())break;
        end=path.find(L'\\',end==3?3:end+1);if(end==std::wstring::npos)end=path.size();
    }
    LocalFree(system);LocalFree(admins);LocalFree(installer);return safe;
}
