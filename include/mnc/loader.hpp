#pragma once

#include "mnc/macho.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mnc::dyld { class DylibRegistry; }

namespace mnc::loader {

struct SegmentMapping {
    std::size_t command_index{};
    std::string name;
    std::uint64_t vm_address{};
    std::uint64_t vm_size{};
    std::uint64_t file_offset{};
    std::uint64_t file_size{};
    std::uint32_t protections{};
};

struct LoadPlan {
    bool valid{false};
    std::uint64_t image_min_address{};
    std::uint64_t image_max_address{};
    std::uint64_t load_bias{};
    std::uint64_t entry_address{};
    std::uint64_t file_base_offset{};
    std::uint32_t rebase_offset{};
    std::uint32_t rebase_size{};
    std::uint32_t bind_offset{};
    std::uint32_t bind_size{};
    std::uint32_t weak_bind_offset{};
    std::uint32_t weak_bind_size{};
    std::uint32_t lazy_bind_offset{};
    std::uint32_t lazy_bind_size{};
    std::uint32_t chained_fixups_offset{};
    std::uint32_t chained_fixups_size{};
    std::vector<SegmentMapping> segments;
    std::vector<std::string> resolved_dependencies;
    std::vector<std::string> unresolved_dependencies;
    std::vector<std::string> diagnostics;
};

struct BindReference {
    std::string stream;
    std::string symbol;
    std::int64_t library_ordinal{};
    std::size_t segment_index{};
    std::uint64_t segment_offset{};
    std::int64_t addend{};
};

LoadPlan make_load_plan(const std::filesystem::path& executable, const MachOImage& image);

struct MappingResult {
    bool mapped{false};
    std::uintptr_t base_address{};
    std::uint64_t load_bias{};
    std::string error;
    std::vector<BindReference> bind_references;
};

class MappedImage {
public:
    MappedImage() = default;
    MappedImage(const MappedImage&) = delete;
    MappedImage& operator=(const MappedImage&) = delete;
    MappedImage(MappedImage&& other) noexcept;
    MappedImage& operator=(MappedImage&& other) noexcept;
    ~MappedImage();

    static MappedImage map_file(const std::filesystem::path& executable, const LoadPlan& plan);
    bool apply_bindings(const mnc::dyld::DylibRegistry& registry, const std::filesystem::path& requesting_image);
    bool valid() const noexcept { return result_.mapped; }
    const MappingResult& result() const noexcept { return result_; }
    const LoadPlan& plan() const noexcept { return plan_; }

private:
    void reset() noexcept;
    LoadPlan plan_;
    MappingResult result_;
    void* allocation_{};
    std::size_t allocation_size_{};
};

} // namespace mnc::loader
