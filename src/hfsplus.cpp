#include "mnc/hfsplus.hpp"

#include <vector>
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <cctype>

namespace mnc::filesystem {
namespace {
std::uint16_t be16(const std::vector<std::uint8_t>& b, std::size_t p) {
    return static_cast<std::uint16_t>((std::uint16_t(b[p]) << 8) | b[p + 1]);
}
std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t p) {
    return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p + 1]) << 16) | (std::uint32_t(b[p + 2]) << 8) | b[p + 3];
}
bool read_bytes(const std::filesystem::path& image, const mnc::image::DmgReport& report,
                std::uint64_t offset, std::uint64_t length, std::vector<std::uint8_t>& out, std::string& diagnostic) {
    constexpr std::uint64_t sector = 512;
    const auto first = offset / sector;
    const auto last = (offset + length + sector - 1) / sector;
    std::vector<std::uint8_t> raw;
    if (!mnc::image::DmgImage::read_sectors(image, report, first, last - first, raw, diagnostic)) return false;
    const auto begin = static_cast<std::size_t>(offset - first * sector);
    if (begin + length > raw.size()) { diagnostic = "HFS+ byte range is outside the logical sector buffer"; return false; }
    out.assign(raw.begin() + static_cast<std::ptrdiff_t>(begin), raw.begin() + static_cast<std::ptrdiff_t>(begin + length));
    return true;
}
std::string hfs_name(const std::vector<std::uint8_t>& node, std::size_t p, std::size_t end) {
    if (p + 8 > end) return {};
    const auto key_length = be16(node, p);
    if (p + 2 + key_length > end || key_length < 6) return {};
    const auto name_length = be16(node, p + 6);
    if (name_length > 255 || p + 8 + name_length * 2 > end) return {};
    std::string name;
    for (std::size_t i = 0; i < name_length; ++i) {
        const auto code = be16(node, p + 8 + i * 2);
        if (code < 0x80) name.push_back(static_cast<char>(code)); else name.push_back('?');
    }
    return name;
}
std::string windows_component(std::string component) {
    for (auto& c : component) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') c = '_';
    }
    while (!component.empty() && (component.back() == ' ' || component.back() == '.')) component.back() = '_';
    if (component.empty() || component == "." || component == "..") component = "_";
    const auto dot = component.find('.');
    const auto base = component.substr(0, dot);
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || (base.size() == 3 && (base[0] == 'C' || base[0] == 'c') && (base[1] == 'O' || base[1] == 'o') && (base[2] >= 'M' && base[2] <= 'm'))) component = "_" + component;
    return component;
}
std::filesystem::path windows_relative_path(const std::string& path) {
    std::filesystem::path result;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!component.empty()) result /= windows_component(component);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}
}

HfsPlusProbe probe_hfs_plus(const std::filesystem::path& image, const mnc::image::DmgReport& report) {
    HfsPlusProbe result;
    std::vector<std::uint8_t> sectors;
    std::string diagnostic;
    const auto scan_limit = std::min<std::uint64_t>(report.sector_count > 1 ? report.sector_count - 1 : 0, 4096);
    std::uint64_t header_sector = 0;
    bool found = false;
    for (std::uint64_t candidate = 0; candidate < scan_limit; ++candidate) {
        std::vector<std::uint8_t> probe;
        if (!mnc::image::DmgImage::read_sectors(image, report, candidate, 1, probe, diagnostic) || probe.size() < 2) continue;
        const auto signature = be16(probe, 0);
        if (signature == 0x482b || signature == 0x4858) { header_sector = candidate; found = true; break; }
    }
    if (!found || !mnc::image::DmgImage::read_sectors(image, report, header_sector, 2, sectors, diagnostic)) {
        result.diagnostic = "cannot locate HFS+ volume header: " + diagnostic;
        return result;
    }
    const auto signature = be16(sectors, 0);
    result.recognized = true;
    result.hfsx = signature == 0x4858;
    result.volume_start_sector = header_sector >= 2 ? header_sector - 2 : 0;
    result.file_count = be32(sectors, 32);
    result.folder_count = be32(sectors, 36);
    result.block_size = be32(sectors, 40);
    result.total_blocks = be32(sectors, 44);
    result.free_blocks = be32(sectors, 48);
    if (result.block_size == 0 || (result.block_size & (result.block_size - 1)) != 0) {
        result.diagnostic = "HFS+ volume header has an invalid allocation block size";
        result.recognized = false;
        return result;
    }
    // The catalog file fork begins at offset 272 in the HFS+ volume header.
    const auto catalog_start = be32(sectors, 288);
    result.catalog_start_block = catalog_start;
    std::vector<std::uint8_t> catalog_header;
    const auto volume_offset = result.volume_start_sector * 512;
    if (catalog_start && read_bytes(image, report, volume_offset + std::uint64_t(catalog_start) * result.block_size, 512, catalog_header, diagnostic)) {
        if (catalog_header.size() >= 48) {
            // The B-tree header record starts after the 14-byte node descriptor.
            result.catalog_root_node = be32(catalog_header, 16);
            result.catalog_leaf_records = be32(catalog_header, 20);
            result.catalog_first_leaf = be32(catalog_header, 24);
            result.catalog_last_leaf = be32(catalog_header, 28);
            result.catalog_node_size = be16(catalog_header, 32);
            if (result.catalog_node_size == 0 || (result.catalog_node_size & (result.catalog_node_size - 1)) != 0) result.diagnostic = "HFS+ catalog B-tree header is invalid";
        }
    }
    const auto catalog_blocks = be32(sectors, 284);
    if (catalog_start && result.catalog_node_size >= 512 && catalog_blocks) {
        const auto max_nodes = std::min<std::uint64_t>(std::uint64_t(catalog_blocks) * result.block_size / result.catalog_node_size, 65536);
        std::uint64_t node_id = result.catalog_first_leaf;
        for (std::uint64_t visited = 0; node_id != 0 && visited < max_nodes; ++visited) {
            std::vector<std::uint8_t> node;
            if (!read_bytes(image, report, volume_offset + std::uint64_t(catalog_start) * result.block_size + node_id * result.catalog_node_size, result.catalog_node_size, node, diagnostic) || node.size() < 14) break;
            ++result.catalog_leaf_nodes_scanned;
            result.catalog_first_leaf_kind = static_cast<std::int8_t>(node[8]);
            if (static_cast<std::int8_t>(node[8]) != -1) break;
            const auto records = be16(node, 10);
            result.catalog_first_leaf_node_records = records;
            const auto next_leaf = be32(node, 0);
            if (records == 0 || 14u + 2u * (records + 1u) > node.size()) { node_id = next_leaf; continue; }
            const auto offset_table = node.size() - 2u * (records + 1u);
            result.catalog_first_record_offset = be16(node, node.size() - 2u);
            result.catalog_second_record_offset = records > 1 ? be16(node, node.size() - 4u) : be16(node, offset_table);
            for (std::uint16_t record = 0; record < records; ++record) {
                const auto begin = be16(node, node.size() - 2u * (record + 1u));
                const auto end = be16(node, node.size() - 2u * (record + 2u));
                if (begin >= end || end > offset_table || begin + 2 > node.size()) continue;
                const auto key_length = be16(node, begin);
                const auto data = begin + 2u + key_length;
                if (data + 2 > end) continue;
                const auto type = be16(node, data);
                ++result.catalog_leaf_record_types;
                if (type == 1) ++result.catalog_folder_records;
                if (type == 2) ++result.catalog_file_records;
                if (type != 1 && type != 2) continue;
                HfsCatalogEntry entry;
                entry.parent_id = be32(node, begin + 2);
                entry.folder = type == 1;
                entry.node_id = be32(node, data + 8);
                entry.name = hfs_name(node, begin, end);
                if (!entry.folder && data + 104 <= end) {
                    entry.logical_size = (std::uint64_t(be32(node, data + 88)) << 32) | be32(node, data + 92);
                    for (std::size_t extent = 0; extent < 8 && data + 104 + extent * 8 + 8 <= end; ++extent) {
                        const auto start_block = be32(node, data + 104 + extent * 8);
                        const auto block_count = be32(node, data + 108 + extent * 8);
                        if (start_block == 0 || block_count == 0) continue;
                        entry.data_extents.emplace_back(start_block, block_count);
                    }
                }
                if (!entry.name.empty()) result.catalog_entries.push_back(std::move(entry));
            }
            node_id = next_leaf;
        }
    }
    std::unordered_map<std::uint32_t, std::string> paths;
    paths.emplace(1, "");
    for (std::size_t pass = 0; pass < result.catalog_entries.size(); ++pass) {
        bool changed = false;
        for (auto& entry : result.catalog_entries) {
            if (!entry.path.empty() || !paths.count(entry.parent_id)) continue;
            const auto parent = paths[entry.parent_id];
            entry.path = parent.empty() ? entry.name : parent + "/" + entry.name;
            paths[entry.node_id] = entry.path;
            changed = true;
        }
        if (!changed) break;
    }
    result.diagnostic = result.hfsx ? "HFSX volume header recognized" : "HFS+ volume header recognized";
    return result;
}

bool extract_hfs_file(const std::filesystem::path& image, const mnc::image::DmgReport& report,
                      const HfsPlusProbe& volume, const HfsCatalogEntry& entry,
                      std::vector<std::uint8_t>& output, std::string& diagnostic) {
    if (!volume.recognized || entry.folder) { diagnostic = "entry is not an extractable HFS+ file"; return false; }
    if (volume.block_size == 0 || volume.block_size % 512 != 0) { diagnostic = "unsupported HFS+ allocation block size"; return false; }
    if (entry.logical_size > static_cast<std::uint64_t>(SIZE_MAX)) { diagnostic = "HFS+ file is too large for this process"; return false; }
    output.clear(); output.reserve(static_cast<std::size_t>(entry.logical_size));
    std::uint64_t remaining = entry.logical_size;
    for (const auto& extent : entry.data_extents) {
        if (!remaining) break;
        const auto extent_bytes = std::uint64_t(extent.second) * volume.block_size;
        const auto bytes_to_read = std::min(remaining, extent_bytes);
        std::vector<std::uint8_t> bytes;
        if (!read_bytes(image, report, volume.volume_start_sector * 512 + std::uint64_t(extent.first) * volume.block_size, bytes_to_read, bytes, diagnostic)) return false;
        output.insert(output.end(), bytes.begin(), bytes.end());
        remaining -= bytes_to_read;
    }
    if (remaining != 0) { diagnostic = "HFS+ file data fork has insufficient extents"; output.clear(); return false; }
    diagnostic = "HFS+ data fork extracted read-only";
    return true;
}

bool extract_hfs_volume(const std::filesystem::path& image, const mnc::image::DmgReport& report,
                        const HfsPlusProbe& volume, const std::filesystem::path& destination,
                        std::string& diagnostic) {
    if (!volume.recognized) { diagnostic = "HFS+ volume is not recognized"; return false; }
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec) { diagnostic = "cannot create extraction directory: " + ec.message(); return false; }
    std::size_t extracted = 0;
    for (const auto& entry : volume.catalog_entries) {
        if (entry.path.empty()) continue;
        std::filesystem::path relative = windows_relative_path(entry.path);
        if (relative.empty()) continue;
        const auto target = (destination / relative).lexically_normal();
        if (entry.folder) {
            std::filesystem::create_directories(target, ec);
            if (ec) { diagnostic = "cannot create HFS+ directory: " + ec.message(); return false; }
            continue;
        }
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) { diagnostic = "cannot create parent directory: " + ec.message(); return false; }
        std::vector<std::uint8_t> bytes;
        if (!extract_hfs_file(image, report, volume, entry, bytes, diagnostic)) return false;
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) { diagnostic = "cannot create extracted file: " + target.string(); return false; }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) { diagnostic = "cannot write extracted file: " + target.string(); return false; }
        ++extracted;
    }
    diagnostic = "HFS+ volume extracted read-only; files=" + std::to_string(extracted);
    return true;
}
} // namespace mnc::filesystem
