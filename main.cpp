#include <sstream>
#include <iostream>
#include <string>
#include <list>
#include <windows.h>
#include <filesystem>
#include <regex>

#define BUFSIZE 1024

int main(int argc, char **argv) {
    if (argc <
        3) { // Needs self (always given), target program, target dll as args.
        std::cout << "Usage: " << argv[0]
                  << " <target executable> <DLLs to load> -args <arguments "
                     "passed to target executable>"
                  << std::endl;
        std::cout << "Example: " << argv[0]
                  << " music.exe volume.dll -args volume:100" << std::endl;
        return -1;
    }
    STARTUPINFO            start_info = {sizeof(start_info)};
    PROCESS_INFORMATION    process_info;
    std::string            program_name = argv[1];
    std::list<std::string> dlls;
    std::filesystem::path  target_binary_abs_path =
        std::filesystem::absolute(std::filesystem::path(program_name));

    // Format passed down to loader to pass to process.
    std::stringstream args;
    args << "\"" + target_binary_abs_path.string() + "\"";
    bool reading_args = false;
    for (int i = 2; i < argc; ++i) {
        if (!reading_args) {
            if (std::string(argv[i]) != "-args") {
                std::filesystem::path abs_dll_path =
                    std::filesystem::absolute(std::filesystem::path(argv[i]));
                std::string abs_dll_path_string = abs_dll_path.string();

                // Needs to be converted to this format for Windows.
                abs_dll_path_string = std::regex_replace(abs_dll_path_string, std::regex("\\\\"), "/");
                dlls.push_back(abs_dll_path_string);
                continue;
            }
            reading_args = true;
            continue;
        }
        args << " " << argv[i];
    }

    // Change the working directory to the target binary instead of the location
    // from which the loader is called.
    std::filesystem::current_path(target_binary_abs_path.parent_path());

    // Launch the target process.
    // &std::string[0] is equivalent to char*.
    if (CreateProcess(nullptr, &(args.str())[0], nullptr, nullptr, FALSE,
                      CREATE_SUSPENDED, nullptr, nullptr, &start_info,
                      &process_info)) {
        std::list<LPVOID> pages;
        for (std::string dll_name : dlls) {
            LPVOID page = VirtualAllocEx(
                process_info.hProcess, nullptr, BUFSIZE,
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (page == nullptr) {
                std::cerr << "VirtualAllocEx error: " << GetLastError()
                          << std::endl;
                return -1;
            }
            if (WriteProcessMemory(process_info.hProcess, page,
                                   dll_name.c_str(), dll_name.length(),
                                   nullptr) == 0) {
                std::cerr << "WriteProcessMemory error: " << GetLastError()
                          << std::endl;
                return -1;
            }
            HANDLE thread = CreateRemoteThread(
                process_info.hProcess, nullptr, 0,
                (LPTHREAD_START_ROUTINE)LoadLibraryA, page, 0, nullptr);
            if (thread == nullptr) {
                std::cerr << "CreateRemoteThread error: " << GetLastError()
                          << std::endl;
                return -1;
            }
            if (WaitForSingleObject(thread, INFINITE) == WAIT_FAILED) {
                std::cerr << "WaitForSingleObject error: " << GetLastError()
                          << std::endl;
                return -1;
            }
            CloseHandle(thread);
            std::cout << "Loaded " << dll_name << std::endl;
        }
        if (ResumeThread(process_info.hThread) == -1) {
            std::cerr << "ResumeThread error: " << GetLastError() << std::endl;
            return -1;
        }
        CloseHandle(process_info.hProcess);
        for (auto page : pages) {
            if (!VirtualFreeEx(process_info.hProcess, page, BUFSIZE,
                               MEM_RELEASE)) {
                std::cerr << "VirtualFreeEx error: " << GetLastError()
                          << std::endl;
                return -1;
            }
        }
        return 0;
    }
    std::cerr << "CreateProcess error: " << GetLastError() << std::endl;
    return -1;
}
