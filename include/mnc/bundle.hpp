#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mnc {

struct AppBundle {
    std::filesystem::path root;
    std::filesystem::path info_plist;
    std::filesystem::path executable;
    std::vector<std::filesystem::path> frameworks;
    std::vector<std::filesystem::path> plugins;
    std::vector<std::string> diagnostics;
};

class BundleInspector {
public:
    static AppBundle inspect(const std::filesystem::path& path);
};

} // namespace mnc
