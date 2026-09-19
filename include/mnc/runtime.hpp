#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mnc::runtime {

enum class LaunchStatus { ExecutableReady, Blocked, InvalidInput, Unsupported };

struct LaunchReport {
    LaunchStatus status{LaunchStatus::InvalidInput};
    std::filesystem::path input;
    std::filesystem::path executable;
    std::vector<std::string> diagnostics;
};

class CompatibilityRuntime {
public:
    CompatibilityRuntime();
    LaunchReport prepare(const std::filesystem::path& input) const;
    bool extract_dmg(const std::filesystem::path& input,
                     const std::filesystem::path& destination,
                     std::vector<std::string>& diagnostics) const;
};

const char* to_string(LaunchStatus status);

} // namespace mnc::runtime
