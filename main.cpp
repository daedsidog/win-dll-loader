#include <sstream>
#include <iostream>
#include <string>
#include <list>
#include <windows.h>

#define BUFSIZE 1024

int main(int argc, char **argv) {
    if (argc <
        3) { // Needs self (always given), target program, target dll as args.
        std::cout << "Usage: " << argv[0]
                  << " <target program> <DLLs to load> -args <args>"
                  << std::endl;
        std::cout << "Arguments will be passed down to the target program."
                  << std::endl
                  << std::endl;
        std::cout << "Example: " << argv[0]
                  << " music.exe volume.dll -args volume:100" << std::endl;
        return 1;
    }
    STARTUPINFO start_info = {sizeof(start_info)};
    PROCESS_INFORMATION process_info;
    std::string program_name = argv[1];
    std::list<std::string> dlls;

    // Format passed down to loader to pass to process.
    std::stringstream args;
    args << program_name;
    bool reading_args = false;
    for (int i = 2; i < argc; ++i) {
        if (!reading_args) {
            if (std::string(argv[i]) != "-args") {
                dlls.push_back(argv[i]);
                continue;
            }
            reading_args = true;
            continue;
        }
        args << " " << argv[i];
    }
    // Launch the target process.
    // &std::string[0] is equivalent to char*.
    if (CreateProcess(nullptr, &(args.str())[0], nullptr, nullptr, FALSE,
                      CREATE_SUSPENDED, nullptr, nullptr, &start_info,
                      &process_info)) {
        std::list<LPVOID> pages;
        for (std::string dll_name : dlls) {
            LPVOID page = VirtualAllocEx(process_info.hProcess, nullptr, BUFSIZE,
                                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            pages.push_back(page);
            if (page == nullptr) {
                std::cerr << "VirtualAllocEx error: " << GetLastError()
                          << std::endl;
                return 1;
            }
            if (WriteProcessMemory(process_info.hProcess, page,
                                   dll_name.c_str(), sizeof(dll_name),
                                   nullptr) == 0) {
                std::cerr << "WriteProcessMemory error: " << GetLastError()
                          << std::endl;
                return 1;
            }
            HANDLE thread = CreateRemoteThread(process_info.hProcess, nullptr, 0,
                                               (LPTHREAD_START_ROUTINE)LoadLibraryA,
                                               page, 0, nullptr);
            if (thread == nullptr) {
                std::cerr << "CreateRemoteThread error: " << GetLastError()
                          << std::endl;
                return 1;
            }
            if (WaitForSingleObject(thread, INFINITE) == WAIT_FAILED) {
                std::cerr << "WaitForSingleObject error: " << GetLastError()
                          << std::endl;
                return 1;
            }
            CloseHandle(thread);
            std::cout << "Loaded " << dll_name << std::endl;
        }
        if (ResumeThread(process_info.hThread) == -1) {
            std::cerr << "ResumeThread error: " << GetLastError() << std::endl;
            return 1;
        }
        CloseHandle(process_info.hProcess);
        for(auto page : pages){
            VirtualFreeEx(process_info.hProcess, page, BUFSIZE, MEM_RELEASE);
        }
    }
}
