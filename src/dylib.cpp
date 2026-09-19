#include "mnc/dylib.hpp"

#include <algorithm>

namespace mnc::dyld {
namespace {
std::string key_for(const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal().string();
}
}

bool DylibRegistry::register_image(const std::filesystem::path& path, std::uintptr_t base_address, std::string& diagnostic, std::int64_t library_ordinal) {
    try {
        auto image = MachOParser::parse_file(path);
        if (image.kind == BinaryKind::Unsupported || image.cpu == CpuType::Unknown) {
            diagnostic = "unsupported dylib architecture: " + path.string();
            return false;
        }
        const auto key = key_for(path);
        images_[key] = RegisteredImage{path, std::move(image), base_address, library_ordinal};
        if (std::find(load_order_.begin(), load_order_.end(), key) == load_order_.end()) load_order_.push_back(key);
        diagnostic = "registered Mach-O image: " + path.string();
        return true;
    } catch (const std::exception& error) {
        diagnostic = "failed to register dylib " + path.string() + ": " + error.what();
        return false;
    }
}

const RegisteredImage* DylibRegistry::find_image(const std::filesystem::path& path) const {
    const auto it = images_.find(key_for(path));
    return it == images_.end() ? nullptr : &it->second;
}

std::filesystem::path DylibRegistry::resolve_path(const std::string& install_name,
                                                   const std::filesystem::path& requesting_image,
                                                   const std::vector<std::string>& rpaths,
                                                   const std::vector<std::filesystem::path>& search_paths) const {
    if (install_name.empty()) return {};
    const auto loader_path = requesting_image.parent_path();
    const auto executable_path = loader_path;
    std::vector<std::filesystem::path> candidates;
    auto append_candidate = [&](std::filesystem::path candidate) {
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) candidates.push_back(std::move(candidate));
    };
    if (install_name[0] == '/') append_candidate(install_name);
    else if (install_name.rfind("@loader_path/", 0) == 0) append_candidate(loader_path / install_name.substr(13));
    else if (install_name.rfind("@executable_path/", 0) == 0) append_candidate(executable_path / install_name.substr(18));
    else if (install_name.rfind("@rpath/", 0) == 0) {
        const auto suffix = install_name.substr(7);
        for (const auto& rpath : rpaths) {
            std::filesystem::path root = rpath;
            const auto rpath_string = rpath;
            if (rpath_string == "@loader_path") root = loader_path;
            else if (rpath_string == "@executable_path") root = executable_path;
            else if (rpath_string.find('@') != std::string::npos) continue;
            append_candidate(root / suffix);
        }
    } else {
        append_candidate(install_name);
    }
    for (const auto& root : search_paths) {
        append_candidate(root / install_name);
        const auto name = std::filesystem::path(install_name).filename();
        append_candidate(root / name);
    }
    return candidates.empty() ? std::filesystem::path{} : candidates.front();
}

SymbolResolution DylibRegistry::resolve(const std::string& symbol, const std::filesystem::path& requesting_image, std::int64_t library_ordinal) const {
    SymbolResolution result;
    result.symbol = symbol;
    const auto requester_key = key_for(requesting_image);
    const auto is_candidate = [&](const RegisteredImage& image, const std::string& key) {
        if (library_ordinal >= 0) return image.library_ordinal == library_ordinal;
        if (library_ordinal == -1) return key == requester_key;
        if (library_ordinal == -2) return !load_order_.empty() && key == load_order_.front();
        if (library_ordinal == -3 || library_ordinal == -4) return true; // flat lookup and weak lookup.
        return false;
    };
    for (const auto& key : load_order_) {
        const auto image_it = images_.find(key);
        if (image_it == images_.end() || !is_candidate(image_it->second, key)) continue;
        const auto& image = image_it->second;
        for (const auto& entry_symbol : image.image.symbol_table) {
            if (entry_symbol.name == symbol && (entry_symbol.type & 0xe0) == 0) {
                result.image_path = image.path;
                result.address = image.base_address + entry_symbol.value;
                result.resolved = true;
                result.diagnostic = "resolved from nlist_64 symbol value and registered load bias";
                return result;
            }
        }
    }
    result.diagnostic = "unresolved symbol '" + symbol + "' requested by " + requesting_image.string();
    return result;
}

} // namespace mnc::dyld
