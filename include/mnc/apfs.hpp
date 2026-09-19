#pragma once

#include "mnc/dmg.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace mnc::filesystem {

struct ApfsProbe {
    bool recognized{false};
    bool gpt_detected{false};
    std::uint64_t apfs_start_sector{};
    std::uint32_t block_size{};
    std::uint64_t gpt_partition_start_sector{};
    std::uint64_t gpt_partition_end_sector{};
    std::string partition_type;
    std::string diagnostic;
};

ApfsProbe probe_apfs(const std::filesystem::path& image, const mnc::image::DmgReport& report);

} // namespace mnc::filesystem
