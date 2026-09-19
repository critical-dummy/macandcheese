#include "mnc/bundle.hpp"
#include <stdexcept>

namespace mnc {
AppBundle BundleInspector::inspect(const std::filesystem::path& input) {
    AppBundle b; b.root=input;
    if (!std::filesystem::is_directory(input)) throw std::runtime_error("bundle path is not a directory: " + input.string());
    auto contents=input/"Contents"; if(!std::filesystem::is_directory(contents)) throw std::runtime_error("missing Contents directory");
    b.info_plist=contents/"Info.plist"; if(!std::filesystem::exists(b.info_plist)) b.diagnostics.push_back("Info.plist is missing");
    auto macos=contents/"MacOS"; if(std::filesystem::is_directory(macos)) for(auto& e: std::filesystem::directory_iterator(macos)) if(std::filesystem::is_regular_file(e)) { if(b.executable.empty()) b.executable=e.path(); }
    if(b.executable.empty()) b.diagnostics.push_back("Contents/MacOS contains no executable candidate");
    auto collect=[&](const std::filesystem::path& dir, auto& out){ if(std::filesystem::is_directory(dir)) for(auto& e:std::filesystem::recursive_directory_iterator(dir)) if(e.is_regular_file()) out.push_back(e.path()); };
    collect(contents/"Frameworks",b.frameworks); collect(contents/"PlugIns",b.plugins);
    b.diagnostics.push_back("bundle inspection only; Info.plist parsing and dependency resolution are loader-phase work");
    return b;
}
}
