// hangame.cpp
//
// Hangame-mode ASI for psobb.exe (TethVer12513).
//
// Loaded by patch.dll's built-in ASI loader, which scans the "patches"
// folder and the game root for .asi files and calls an exported
//   void __stdcall load(void);
// on each one.
//
// This ASI is SELF-GATING: on load it looks for "hangame.ini" next to
// psobb.exe. If the file is absent, it does nothing and returns -- so the
// same .asi is harmless on standard (registry) launches. The launcher only
// writes hangame.ini when launching a profile whose AuthMode == Hangame.
//
// On finding the file it:
//   1. reads username/password,
//   2. deletes the file (tight on-disk window for the plaintext password),
//   3. applies the four memory hooks that put the client into Hangame mode.
//
// Credential format (enforced launcher-side, per newserv issue #401):
//   username : ends in "@HG", <= 11 chars
//   password : numeric, 1-8 digits
//
// ---------------------------------------------------------------------------
// DEPENDENCY ON THE FORK'S HOOK LAYER
// ---------------------------------------------------------------------------
// This file uses Utils::hook::return_value and Utils::hook::write, which come
// from the fork (the owner's snippet uses them). Include whatever header
// declares them. The two calls that pass g_username/g_password assume
// return_value stores the POINTER (does not copy the string); that is why the
// strings live in static storage that outlives load(). If the fork's
// return_value copies the bytes instead, the static storage is simply
// belt-and-suspenders and still correct. Confirm against the real header.
// ---------------------------------------------------------------------------

#include <windows.h>
#include <string>
#include <fstream>

// Standalone hook helpers (return_value / write). Swap this for the owner's
// real Utils::hook header later if desired; call sites won't need to change.
#include "hook.hpp"

namespace hangame
{
    // Long-lived storage. The pointers handed to return_value below must stay
    // valid for the lifetime of the process, so these are static and never
    // freed. Do NOT make these locals.
    static std::string g_username;
    static std::string g_password;

    static std::string trim(const std::string& s)
    {
        const char* ws = " \t\r\n";
        size_t a = s.find_first_not_of(ws);
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(ws);
        return s.substr(a, b - a + 1);
    }

    // Directory containing the running psobb.exe (no trailing slash).
    static std::string game_dir()
    {
        char buf[MAX_PATH]{};
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string exePath(buf, n);
        size_t slash = exePath.find_last_of("\\/");
        return (slash == std::string::npos) ? "." : exePath.substr(0, slash);
    }

    // Parse hangame.ini. Returns true only if both fields were found.
    // Tolerates the optional "[hangame]" section header and blank lines.
    static bool read_handoff(const std::string& dir)
    {
        std::string path = dir + "\\hangame.ini";

        std::ifstream f(path);
        if (!f.is_open())
            return false;

        std::string line;
        while (std::getline(f, line))
        {
            std::string t = trim(line);
            if (t.empty() || t[0] == '[' || t[0] == ';' || t[0] == '#')
                continue;

            size_t eq = t.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = trim(t.substr(0, eq));
            std::string val = trim(t.substr(eq + 1));

            if (key == "username")      g_username = val;
            else if (key == "password") g_password = val;
        }
        f.close();

        // Wipe the handoff file as soon as it's read, regardless of validity.
        DeleteFileA(path.c_str());

        return !g_username.empty() && !g_password.empty();
    }

    void apply()
    {
        if (!read_handoff(game_dir()))
            return;  // no/invalid hangame.ini -> not a Hangame launch.

        // --- The four hooks from the owner's snippet ---

        // Set Hangame mode
        Utils::hook::return_value(0x0082D2F8, 1);

        // Bypass data server version check (else the client stalls at the
        // patch screen because it thinks its version is wrong). TethVer12513.
        Utils::hook::write(0x0048210D, { 0xEB });

        // Set the User ID and Token into memory (this is what's sent to the
        // server). g_username/g_password are static, so the pointers stay
        // valid for as long as the client needs them.
        Utils::hook::return_value(0x0082D300, g_username.c_str());
        Utils::hook::return_value(0x0082D308, g_password.c_str());
    }
}

// The loader resolves this by name. Exported undecorated as "load" via the
// .def file (see hangame.def) so GetProcAddress(h, "load") succeeds despite
// __stdcall name decoration (_load@0).
extern "C" __declspec(dllexport) void __stdcall load(void)
{
    hangame::apply();
}
