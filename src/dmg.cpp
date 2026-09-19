#include "mnc/dmg.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(MNC_HAS_ZLIB)
#include <zlib.h>
#endif

namespace mnc::image {
namespace {
std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t p) {
    return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p + 1]) << 16) | (std::uint32_t(b[p + 2]) << 8) | b[p + 3];
}
std::uint64_t be64(const std::vector<std::uint8_t>& b, std::size_t p) {
    return (std::uint64_t(be32(b, p)) << 32) | be32(b, p + 4);
}
bool range_valid(std::uint64_t offset, std::uint64_t length, std::uint64_t total) {
    return offset <= total && length <= total - offset;
}
int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
std::vector<std::uint8_t> decode_base64(std::string text) {
    std::vector<std::uint8_t> out;
    int value = 0, bits = -8;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '=') continue;
        const int digit = base64_value(c); if (digit < 0) return {};
        value = (value << 6) | digit; bits += 6;
        if (bits >= 0) { out.push_back(static_cast<std::uint8_t>((value >> bits) & 0xff)); bits -= 8; }
    }
    return out;
}
void parse_blkx_data(const std::string& xml, DmgReport& report) {
    std::size_t cursor = 0;
    while ((cursor = xml.find("<data", cursor)) != std::string::npos) {
        const auto data_tag_end = xml.find('>', cursor);
        const auto begin = data_tag_end == std::string::npos ? std::string::npos : data_tag_end + 1;
        const auto end = begin == std::string::npos ? std::string::npos : xml.find("</data>", begin);
        if (end == std::string::npos) break;
        const auto bytes = decode_base64(xml.substr(begin, end - begin));
        std::uint64_t table_start_sector = 0;
        std::uint64_t table_data_offset = 0;
        if (bytes.size() >= 32 && std::string(reinterpret_cast<const char*>(bytes.data()), 4) == "mish") {
            table_start_sector = be64(bytes, 8);
            table_data_offset = be64(bytes, 24);
        }
        report.diagnostics.push_back("blkx table Start Sector=" + std::to_string(table_start_sector));
        std::size_t table_start = 0;
        if (bytes.size() >= 4 && std::string(reinterpret_cast<const char*>(bytes.data()), 4) == "mish") table_start = 204;
        if (bytes.size() >= table_start + 40) {
            for (std::size_t p = table_start; p + 40 <= bytes.size(); p += 40) {
                DmgExtent extent;
                extent.type = be32(bytes, p);
                extent.sector_start = table_start_sector + be64(bytes, p + 8);
                extent.sector_count = be64(bytes, p + 16);
                extent.data_offset = table_data_offset + be64(bytes, p + 24);
                extent.data_length = be64(bytes, p + 32);
                if (extent.type == 0 || extent.type == 1 || extent.type == 2 || extent.type == 0x80000005u || extent.type == 0x80000006u || extent.type == 0x80000007u || extent.type == 0xffffffffu) report.extents.push_back(extent);
            }
        }
        cursor = end + 7;
    }
}
}

DmgReport DmgImage::inspect(const std::filesystem::path& path) {
    DmgReport report;
    std::error_code ec;
    report.image_size = std::filesystem::file_size(path, ec);
    if (ec) throw std::runtime_error("cannot read DMG: " + path.string());
    if (report.image_size < 512) { report.diagnostics.push_back("file is too small to contain a 512-byte UDIF trailer"); return report; }

    std::ifstream file(path, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(report.image_size - 512), std::ios::beg);
    std::vector<std::uint8_t> trailer(512);
    file.read(reinterpret_cast<char*>(trailer.data()), static_cast<std::streamsize>(trailer.size()));
    if (file.gcount() != static_cast<std::streamsize>(trailer.size())) throw std::runtime_error("cannot read UDIF trailer");
    if (std::string(reinterpret_cast<const char*>(trailer.data()), 4) != "koly") { report.format = "unknown disk image"; report.diagnostics.push_back("UDIF signature not found"); return report; }

    report.is_udif = true; report.format = "Apple UDIF/DMG";
    report.version = be32(trailer, 4); report.header_size = be32(trailer, 8); report.flags = be32(trailer, 12);
    report.data_fork_offset = be64(trailer, 24); report.data_fork_length = be64(trailer, 32);
    report.resource_fork_offset = be64(trailer, 40); report.resource_fork_length = be64(trailer, 48);
    report.xml_offset = be64(trailer, 216); report.xml_length = be64(trailer, 224); report.sector_count = be64(trailer, 492);
    report.diagnostics.push_back("UDIF trailer parsed");
    std::ostringstream metadata; metadata << "UDIF v" << report.version << ", data fork offset=" << report.data_fork_offset << ", length=" << report.data_fork_length << ", XML offset=" << report.xml_offset << ", XML length=" << report.xml_length << ", sectors=" << report.sector_count; report.diagnostics.push_back(metadata.str());
    if (report.header_size != 512) report.diagnostics.push_back("unexpected UDIF header size");
    if (report.version == 0 || report.version > 4) report.diagnostics.push_back("unknown UDIF version");
    if (!range_valid(report.data_fork_offset, report.data_fork_length, report.image_size)) report.diagnostics.push_back("data fork range exceeds image");
    if (report.resource_fork_length && !range_valid(report.resource_fork_offset, report.resource_fork_length, report.image_size)) report.diagnostics.push_back("resource fork range exceeds image");
    if (report.xml_length && !range_valid(report.xml_offset, report.xml_length, report.image_size)) report.diagnostics.push_back("XML property-list range exceeds image");
    if (!report.xml_length) report.diagnostics.push_back("UDIF XML property list is absent");

    if (report.xml_length && range_valid(report.xml_offset, report.xml_length, report.image_size) && report.xml_length <= 64u * 1024u * 1024u) {
        file.seekg(static_cast<std::streamoff>(report.xml_offset), std::ios::beg);
        std::string xml(static_cast<std::size_t>(report.xml_length), '\0');
        file.read(xml.data(), static_cast<std::streamsize>(xml.size()));
        if (file.gcount() == static_cast<std::streamsize>(xml.size())) {
            report.has_property_list = xml.find("plist") != std::string::npos;
            report.has_block_map = xml.find("blkx") != std::string::npos || xml.find("BlockMap") != std::string::npos;
            report.diagnostics.push_back(report.has_property_list ? "UDIF XML property list read" : "UDIF XML region does not contain a recognizable plist marker");
            report.diagnostics.push_back(report.has_block_map ? "UDIF block map key detected" : "UDIF block map key not detected");
            if (report.has_block_map) {
                parse_blkx_data(xml, report);
                report.diagnostics.push_back("decoded " + std::to_string(report.extents.size()) + " candidate block extents");
                const auto preview = std::min<std::size_t>(report.extents.size(), 6);
                for (std::size_t i = 0; i < preview; ++i) {
                    const auto& extent = report.extents[i];
                    std::ostringstream line;
                    line << "extent[" << i << "] type=0x" << std::hex << extent.type << std::dec
                         << " sectors=" << extent.sector_start << "+" << extent.sector_count
                         << " data=" << extent.data_offset << "+" << extent.data_length;
                    report.diagnostics.push_back(line.str());
                }
            }
        } else report.diagnostics.push_back("unable to read complete UDIF XML property list");
    } else if (report.xml_length > 64u * 1024u * 1024u) report.diagnostics.push_back("UDIF XML property list exceeds safe inspection limit");
    report.diagnostics.push_back("compressed sector decoding and HFS+/APFS filesystem access remain separate subsystems");
    return report;
}

bool DmgImage::read_sectors(const std::filesystem::path& path, const DmgReport& report,
                            std::uint64_t sector_start, std::uint64_t sector_count,
                            std::vector<std::uint8_t>& output, std::string& diagnostic) {
    constexpr std::uint64_t sector_size = 512;
    if (!report.is_udif || sector_count > (static_cast<std::uint64_t>(SIZE_MAX) / sector_size)) {
        diagnostic = "invalid UDIF report or requested sector count";
        return false;
    }
    if (sector_start > report.sector_count || sector_count > report.sector_count - sector_start) {
        diagnostic = "requested logical sectors exceed the UDIF image";
        return false;
    }
    output.assign(static_cast<std::size_t>(sector_count * sector_size), 0);
    std::ifstream file(path, std::ios::binary);
    if (!file) { diagnostic = "cannot open DMG data fork"; return false; }
    bool covered = false;
    for (const auto& extent : report.extents) {
        const auto request_end = sector_start + sector_count;
        const auto extent_end = extent.sector_start + extent.sector_count;
        if (sector_start >= extent_end || request_end <= extent.sector_start) continue;
        const auto overlap_start = std::max(sector_start, extent.sector_start);
        const auto overlap_end = std::min(request_end, extent_end);
        const auto overlap_sectors = overlap_end - overlap_start;
        const auto output_offset = (overlap_start - sector_start) * sector_size;
        if (extent.type == 0 || extent.type == 1) {
            const auto source_offset = report.data_fork_offset + extent.data_offset + (overlap_start - extent.sector_start) * sector_size;
            const auto source_length = overlap_sectors * sector_size;
            if (!range_valid(source_offset, source_length, report.image_size)) { diagnostic = "raw extent exceeds DMG file"; return false; }
            file.seekg(static_cast<std::streamoff>(source_offset), std::ios::beg);
            file.read(reinterpret_cast<char*>(output.data() + output_offset), static_cast<std::streamsize>(source_length));
            if (file.gcount() != static_cast<std::streamsize>(source_length)) { diagnostic = "short read from raw DMG extent"; return false; }
            covered = true;
        } else if (extent.type == 2) {
            covered = true;
        } else if (extent.type == 0x80000005u || extent.type == 0x80000006u || extent.type == 0x80000007u) {
            if (extent.type != 0x80000005u) { diagnostic = "requested sector uses bzip2/LZFSE UDIF compression; only zlib is enabled"; return false; }
#if defined(MNC_HAS_ZLIB)
            const auto compressed_offset = report.data_fork_offset + extent.data_offset;
            if (!range_valid(compressed_offset, extent.data_length, report.image_size)) { diagnostic = "compressed extent exceeds DMG file"; return false; }
            std::vector<std::uint8_t> compressed(static_cast<std::size_t>(extent.data_length));
            file.seekg(static_cast<std::streamoff>(compressed_offset), std::ios::beg);
            file.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
            if (file.gcount() != static_cast<std::streamsize>(compressed.size())) { diagnostic = "short read from zlib extent"; return false; }
            const auto expected_size = extent.sector_count * sector_size;
            std::vector<std::uint8_t> decoded(static_cast<std::size_t>(expected_size));
            auto decoded_size = static_cast<uLongf>(decoded.size());
            const auto result = uncompress(decoded.data(), &decoded_size, compressed.data(), static_cast<uLong>(compressed.size()));
            if (result != Z_OK || decoded_size < (overlap_end - extent.sector_start) * sector_size) { diagnostic = "zlib UDIF extent decompression failed or returned too little data"; return false; }
            const auto decoded_offset = (overlap_start - extent.sector_start) * sector_size;
            std::copy_n(decoded.begin() + static_cast<std::ptrdiff_t>(decoded_offset), static_cast<std::size_t>(overlap_sectors * sector_size), output.begin() + static_cast<std::ptrdiff_t>(output_offset));
            covered = true;
#else
            diagnostic = "zlib UDIF extent encountered, but Mac&Cheese was built without zlib support";
            return false;
#endif
        }
    }
    if (!covered) { diagnostic = "no supported extent covers the requested logical sectors"; return false; }
    diagnostic = "logical sectors read from raw/zero UDIF extents";
    return true;
}
} // namespace mnc::image
