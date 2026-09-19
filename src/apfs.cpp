#include "mnc/apfs.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace mnc::filesystem {
namespace {
std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t p) {
    return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8) | (std::uint32_t(b[p + 2]) << 16) | (std::uint32_t(b[p + 3]) << 24);
}
std::uint64_t le64(const std::vector<std::uint8_t>& b, std::size_t p) {
    return std::uint64_t(le32(b, p)) | (std::uint64_t(le32(b, p + 4)) << 32);
}
bool has(const std::vector<std::uint8_t>& b, std::size_t p, const char* text) {
    for (std::size_t i = 0; text[i]; ++i) if (p + i >= b.size() || b[p + i] != static_cast<std::uint8_t>(text[i])) return false;
    return true;
}
std::string hex_guid(const std::vector<std::uint8_t>& b, std::size_t p) {
    std::ostringstream out;
    for (std::size_t i = 0; i < 16; ++i) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(b[p + i]);
    return out.str();
}
}

ApfsProbe probe_apfs(const std::filesystem::path& image, const mnc::image::DmgReport& report) {
    ApfsProbe result;
    std::string diagnostic;
    const auto scan_limit = std::min<std::uint64_t>(report.sector_count, 32768);
    std::uint64_t gpt_sector = 0;
    std::vector<std::uint8_t> header;
    bool found_gpt = false;
    for (std::uint64_t sector = 0; sector < scan_limit; ++sector) {
        std::vector<std::uint8_t> bytes;
        if (!mnc::image::DmgImage::read_sectors(image, report, sector, 1, bytes, diagnostic)) continue;
        if (bytes.size() >= 8 && has(bytes, 0, "EFI PART")) { gpt_sector = sector; header = std::move(bytes); found_gpt = true; break; }
    }
    if (found_gpt) {
        result.gpt_detected = true;
        const auto entry_lba = le64(header, 72);
        const auto entry_count = std::min<std::uint64_t>(le32(header, 80), 4096);
        const auto entry_size = le32(header, 84);
        if (entry_size >= 128 && entry_size <= 4096) {
            for (std::uint64_t i = 0; i < entry_count; ++i) {
                const auto byte_offset = entry_lba * 512 + i * entry_size;
                const auto lba = gpt_sector + byte_offset / 512;
                const auto within = byte_offset % 512;
                std::vector<std::uint8_t> entry_bytes;
                const auto sectors_needed = (within + entry_size + 511) / 512;
                if (!mnc::image::DmgImage::read_sectors(image, report, lba, sectors_needed, entry_bytes, diagnostic) || within + entry_size > entry_bytes.size()) continue;
                bool nonzero = false;
                for (std::size_t j = 0; j < 16; ++j) if (entry_bytes[within + j] != 0) { nonzero = true; break; }
                if (!nonzero) continue;
                const auto start = le64(entry_bytes, within + 32);
                const auto end = le64(entry_bytes, within + 40);
                if (start > end || start >= report.sector_count) continue;
                result.gpt_partition_start_sector = start;
                result.gpt_partition_end_sector = std::min(end, report.sector_count - 1);
                result.partition_type = hex_guid(entry_bytes, within);
                std::vector<std::uint8_t> first;
                if (mnc::image::DmgImage::read_sectors(image, report, start, 1, first, diagnostic) && first.size() >= 40 && has(first, 32, "NXSB")) {
                    result.recognized = true;
                    result.apfs_start_sector = start;
                    result.block_size = le32(first, 36);
                    result.diagnostic = "GPT APFS partition and NXSB container superblock recognized";
                    return result;
                }
            }
        }
        result.diagnostic = "GPT partition map recognized; no APFS NXSB at partition starts";
        return result;
    }
    for (std::uint64_t sector = 0; sector < scan_limit; ++sector) {
        std::vector<std::uint8_t> bytes;
        if (!mnc::image::DmgImage::read_sectors(image, report, sector, 1, bytes, diagnostic)) continue;
        if (bytes.size() >= 40 && has(bytes, 32, "NXSB")) {
            result.recognized = true;
            result.apfs_start_sector = sector;
            result.block_size = le32(bytes, 36);
            result.diagnostic = "APFS NXSB container superblock recognized without GPT";
            return result;
        }
    }
    result.diagnostic = "APFS NXSB and GPT signatures not found";
    return result;
}
} // namespace mnc::filesystem
