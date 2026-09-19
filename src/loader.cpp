#include "mnc/loader.hpp"
#include "mnc/dylib.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace mnc::loader {
namespace {
bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::filesystem::path resolve_candidate(const std::filesystem::path& executable, const std::string& dependency, const std::vector<std::string>& rpaths) {
    if (dependency.empty()) return {};
    if (dependency[0] == '/') return std::filesystem::path(dependency);
    if (starts_with(dependency, "@loader_path/")) return executable.parent_path() / dependency.substr(13);
    if (starts_with(dependency, "@executable_path/")) return executable.parent_path() / dependency.substr(18);
    if (starts_with(dependency, "@rpath/")) {
        for (const auto& rpath : rpaths) {
            std::filesystem::path base = rpath;
            if (rpath == "@loader_path" || rpath == "@executable_path") base = executable.parent_path();
            if (!base.empty()) {
                const auto candidate = base / dependency.substr(7);
                if (std::filesystem::exists(candidate)) return candidate;
            }
        }
    }
    return {};
}

#ifdef _WIN32
DWORD windows_protection(std::uint32_t protections) {
    const bool read = (protections & 1u) != 0;
    const bool write = (protections & 2u) != 0;
    const bool execute = (protections & 4u) != 0;
    if (execute) {
        if (write) return PAGE_EXECUTE_READWRITE;
        if (read) return PAGE_EXECUTE_READ;
        return PAGE_EXECUTE;
    }
    if (write) return read ? PAGE_READWRITE : PAGE_WRITECOPY;
    return read ? PAGE_READONLY : PAGE_NOACCESS;
}

bool read_uleb(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, std::uint64_t& value) {
    value = 0;
    unsigned shift = 0;
    while (cursor < bytes.size() && shift < 64) {
        const auto byte = bytes[cursor++];
        value |= std::uint64_t(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) return true;
        shift += 7;
    }
    return false;
}

bool apply_rebases(void* allocation, const LoadPlan& plan, const std::vector<std::uint8_t>& opcodes, std::string& error) {
    if (opcodes.empty()) return true;
    constexpr std::uint8_t REBASE_TYPE_POINTER = 1;
    const auto base = reinterpret_cast<std::uintptr_t>(allocation);
    const auto slide = static_cast<std::int64_t>(static_cast<std::uint64_t>(base) - plan.image_min_address);
    std::uint8_t type = REBASE_TYPE_POINTER;
    std::size_t cursor = 0;
    std::size_t segment_index = 0;
    std::uint64_t segment_offset = 0;
    auto rebase_one = [&]() -> bool {
        if (type != REBASE_TYPE_POINTER) { error = "unsupported dyld rebase type"; return false; }
        const SegmentMapping* segment = nullptr;
        for (const auto& candidate : plan.segments) if (candidate.command_index == segment_index) { segment = &candidate; break; }
        if (!segment || segment_offset > segment->vm_size || segment->vm_size - segment_offset < sizeof(std::uint64_t)) { error = "dyld rebase location is outside a mapped segment"; return false; }
        auto* location = reinterpret_cast<std::uint64_t*>(base + (segment->vm_address - plan.image_min_address) + segment_offset);
        *location = static_cast<std::uint64_t>(static_cast<std::int64_t>(*location) + slide);
        return true;
    };
    while (cursor < opcodes.size()) {
        const auto opcode = opcodes[cursor++];
        const auto imm = opcode & 0x0f;
        switch (opcode & 0xf0) {
        case 0x00: if (opcode == 0) return true; error = "unknown dyld rebase DONE opcode"; return false;
        case 0x10: type = imm; break;
        case 0x20: { segment_index = imm; std::uint64_t offset = 0; if (!read_uleb(opcodes, cursor, offset)) { error = "truncated dyld rebase segment offset"; return false; } segment_offset = offset; break; }
        case 0x30: { std::uint64_t delta = 0; if (!read_uleb(opcodes, cursor, delta)) { error = "truncated dyld rebase address delta"; return false; } segment_offset += delta; break; }
        case 0x50: for (std::uint8_t i = 0; i < imm; ++i) { if (!rebase_one()) return false; segment_offset += 8; } break;
        case 0x60: { std::uint64_t count = 0; if (!read_uleb(opcodes, cursor, count)) { error = "truncated dyld rebase count"; return false; } for (std::uint64_t i = 0; i < count; ++i) { if (!rebase_one()) return false; segment_offset += 8; } break; }
        case 0x70: if (!rebase_one()) return false; { std::uint64_t delta = 0; if (!read_uleb(opcodes, cursor, delta)) { error = "truncated dyld rebase delta"; return false; } segment_offset += 8 + delta; } break;
        case 0x80: { std::uint64_t count = 0, skip = 0; if (!read_uleb(opcodes, cursor, count) || !read_uleb(opcodes, cursor, skip)) { error = "truncated dyld rebase skip"; return false; } for (std::uint64_t i = 0; i < count; ++i) { if (!rebase_one()) return false; segment_offset += 8 + skip; } break; }
        case 0xa0: if (!rebase_one()) return false; segment_offset += 8 + std::uint64_t(imm) * 8; break;
        case 0xb0: if (!rebase_one()) return false; segment_offset += 8 + std::uint64_t(imm) * 8; break;
        default: error = "unsupported dyld rebase opcode"; return false;
        }
    }
    error = "dyld rebase stream ended without DONE opcode";
    return false;
}

bool read_sleb(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, std::int64_t& value) {
    std::uint64_t result = 0; unsigned shift = 0; std::uint8_t byte = 0;
    do { if (cursor >= bytes.size() || shift >= 64) return false; byte = bytes[cursor++]; result |= std::uint64_t(byte & 0x7f) << shift; shift += 7; } while (byte & 0x80);
    if (shift < 64 && (byte & 0x40)) result |= (~std::uint64_t(0)) << shift;
    value = static_cast<std::int64_t>(result); return true;
}

bool collect_bind_stream(const std::string& stream_name, const std::vector<std::uint8_t>& bytes, std::vector<BindReference>& output, std::string& error) {
    std::size_t cursor = 0, segment_index = 0; std::uint64_t segment_offset = 0; std::int64_t ordinal = 0, addend = 0; std::uint8_t type = 1;
    std::string symbol;
    auto collect = [&]() { output.push_back({stream_name, symbol, ordinal, segment_index, segment_offset, addend}); };
    while (cursor < bytes.size()) {
        const auto opcode = bytes[cursor++]; const auto imm = opcode & 0x0f;
        switch (opcode & 0xf0) {
        case 0x00: break;
        case 0x10: ordinal = imm; break;
        case 0x20: { std::uint64_t v = 0; if (!read_uleb(bytes, cursor, v)) { error = "truncated bind library ordinal"; return false; } ordinal = static_cast<std::int64_t>(v); break; }
        case 0x30: ordinal = static_cast<std::int8_t>(imm | ((imm & 8) ? 0xf0 : 0)); break;
        case 0x40: { const auto start = cursor; while (cursor < bytes.size() && bytes[cursor] != 0) ++cursor; if (cursor >= bytes.size()) { error = "unterminated bind symbol"; return false; } symbol.assign(reinterpret_cast<const char*>(bytes.data() + start), cursor - start); ++cursor; break; }
        case 0x50: type = imm; break;
        case 0x60: if (!read_sleb(bytes, cursor, addend)) { error = "truncated bind addend"; return false; } break;
        case 0x70: { segment_index = imm; if (!read_uleb(bytes, cursor, segment_offset)) { error = "truncated bind segment offset"; return false; } break; }
        case 0x80: { std::uint64_t delta = 0; if (!read_uleb(bytes, cursor, delta)) { error = "truncated bind address delta"; return false; } segment_offset += delta; break; }
        case 0x90: collect(); segment_offset += 8; break;
        case 0xa0: { collect(); std::uint64_t delta = 0; if (!read_uleb(bytes, cursor, delta)) { error = "truncated bind address delta"; return false; } segment_offset += 8 + delta; break; }
        case 0xb0: collect(); segment_offset += 8 + std::uint64_t(imm) * 8; break;
        case 0xc0: { std::uint64_t count = 0, skip = 0; if (!read_uleb(bytes, cursor, count) || !read_uleb(bytes, cursor, skip)) { error = "truncated bind repeat"; return false; } for (std::uint64_t i = 0; i < count; ++i) { collect(); segment_offset += 8 + skip; } break; }
        default: error = "unsupported dyld bind opcode"; return false;
        }
        if (type == 0) { error = "unsupported dyld bind type"; return false; }
    }
    return true;
}

bool apply_chained_rebases(void* allocation, const LoadPlan& plan, const std::vector<std::uint8_t>& bytes, std::string& error) {
    if (bytes.size() < 28) { error = "chained fixups header is truncated"; return false; }
    const auto read16 = [&](std::size_t p) { return std::uint16_t(bytes[p]) | (std::uint16_t(bytes[p + 1]) << 8); };
    const auto read32 = [&](std::size_t p) { return std::uint32_t(bytes[p]) | (std::uint32_t(bytes[p + 1]) << 8) | (std::uint32_t(bytes[p + 2]) << 16) | (std::uint32_t(bytes[p + 3]) << 24); };
    const auto read64 = [&](std::size_t p) { return std::uint64_t(read32(p)) | (std::uint64_t(read32(p + 4)) << 32); };
    const auto starts_offset = read32(4);
    if (starts_offset >= bytes.size() || starts_offset + 4 > bytes.size()) { error = "chained starts offset is outside the fixups blob"; return false; }
    const auto segment_count = read32(starts_offset);
    if (segment_count > 4096 || starts_offset + 4 + std::size_t(segment_count) * 4 > bytes.size()) { error = "invalid chained segment count"; return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(allocation);
    const auto slide = static_cast<std::int64_t>(static_cast<std::uint64_t>(base) - plan.image_min_address);
    for (std::uint32_t segment_index = 0; segment_index < segment_count; ++segment_index) {
        const auto info_offset = read32(starts_offset + 4 + std::size_t(segment_index) * 4);
        if (info_offset == 0) continue;
        if (info_offset + 22 > bytes.size()) { error = "chained segment starts record is truncated"; return false; }
        const auto size = read32(info_offset);
        const auto page_size = read16(info_offset + 4);
        const auto pointer_format = read16(info_offset + 6);
        const auto segment_offset = read64(info_offset + 8);
        const auto page_count = read16(info_offset + 20);
        if (size < 22 || info_offset + size > bytes.size() || info_offset + 22 + std::size_t(page_count) * 2 > bytes.size()) { error = "invalid chained segment starts record"; return false; }
        if (pointer_format != 2 && pointer_format != 6) { error = "unsupported chained pointer format for x86_64: " + std::to_string(pointer_format); return false; }
        const SegmentMapping* segment = nullptr;
        for (const auto& candidate : plan.segments) if (candidate.command_index == segment_index) { segment = &candidate; break; }
        if (!segment) { error = "chained fixups references an unmapped segment"; return false; }
        for (std::uint16_t page = 0; page < page_count; ++page) {
            const auto page_start = read16(info_offset + 22 + std::size_t(page) * 2);
            if (page_start == 0xffff) continue;
            if (page_start >= page_size) { error = "chained page start exceeds page size"; return false; }
            auto location = base + (segment->vm_address - plan.image_min_address) + std::uint64_t(page) * page_size + page_start;
            const auto segment_end = base + (segment->vm_address - plan.image_min_address) + segment->vm_size;
            if (location < base || location + sizeof(std::uint64_t) > segment_end) { error = "chained pointer location is outside segment"; return false; }
            for (;;) {
                const auto raw = *reinterpret_cast<std::uint64_t*>(location);
                if ((raw >> 63) != 0) { error = "chained bind pointer encountered; chained bind resolver is not implemented"; return false; }
                const auto target = raw & 0x0000000fffffffffULL;
                const auto high8 = (raw >> 36) & 0xffULL;
                const auto value = pointer_format == 6 ? static_cast<std::uint64_t>(base) + target : (target | (high8 << 56));
                *reinterpret_cast<std::uint64_t*>(location) = pointer_format == 6 ? value : static_cast<std::uint64_t>(static_cast<std::int64_t>(value) + slide);
                const auto next = (raw >> 51) & 0x7ffULL;
                if (next == 0) break;
                location += next * 4;
                if (location + sizeof(std::uint64_t) > segment_end) { error = "chained pointer next stride exits segment"; return false; }
            }
        }
    }
    return true;
}
#endif
} // namespace

LoadPlan make_load_plan(const std::filesystem::path& executable, const MachOImage& image) {
    LoadPlan plan;
    if (image.segments.empty()) {
        plan.diagnostics.push_back("Mach-O contains no segments");
        return plan;
    }
    std::uint64_t min_address = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_address = 0;
    for (std::size_t command_index = 0; command_index < image.segments.size(); ++command_index) {
        const auto& segment = image.segments[command_index];
        if (segment.vm_size == 0 || segment.name == "__PAGEZERO") continue;
        if (segment.file_size > segment.vm_size) {
            plan.diagnostics.push_back("segment file size exceeds virtual size: " + segment.name);
            return plan;
        }
        if (segment.vm_address > std::numeric_limits<std::uint64_t>::max() - segment.vm_size) {
            plan.diagnostics.push_back("segment address overflow: " + segment.name);
            return plan;
        }
        min_address = std::min(min_address, segment.vm_address);
        max_address = std::max(max_address, segment.vm_address + segment.vm_size);
        plan.segments.push_back({command_index, segment.name, segment.vm_address, segment.vm_size, segment.file_offset, segment.file_size, segment.protections});
    }
    if (min_address == std::numeric_limits<std::uint64_t>::max() || max_address <= min_address) {
        plan.diagnostics.push_back("Mach-O segment address range is empty");
        return plan;
    }
    plan.image_min_address = min_address;
    plan.image_max_address = max_address;
    plan.file_base_offset = image.file_base_offset;
    plan.rebase_offset = image.rebase_offset;
    plan.rebase_size = image.rebase_size;
    plan.bind_offset = image.bind_offset;
    plan.bind_size = image.bind_size;
    plan.weak_bind_offset = image.weak_bind_offset;
    plan.weak_bind_size = image.weak_bind_size;
    plan.lazy_bind_offset = image.lazy_bind_offset;
    plan.lazy_bind_size = image.lazy_bind_size;
    plan.chained_fixups_offset = image.chained_fixups_offset;
    plan.chained_fixups_size = image.chained_fixups_size;
    plan.load_bias = 0;
    plan.entry_address = 0;
    if (image.entry_point != 0) {
        for (const auto& segment : image.segments) {
            if (segment.name == "__PAGEZERO" || image.entry_point < segment.file_offset || image.entry_point - segment.file_offset >= segment.file_size) continue;
            plan.entry_address = segment.vm_address + (image.entry_point - segment.file_offset);
            break;
        }
        if (plan.entry_address == 0) plan.diagnostics.push_back("LC_MAIN entryoff does not fall inside a file-backed segment");
    }
    for (const auto& dependency : image.dependencies) {
        const auto candidate = resolve_candidate(executable, dependency, image.rpaths);
        if (!candidate.empty() && std::filesystem::exists(candidate)) plan.resolved_dependencies.push_back(dependency + " -> " + candidate.string());
        else plan.unresolved_dependencies.push_back(dependency);
    }
    if (plan.bind_size != 0 || plan.weak_bind_size != 0 || plan.lazy_bind_size != 0) plan.diagnostics.push_back("dyld bind streams present; symbol binding is not performed by the mapper");
    if (plan.chained_fixups_size != 0) plan.diagnostics.push_back("dyld chained fixups present; chained pointer rebasing/binding is not performed by the mapper");
    plan.valid = true;
    plan.diagnostics.push_back("validated segment mapping plan; no pages mapped and no code executed");
    return plan;
}

MappedImage::MappedImage(MappedImage&& other) noexcept
    : plan_(std::move(other.plan_)), result_(std::move(other.result_)), allocation_(other.allocation_), allocation_size_(other.allocation_size_) {
    other.allocation_ = nullptr;
    other.allocation_size_ = 0;
    other.result_ = {};
}

MappedImage& MappedImage::operator=(MappedImage&& other) noexcept {
    if (this == &other) return *this;
    reset();
    plan_ = std::move(other.plan_);
    result_ = std::move(other.result_);
    allocation_ = other.allocation_;
    allocation_size_ = other.allocation_size_;
    other.allocation_ = nullptr;
    other.allocation_size_ = 0;
    other.result_ = {};
    return *this;
}

MappedImage::~MappedImage() { reset(); }

void MappedImage::reset() noexcept {
#ifdef _WIN32
    if (allocation_) VirtualFree(allocation_, 0, MEM_RELEASE);
#endif
    allocation_ = nullptr;
    allocation_size_ = 0;
    result_ = {};
}

MappedImage MappedImage::map_file(const std::filesystem::path& executable, const LoadPlan& plan) {
    MappedImage mapped;
    mapped.plan_ = plan;
    if (!plan.valid) {
        mapped.result_.error = "cannot map an invalid load plan";
        return mapped;
    }
#ifdef _WIN32
    constexpr std::uint64_t page_size = 0x1000;
    const std::uint64_t span = (plan.image_max_address - plan.image_min_address + page_size - 1) & ~(page_size - 1);
    if (span == 0 || span > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        mapped.result_.error = "Mach-O image span is invalid or too large";
        return mapped;
    }
    std::ifstream input(executable, std::ios::binary);
    if (!input) {
        mapped.result_.error = "cannot open Mach-O file: " + executable.string();
        return mapped;
    }
    mapped.allocation_size_ = static_cast<std::size_t>(span);
    mapped.allocation_ = VirtualAlloc(nullptr, mapped.allocation_size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!mapped.allocation_) {
        mapped.result_.error = "VirtualAlloc failed: " + std::to_string(GetLastError());
        mapped.allocation_size_ = 0;
        return mapped;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(mapped.allocation_);
    mapped.result_.base_address = base;
    mapped.result_.load_bias = static_cast<std::uint64_t>(base) - plan.image_min_address;
    for (const auto& segment : plan.segments) {
        if (segment.file_size != 0) {
            if (plan.file_base_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) - segment.file_offset) {
                mapped.result_.error = "segment file offset exceeds stream limits";
                mapped.reset();
                return mapped;
            }
            std::vector<char> bytes(static_cast<std::size_t>(segment.file_size));
            input.clear();
            input.seekg(static_cast<std::streamoff>(plan.file_base_offset + segment.file_offset));
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
                mapped.result_.error = "short read while mapping segment: " + segment.name;
                mapped.reset();
                return mapped;
            }
            auto* destination = reinterpret_cast<std::byte*>(base + (segment.vm_address - plan.image_min_address));
            std::copy(bytes.begin(), bytes.end(), reinterpret_cast<char*>(destination));
        }
    }
    if (plan.rebase_size != 0) {
        if (plan.file_base_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) - plan.rebase_offset) {
            mapped.result_.error = "dyld rebase offset exceeds stream limits";
            mapped.reset();
            return mapped;
        }
        std::vector<std::uint8_t> rebase(plan.rebase_size);
        input.clear();
        input.seekg(static_cast<std::streamoff>(plan.file_base_offset + plan.rebase_offset));
        input.read(reinterpret_cast<char*>(rebase.data()), static_cast<std::streamsize>(rebase.size()));
        if (input.gcount() != static_cast<std::streamsize>(rebase.size())) {
            mapped.result_.error = "short read while loading dyld rebase stream";
            mapped.reset();
            return mapped;
        }
        std::string rebase_error;
        if (!apply_rebases(mapped.allocation_, plan, rebase, rebase_error)) {
            mapped.result_.error = "dyld rebasing failed: " + rebase_error;
            mapped.reset();
            return mapped;
        }
    }
    const auto collect_stream = [&](const char* name, std::uint32_t offset, std::uint32_t size) -> bool {
        if (size == 0) return true;
        if (plan.file_base_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) - offset) { mapped.result_.error = std::string(name) + " offset exceeds stream limits"; return false; }
        std::vector<std::uint8_t> stream(size);
        input.clear(); input.seekg(static_cast<std::streamoff>(plan.file_base_offset + offset));
        input.read(reinterpret_cast<char*>(stream.data()), static_cast<std::streamsize>(stream.size()));
        if (input.gcount() != static_cast<std::streamsize>(stream.size())) { mapped.result_.error = std::string("short read while loading ") + name + " stream"; return false; }
        std::string bind_error;
        if (!collect_bind_stream(name, stream, mapped.result_.bind_references, bind_error)) { mapped.result_.error = std::string(name) + " parsing failed: " + bind_error; return false; }
        return true;
    };
    if (!collect_stream("bind", plan.bind_offset, plan.bind_size) || !collect_stream("weak-bind", plan.weak_bind_offset, plan.weak_bind_size) || !collect_stream("lazy-bind", plan.lazy_bind_offset, plan.lazy_bind_size)) { mapped.reset(); return mapped; }
    for (const auto& segment : plan.segments) {
        DWORD old_protection = 0;
        auto* address = reinterpret_cast<void*>(base + (segment.vm_address - plan.image_min_address));
        if (!VirtualProtect(address, static_cast<SIZE_T>(segment.vm_size), windows_protection(segment.protections), &old_protection)) {
            mapped.result_.error = "VirtualProtect failed for segment " + segment.name + ": " + std::to_string(GetLastError());
            mapped.reset();
            return mapped;
        }
    }
    mapped.result_.mapped = true;
#else
    (void)executable;
    mapped.result_.error = "Mach-O memory mapping requires the Windows host backend";
#endif
    return mapped;
}

bool MappedImage::apply_bindings(const mnc::dyld::DylibRegistry& registry, const std::filesystem::path& requesting_image) {
    if (!result_.mapped) { result_.error = "cannot bind an image that is not mapped"; return false; }
    struct Pending { std::uintptr_t address; std::uint64_t value; };
    std::vector<Pending> pending;
    const auto base = reinterpret_cast<std::uintptr_t>(allocation_);
    for (const auto& reference : result_.bind_references) {
        if (reference.stream == "lazy-bind") continue;
        const auto resolution = registry.resolve(reference.symbol, requesting_image, reference.library_ordinal);
        if (!resolution.resolved) { result_.error = resolution.diagnostic; return false; }
        const SegmentMapping* segment = nullptr;
        for (const auto& candidate : plan_.segments) if (candidate.command_index == reference.segment_index) { segment = &candidate; break; }
        if (!segment || reference.segment_offset > segment->vm_size || segment->vm_size - reference.segment_offset < sizeof(std::uint64_t)) { result_.error = "bind location is outside a mapped segment: " + reference.symbol; return false; }
        const auto location = base + (segment->vm_address - plan_.image_min_address) + reference.segment_offset;
        pending.push_back({location, static_cast<std::uint64_t>(static_cast<std::int64_t>(resolution.address) + reference.addend)});
    }
    for (const auto& item : pending) *reinterpret_cast<std::uint64_t*>(item.address) = item.value;
    return true;
}

} // namespace mnc::loader
