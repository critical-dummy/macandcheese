#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mnc {

enum class CpuType : std::uint32_t { X86_64 = 0x01000007, ARM64 = 0x0100000c, Unknown = 0 };

enum class BinaryKind { Thin64, Fat, Unsupported };

struct SegmentInfo {
    std::string name;
    std::uint64_t vm_address{};
    std::uint64_t vm_size{};
    std::uint64_t file_offset{};
    std::uint64_t file_size{};
    std::uint32_t protections{};
};

struct SymbolInfo {
    std::string name;
    std::uint64_t value{};
    std::uint8_t type{};
    std::uint8_t section{};
    std::uint16_t description{};
};

struct SectionInfo {
    std::string name;
    std::string segment_name;
    std::uint64_t address{};
    std::uint64_t size{};
    std::uint32_t file_offset{};
    std::uint32_t flags{};
};

struct MachOImage {
    BinaryKind kind{BinaryKind::Unsupported};
    CpuType cpu{CpuType::Unknown};
    std::uint32_t file_type{};
    std::uint32_t flags{};
    std::uint64_t entry_point{};
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
    std::uint32_t symbol_offset{};
    std::uint32_t symbol_count{};
    std::uint32_t string_offset{};
    std::uint32_t string_size{};
    std::vector<SegmentInfo> segments;
    std::vector<std::string> dependencies;
    std::vector<std::string> rpaths;
    std::vector<std::string> symbols;
    std::vector<SymbolInfo> symbol_table;
    std::vector<SectionInfo> sections;
    std::vector<std::string> diagnostics;
};

class MachOParser {
public:
    static MachOImage parse_file(const std::filesystem::path& path);
    static MachOImage parse_bytes(const std::vector<std::uint8_t>& bytes);
};

std::string to_string(CpuType cpu);
std::string to_string(BinaryKind kind);

} // namespace mnc
