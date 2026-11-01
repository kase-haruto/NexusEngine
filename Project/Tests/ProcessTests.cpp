#include "Platform/Process/Process.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>

using namespace NexusEngine;
namespace {
void Require(bool value, const char* message) { if(!value) throw std::runtime_error(message); }
std::filesystem::path Executable() {
    wchar_t buffer[32768]{}; const auto count = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    Require(count && count < std::size(buffer), "Cannot locate process test executable."); return buffer;
}
uint32_t Wait(Process& process) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for(;;) {
        auto state = process.Poll(); Require(state.has_value(), "Cannot poll test child.");
        if(*state) return **state;
        if(std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Test child did not exit.");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
uint32_t ReadPid(const std::filesystem::path& file, Process& owner) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for(;;) {
        uint32_t id = 0; std::ifstream input(file); input >> id;
        if(id) return id;
        auto state = owner.Poll(); Require(state && !*state, "Tree owner ended before publishing child PID.");
        if(std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Tree child PID timed out.");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if(argc > 1 && std::wstring_view(argv[1]) == L"--sleep") { Sleep(30000); return 0; }
        if(argc > 2 && std::wstring_view(argv[1]) == L"--tree") {
            auto child = Process::Start(Executable(), {L"--sleep"}, std::filesystem::current_path(), true);
            if(!child) return 2;
            { std::ofstream output{std::filesystem::path(argv[2])}; output << child->Id(); }
            Sleep(30000); return 0;
        }
        if(argc > 3 && std::wstring_view(argv[1]) == L"--probe") {
            Require(std::filesystem::equivalent(std::filesystem::current_path(), argv[3]), "Child working directory differs.");
            std::ofstream output{std::filesystem::path(argv[2]), std::ios::binary};
            for(int index = 4; index < argc; ++index) {
                const std::wstring argument{argv[index]}; const auto size = static_cast<uint32_t>(argument.size());
                output.write(reinterpret_cast<const char*>(&size), sizeof(size));
                output.write(reinterpret_cast<const char*>(argument.data()), static_cast<std::streamsize>(size * sizeof(wchar_t)));
            }
            std::cout << "STDOUT probe\n"; std::cerr << "STDERR probe\n"; return 259;
        }
        const auto executable = Executable();
        const auto root = executable.parent_path() / (L"Process 日本語 Test " + std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(root);
        const auto argumentsFile = root / L"Arguments.bin"; const auto log = root / L"ログ.txt";
        const std::vector<std::wstring> samples{L"", L"plain", L"日本語 空白", L"quote\"value", L"C:\\trailing slash\\", L"\\\\\"", L"a&b;$()|%PATH%", L"改行\n末尾"};
        std::vector<std::wstring> arguments{L"--probe", argumentsFile.wstring(), root.wstring()}; arguments.insert(arguments.end(), samples.begin(), samples.end());
        auto process = Process::Start(executable, arguments, root, true, log, true); Require(process.has_value(), "Cannot launch argv probe.");
        Process moved = std::move(*process); Require(Wait(moved) == 259, "Completed exit code 259 was confused with a running process.");
        std::ifstream input(argumentsFile, std::ios::binary);
        for(const auto& expected : samples) {
            uint32_t size = 0; input.read(reinterpret_cast<char*>(&size), sizeof(size)); Require(size == expected.size(), "Quoted argv length differs.");
            std::wstring actual(size, L'\0'); input.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(size * sizeof(wchar_t)));
            Require(input.good() && actual == expected, "Quoted argv does not round-trip.");
        }
        std::ifstream logs(log); const std::string bytes{std::istreambuf_iterator<char>(logs), {}};
        Require(bytes.find("STDOUT probe") != bytes.npos && bytes.find("STDERR probe") != bytes.npos, "Child standard streams were not captured.");
        Require(!Process::Start(executable, arguments, root, true, log), "Existing log was overwritten.");
        Require(!Process::Start(L"relative.exe", {}, root), "Relative executable was accepted.");
        Require(!Process::Start(executable, {std::wstring(L"bad\0argument",12)}, root), "Embedded NUL argument was accepted.");
        auto sibling = Process::Start(executable, {L"--sleep"}, root, true, {}, true); Require(sibling.has_value(), "Cannot start separate sibling job.");
        auto owned = Process::Start(executable, {L"--tree", (root / "Child.pid").wstring()}, root, true, {}, true); Require(owned.has_value(), "Cannot start owned process tree.");
        const auto childId = ReadPid(root / "Child.pid", *owned);
        HANDLE descendant = OpenProcess(SYNCHRONIZE, FALSE, childId); Require(descendant != nullptr, "Cannot observe descendant.");
        struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{descendant};
        Require(owned->Terminate().has_value(), "Cannot stop own job.");
        Require(WaitForSingleObject(descendant, 5000) == WAIT_OBJECT_0, "Owned descendant survived cancellation.");
        static_cast<void>(Wait(*owned));
        auto state = sibling->Poll(); Require(state && !*state, "Stopping one job terminated an unrelated sibling job.");
        Require(sibling->Terminate().has_value(), "Cannot stop test sibling."); static_cast<void>(Wait(*sibling));
        std::cout << "PASS: Unicode/quoted/empty argv, child CWD, logs, exit 259, owned-tree cancellation and sibling isolation.\n";
        return 0;
    } catch(const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
