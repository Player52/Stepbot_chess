// stepbot_wrapper.cpp
// Tiny wrapper that launches "python run.py" in the same directory.
// Lucas Chess points at this .exe — it passes UCI commands straight
// through to the Python engine via stdin/stdout.
//
// Compile with:
//   g++ -o stepbot.exe stepbot_wrapper.cpp -m64 -static -lkernel32
//   (MinGW-w64 on Windows, or cross-compile on Linux)

#include <windows.h>
#include <string>

int main()
{
    // ── Find the directory this .exe lives in ──
    // This means run.py is always found relative to stepbot.exe,
    // regardless of where Lucas Chess calls it from.
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    // Strip the filename to get just the directory
    std::string dir(exe_path);
    size_t last_slash = dir.find_last_of("\\/");
    if (last_slash != std::string::npos)
        dir = dir.substr(0, last_slash);

    // ── Build the command ──
    // Runs: python run.py
    // Working directory is set to the Stepbot folder so all imports work.
    std::string command = "python \"" + dir + "\\run.py\"";

    // ── Set up the process ──
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    // Inherit stdin/stdout/stderr so UCI commands pass through cleanly
    si.dwFlags     = STARTF_USESTDHANDLES;
    si.hStdInput   = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput  = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError   = GetStdHandle(STD_ERROR_HANDLE);

    // ── Launch Python ──
    BOOL success = CreateProcessA(
        NULL,                          // No explicit executable path
        (LPSTR)command.c_str(),        // Command: python run.py
        NULL,                          // Default process security
        NULL,                          // Default thread security
        TRUE,                          // Inherit handles (stdin/stdout)
        0,                             // No creation flags
        NULL,                          // Inherit environment
        dir.c_str(),                   // Working directory = Stepbot folder
        &si,
        &pi
    );

    if (!success) {
        // Python wasn't found — show a helpful error
        MessageBoxA(
            NULL,
            "Stepbot could not start Python.\n\n"
            "Please make sure Python is installed and added to your PATH.\n"
            "Download from: https://www.python.org",
            "Stepbot — Python Not Found",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    // ── Wait for Python to finish ──
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Clean up
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}
