#include "mnc/macho.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace mnc {
namespace {
constexpr std::uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr std::uint32_t FAT_MAGIC = 0xcafebabe;
constexpr std::uint32_t FAT_CIGAM = 0xbebafeca;
constexpr std::uint32_t LC_SEGMENT_64 = 0x19;
constexpr std::uint32_t LC_LOAD_DYLIB = 0xc;
constexpr std::uint32_t LC_RPATH = 0x8000001c;
constexpr std::uint32_t LC_SYMTAB = 0x2;
constexpr std::uint32_t LC_MAIN = 0x80000028;
constexpr std::uint32_t LC_DYLD_INFO = 0x22;
constexpr std::uint32_t LC_DYLD_INFO_ONLY = 0x80000022;
constexpr std::uint32_t LC_DYLD_CHAINED_FIXUPS = 0x80000034;
constexpr std::uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr std::uint32_t CPU_TYPE_ARM64 = 0x0100000c;

std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("truncated 32-bit field");
    return b[p] | (std::uint32_t(b[p+1]) << 8) | (std::uint32_t(b[p+2]) << 16) | (std::uint32_t(b[p+3]) << 24);
}
std::uint64_t u64(const std::vector<std::uint8_t>& b, std::size_t p) {
    return std::uint64_t(u32(b,p)) | (std::uint64_t(u32(b,p+4)) << 32);
}
std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("truncated big-endian field");
    return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p+1]) << 16) | (std::uint32_t(b[p+2]) << 8) | std::uint32_t(b[p+3]);
}
std::string cstr(const std::vector<std::uint8_t>& b, std::size_t p, std::size_t max) {
    if (p >= b.size()) return {};
    const auto end = std::min(b.size(), p + max);
    std::size_t q = p; while (q < end && b[q]) ++q;
    return std::string(reinterpret_cast<const char*>(b.data()+p), q-p);
}
void require_range(const std::vector<std::uint8_t>& b, std::size_t p, std::size_t n) {
    if (p > b.size() || n > b.size()-p) throw std::runtime_error("truncated Mach-O load command");
}
MachOImage parse_thin(const std::vector<std::uint8_t>& b, std::size_t base) {
    require_range(b, base, 32);
    if (u32(b,base) != MH_MAGIC_64) throw std::runtime_error("unsupported Mach-O magic (expected 64-bit little-endian)");
    MachOImage out; out.kind = BinaryKind::Thin64; out.file_base_offset = base;
    const auto cpu = u32(b,base+4); out.cpu = cpu == CPU_TYPE_X86_64 ? CpuType::X86_64 : cpu == CPU_TYPE_ARM64 ? CpuType::ARM64 : CpuType::Unknown;
    out.file_type = u32(b,base+12); const auto ncmds = u32(b,base+16); const auto sizeofcmds = u32(b,base+20); out.flags = u32(b,base+24);
    require_range(b, base+32, sizeofcmds);
    std::size_t p = base + 32;
    for (std::uint32_t i=0; i<ncmds; ++i) {
        require_range(b,p,8); const auto cmd=u32(b,p), size=u32(b,p+4);
        if (size < 8) throw std::runtime_error("invalid load command size"); require_range(b,p,size);
        if (cmd == LC_SEGMENT_64 && size >= 72) {
            SegmentInfo s; s.name=cstr(b,p+8,16); s.vm_address=u64(b,p+24); s.vm_size=u64(b,p+32); s.file_offset=u64(b,p+40); s.file_size=u64(b,p+48); s.protections=u32(b,p+60); out.segments.push_back(std::move(s));
            const auto section_count = u32(b, p + 64);
            if (section_count > (size - 72) / 80) throw std::runtime_error("invalid section_64 count");
            for (std::uint32_t section_index = 0; section_index < section_count; ++section_index) {
                const auto section_pos = p + 72 + std::size_t(section_index) * 80;
                SectionInfo section; section.name=cstr(b, section_pos, 16); section.segment_name=cstr(b, section_pos + 16, 16); section.address=u64(b, section_pos + 32); section.size=u64(b, section_pos + 40); section.file_offset=u32(b, section_pos + 48); section.flags=u32(b, section_pos + 64); out.sections.push_back(std::move(section));
            }
        } else if ((cmd == LC_LOAD_DYLIB || cmd == LC_RPATH) && size >= 16) {
            const auto off=u32(b,p+8); if (off < size) { auto value=cstr(b,p+off,size-off); if(cmd==LC_RPATH) out.rpaths.push_back(value); else out.dependencies.push_back(value); }
        } else if (cmd == LC_MAIN && size >= 24) out.entry_point=u64(b,p+8);
        else if ((cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) && size >= 48) { out.rebase_offset=u32(b,p+8); out.rebase_size=u32(b,p+12); out.bind_offset=u32(b,p+16); out.bind_size=u32(b,p+20); out.weak_bind_offset=u32(b,p+24); out.weak_bind_size=u32(b,p+28); out.lazy_bind_offset=u32(b,p+32); out.lazy_bind_size=u32(b,p+36); }
        else if (cmd == LC_DYLD_CHAINED_FIXUPS && size >= 16) { out.chained_fixups_offset=u32(b,p+8); out.chained_fixups_size=u32(b,p+12); }
        else if (cmd == LC_SYMTAB && size >= 24) {
            out.symbol_offset=u32(b,p+8); out.symbol_count=u32(b,p+12); out.string_offset=u32(b,p+16); out.string_size=u32(b,p+20);
            require_range(b, base + out.string_offset, out.string_size);
            if (out.symbol_count > (std::numeric_limits<std::size_t>::max() / 16)) throw std::runtime_error("nlist_64 symbol table is too large");
            require_range(b, base + out.symbol_offset, std::size_t(out.symbol_count) * 16);
            for (std::uint32_t symbol_index = 0; symbol_index < out.symbol_count; ++symbol_index) {
                const auto symbol_pos = base + out.symbol_offset + std::size_t(symbol_index) * 16;
                const auto string_index = u32(b, symbol_pos);
                if (string_index >= out.string_size) continue;
                SymbolInfo info; info.name=cstr(b, base + out.string_offset + string_index, out.string_size - string_index); info.type=b[symbol_pos+4]; info.section=b[symbol_pos+5]; info.description=std::uint16_t(b[symbol_pos+6]) | (std::uint16_t(b[symbol_pos+7]) << 8); info.value=u64(b, symbol_pos+8);
                out.symbols.push_back(info.name); out.symbol_table.push_back(std::move(info));
            }
        }
        p += size;
    }
    out.diagnostics.push_back("parsed Mach-O load commands without mapping or execution");
    return out;
}
} // namespace

MachOImage MachOParser::parse_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary); if(!f) throw std::runtime_error("cannot open: " + path.string());
    return parse_bytes(std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), {}));
}
MachOImage MachOParser::parse_bytes(const std::vector<std::uint8_t>& b) {
    if (b.size() < 4) throw std::runtime_error("file is too small");
    const auto magic=u32(b,0);
    if (magic == MH_MAGIC_64) return parse_thin(b,0);
    if (magic == FAT_MAGIC || magic == FAT_CIGAM) {
        if (b.size()<8) throw std::runtime_error("truncated FAT header");
        const bool big_endian = magic == FAT_CIGAM;
        const auto read32 = [&](std::size_t p) { return big_endian ? be32(b, p) : u32(b, p); };
        const auto n=read32(4); if(n>128 || 8ull+n*20>b.size()) throw std::runtime_error("invalid FAT architecture table");
        MachOImage out; out.kind=BinaryKind::Fat; out.diagnostics.push_back("universal binary detected; selected architecture is not implicit");
        for(std::uint32_t i=0;i<n;++i) { const auto off=read32(8+i*20+8); if(off<b.size() && u32(b,off)==MH_MAGIC_64) { auto selected=parse_thin(b,off); selected.kind=BinaryKind::Fat; selected.diagnostics.insert(selected.diagnostics.begin(), "selected first 64-bit slice"); return selected; } }
        throw std::runtime_error("FAT binary has no supported 64-bit little-endian slice");
    }
    throw std::runtime_error("unsupported file: not a 64-bit Mach-O or FAT binary");
}
std::string to_string(CpuType c) { return c==CpuType::X86_64?"x86_64":c==CpuType::ARM64?"arm64":"unknown"; }
std::string to_string(BinaryKind k) { return k==BinaryKind::Thin64?"thin-64":k==BinaryKind::Fat?"fat/universal":"unsupported"; }
} // namespace mnc
