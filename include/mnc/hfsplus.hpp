#pragma once

#include "mnc/dmg.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace mnc::filesystem {

struct HfsCatalogEntry {
    std::uint32_t node_id{};
    std::uint32_t parent_id{};
    bool folder{false};
    std::string name;
    std::string path;
    std::uint64_t logical_size{};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> data_extents;
};

struct HfsPlusProbe {
    bool recognized{false};
    bool hfsx{false};
    std::uint64_t volume_start_sector{};
    std::uint32_t block_size{};
    std::uint32_t total_blocks{};
    std::uint32_t free_blocks{};
    std::uint32_t file_count{};
    std::uint32_t folder_count{};
    std::uint32_t catalog_start_block{};
    std::uint16_t catalog_node_size{};
    std::uint32_t catalog_root_node{};
    std::uint32_t catalog_leaf_records{};
    std::uint32_t catalog_first_leaf{};
    std::uint32_t catalog_last_leaf{};
    std::uint32_t catalog_leaf_nodes_scanned{};
    std::uint32_t catalog_leaf_record_types{};
    std::uint32_t catalog_file_records{};
    std::uint32_t catalog_folder_records{};
    std::int8_t catalog_first_leaf_kind{};
    std::uint16_t catalog_first_leaf_node_records{};
    std::uint16_t catalog_first_record_offset{};
    std::uint16_t catalog_second_record_offset{};
    std::vector<HfsCatalogEntry> catalog_entries;
    std::string diagnostic;
};

HfsPlusProbe probe_hfs_plus(const std::filesystem::path& image, const mnc::image::DmgReport& report);

bool extract_hfs_file(const std::filesystem::path& image,
                      const mnc::image::DmgReport& report,
                      const HfsPlusProbe& volume,
                      const HfsCatalogEntry& entry,
                      std::vector<std::uint8_t>& output,
                      std::string& diagnostic);

bool extract_hfs_volume(const std::filesystem::path& image,
                        const mnc::image::DmgReport& report,
                        const HfsPlusProbe& volume,
                        const std::filesystem::path& destination,
                        std::string& diagnostic);

} // namespace mnc::filesystem
