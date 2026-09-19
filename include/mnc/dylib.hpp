#pragma once

#include "mnc/macho.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace mnc::dyld {

struct RegisteredImage {
    std::filesystem::path path;
    MachOImage image;
    std::uintptr_t base_address{};
    std::int64_t library_ordinal{};
};

struct SymbolResolution {
    bool resolved{false};
    std::string symbol;
    std::filesystem::path image_path;
    std::uintptr_t address{};
    std::string diagnostic;
};

class DylibRegistry {
public:
    bool register_image(const std::filesystem::path& path, std::uintptr_t base_address, std::string& diagnostic, std::int64_t library_ordinal = -1);
    std::filesystem::path resolve_path(const std::string& install_name,
                                       const std::filesystem::path& requesting_image,
                                       const std::vector<std::string>& rpaths,
                                       const std::vector<std::filesystem::path>& search_paths = {}) const;
    const RegisteredImage* find_image(const std::filesystem::path& path) const;
    SymbolResolution resolve(const std::string& symbol, const std::filesystem::path& requesting_image, std::int64_t library_ordinal = -3) const;
    std::size_t size() const noexcept { return images_.size(); }

private:
    std::unordered_map<std::string, RegisteredImage> images_;
    std::vector<std::string> load_order_;
};

} // namespace mnc::dyld
