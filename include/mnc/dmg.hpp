#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mnc::image {

struct DmgExtent {
    std::uint32_t type{};
    std::uint64_t sector_start{};
    std::uint64_t sector_count{};
    std::uint64_t data_offset{};
    std::uint64_t data_length{};
};

struct DmgReport {
    bool is_udif{false};
    std::uint64_t image_size{};
    std::uint32_t version{};
    std::uint32_t header_size{};
    std::uint32_t flags{};
    std::uint64_t data_fork_offset{};
    std::uint64_t data_fork_length{};
    std::uint64_t resource_fork_offset{};
    std::uint64_t resource_fork_length{};
    std::uint64_t xml_offset{};
    std::uint64_t xml_length{};
    std::uint64_t sector_count{};
    bool has_block_map{false};
    bool has_property_list{false};
    std::vector<DmgExtent> extents;
    std::string format;
    std::vector<std::string> diagnostics;
};

class DmgImage {
public:
    static DmgReport inspect(const std::filesystem::path& path);

    // Reads logical 512-byte sectors from raw/zero UDIF extents.
    // Compressed extents are reported as unsupported until their decoder is enabled.
    static bool read_sectors(const std::filesystem::path& path,
                             const DmgReport& report,
                             std::uint64_t sector_start,
                             std::uint64_t sector_count,
                             std::vector<std::uint8_t>& output,
                             std::string& diagnostic);
};

} // namespace mnc::image
