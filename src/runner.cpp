#include "mnc/runtime.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

static void usage() {
    std::cout << "Mac&Cheese native compatibility runtime\n"
              << "usage: mnc-run [--diagnose] <Application.app|disk.dmg|Mach-O>\n"
              << "       mnc-run --extract <disk.dmg> <destination>\n\n"
              << "options:\n"
              << "  --diagnose       prepare and report blockers without launching\n"
              << "  --extract        read-only extract a supported DMG into a Windows folder\n"
              << "  --version        print runtime version\n"
              << "  --help           show this help\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string command = argv[1];
    if (command == "--help" || command == "-h") { usage(); return 0; }
    if (command == "--version") { std::cout << "Mac&Cheese runtime 0.3.0 (framework development channel)\n"; return 0; }

    mnc::runtime::CompatibilityRuntime runtime;
    if (command == "--extract") {
        if (argc < 4) { std::cerr << "mnc-run --extract requires <disk.dmg> <destination>\n"; return 2; }
        std::vector<std::string> diagnostics;
        const bool ok = runtime.extract_dmg(argv[2], argv[3], diagnostics);
        for (const auto& diagnostic : diagnostics) std::cout << "[Mac&Cheese][Extract] " << diagnostic << "\n";
        return ok ? 0 : 3;
    }

    const bool diagnose_only = command == "--diagnose";
    const int path_index = diagnose_only ? 2 : 1;
    if (argc <= path_index) { std::cerr << "mnc-run: an app, DMG, or Mach-O path is required\n"; return 2; }
    auto report = runtime.prepare(std::filesystem::path(argv[path_index]));
    std::cout << "[Mac&Cheese][Runtime] input: " << report.input << "\n"
              << "[Mac&Cheese][Runtime] status: " << mnc::runtime::to_string(report.status) << "\n";
    if (!report.executable.empty()) std::cout << "[Mac&Cheese][Runtime] executable: " << report.executable << "\n";
    for (const auto& diagnostic : report.diagnostics) std::cout << "[Mac&Cheese][Runtime] " << diagnostic << "\n";
    if (report.status == mnc::runtime::LaunchStatus::Blocked) {
        std::cout << "[Mac&Cheese][BLOCKED] execution backend is not implemented for this input yet\n";
        return 3;
    }
    return report.status == mnc::runtime::LaunchStatus::ExecutableReady ? 0 : 1;
}
