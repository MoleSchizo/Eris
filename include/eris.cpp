#include "eris.h"
#include <iostream>
#include <tlhelp32.h>

namespace eris {
    DWORD get_pid(const std::string& process_name) {
        DWORD proc_id = 0;
        HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (h_snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe;
            pe.dwSize = sizeof(pe);

            if (Process32First(h_snap, &pe)) {
                if (!pe.th32ProcessID)
                    Process32Next(h_snap, &pe);
                do {
                    if (!_stricmp(pe.szExeFile, process_name.c_str())) {
                        proc_id = pe.th32ProcessID;
                        break;
                    }
                } while (Process32Next(h_snap, &pe));
            }
        }
        CloseHandle(h_snap);
        return proc_id;
    }

    bool is_valid(HANDLE handle) {
        return handle && handle != INVALID_HANDLE_VALUE;
    }

    HANDLE hijack(DWORD target_process_id) {
        HMODULE ntdll = GetModuleHandleA("ntdll");
        auto rtl_adjust_privilege = (_rtl_adjust_privilege)
            GetProcAddress(ntdll, "RtlAdjustPrivilege");
        BOOLEAN old_priv;
        rtl_adjust_privilege(20, TRUE, FALSE, &old_priv);

        auto nt_query_system_information = (_nt_query_system_information)
            GetProcAddress(ntdll, "NtQuerySystemInformation");
        auto nt_duplicate_object = (_nt_duplicate_object)
            GetProcAddress(ntdll, "NtDuplicateObject");
        auto nt_open_process = (_nt_open_process)
            GetProcAddress(ntdll, "NtOpenProcess");

        object_attributes obj_attribute = { sizeof(object_attributes) };
        client_id client_id = { 0 };
        DWORD size = sizeof(system_handle_information);
        auto h_info = std::make_unique<BYTE[]>(size);
        ZeroMemory(h_info.get(), size);
        NTSTATUS nt_ret;

        do {
            h_info.reset(new BYTE[size *= 2]);
        } while ((nt_ret = nt_query_system_information(k_system_handle_information,
            reinterpret_cast<psystem_handle_information>(h_info.get()), size,
            nullptr)) == k_status_info_length_mismatch);

        if (!NT_SUCCESS(nt_ret))
            return nullptr;

        for (unsigned int i = 0; i < reinterpret_cast<psystem_handle_information>(h_info.get())->handle_count; ++i)
        {
            auto handle = reinterpret_cast<psystem_handle_information>(h_info.get())->handles[i];
            if (!is_valid((HANDLE)handle.handle))
                continue;
            if (handle.object_type_number != k_process_handle_type)
                continue;
            client_id.unique_process = (HANDLE)handle.process_id;
            HANDLE proc_handle;
            nt_ret = nt_open_process(&proc_handle, PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                &obj_attribute,
                &client_id);
            if (!is_valid(proc_handle) || !NT_SUCCESS(nt_ret))
                continue;

            HANDLE hijacked_handle;
            nt_ret = nt_duplicate_object(proc_handle,
                (HANDLE)handle.handle,
                nt_current_process,
                &hijacked_handle,
                PROCESS_ALL_ACCESS,
                0,
                0);
            if (!is_valid(hijacked_handle) || !NT_SUCCESS(nt_ret))
                continue;

            if (GetProcessId(hijacked_handle) != target_process_id)
            {
                CloseHandle(hijacked_handle);
                continue;
            }
            return hijacked_handle;
        }
        return nullptr;
    }
}