// hook.hpp
//
// Minimal standalone implementation of the two hook helpers the Hangame ASI
// uses: Utils::hook::return_value and Utils::hook::write. This lets hangame.cpp
// build in a clean VS project with no dependency on the owner's native patch
// codebase.
//
// If/when you switch to the owner's real Utils::hook layer, just change the
// include in hangame.cpp back to their header and delete this file. The call
// sites won't need to change as long as their signatures match these.
//
// 32-bit (x86) ONLY. The client is 32-bit; these helpers assume 4-byte
// pointers and x86 opcodes.

#pragma once

#include <windows.h>
#include <cstdint>
#include <initializer_list>

namespace Utils
{
    namespace hook
    {
        namespace detail
        {
            // Write `len` bytes from `src` to `addr`, flipping page protection
            // around the write and restoring it after.
            inline void patch_bytes(uintptr_t addr, const uint8_t* src, size_t len)
            {
                DWORD oldProtect = 0;
                if (!VirtualProtect(reinterpret_cast<void*>(addr), len,
                                    PAGE_EXECUTE_READWRITE, &oldProtect))
                    return;

                memcpy(reinterpret_cast<void*>(addr), src, len);

                DWORD tmp = 0;
                VirtualProtect(reinterpret_cast<void*>(addr), len, oldProtect, &tmp);

                FlushInstructionCache(GetCurrentProcess(),
                                      reinterpret_cast<void*>(addr), len);
            }
        }

        // write(addr, { 0xEB, ... })
        // Overwrites raw bytes at addr. Used by the snippet to patch the data
        // server version check: write(0x0048210D, { 0xEB }).
        inline void write(uintptr_t addr, std::initializer_list<uint8_t> bytes)
        {
            // initializer_list storage is contiguous, so .begin() is a valid
            // pointer to the first of bytes.size() elements.
            detail::patch_bytes(addr, bytes.begin(), bytes.size());
        }

        // return_value(addr, int)
        // Patches the function at addr to immediately return the given 32-bit
        // value, by writing:
        //     B8 <imm32>   mov eax, imm32
        //     C3           ret
        // This is the classic "force this function to return X" stub. It
        // assumes a cdecl/no-args-cleanup return is acceptable at the call
        // site (which is how the snippet uses it for the Hangame-mode flag).
        inline void return_value(uintptr_t addr, int value)
        {
            uint8_t stub[6];
            stub[0] = 0xB8;                                  // mov eax,
            *reinterpret_cast<int32_t*>(&stub[1]) = value;  // imm32
            stub[5] = 0xC3;                                  // ret
            detail::patch_bytes(addr, stub, sizeof(stub));
        }

        // return_value(addr, const char*)
        // Same idea, but returns a pointer (e.g. a string buffer) in eax.
        // The pointer value is baked into the mov imm32, so the buffer the
        // pointer refers to MUST outlive every call to the patched function.
        // (hangame.cpp keeps the strings in static storage for exactly this.)
        inline void return_value(uintptr_t addr, const char* value)
        {
            uint8_t stub[6];
            stub[0] = 0xB8;                                          // mov eax,
            *reinterpret_cast<uintptr_t*>(&stub[1]) =
                reinterpret_cast<uintptr_t>(value);                 // imm32 = ptr
            stub[5] = 0xC3;                                          // ret
            detail::patch_bytes(addr, stub, sizeof(stub));
        }
    }
}
