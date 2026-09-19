#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "mnc/macho.hpp"

namespace mnc::objc {

using SEL = std::size_t;
using ClassId = std::size_t;
using IMP0 = void(*)(void* receiver);

struct MethodEntry {
    SEL selector{};
    IMP0 implementation{};
};

struct ClassEntry {
    ClassId id{};
    ClassId superclass{};
    std::string name;
    std::unordered_map<SEL, IMP0> methods;
};

struct MetadataResult {
    std::size_t classes_discovered{};
    std::size_t protocols_discovered{};
    std::size_t methods_discovered{};
    std::vector<std::string> diagnostics;
};

enum class SignatureKind { Unsupported, VoidReceiver, PointerReceiver, IntReceiver };

struct SignatureInfo {
    SignatureKind kind{SignatureKind::Unsupported};
    std::size_t argument_count{};
    std::string diagnostic;
};

class Runtime {
public:
    SEL register_selector(const std::string& name);
    ClassId register_class(const std::string& name, ClassId superclass = 0);
    bool register_method(ClassId class_id, SEL selector, IMP0 implementation, std::string& diagnostic);
    IMP0 lookup_method(ClassId class_id, SEL selector) const;
    bool send0(ClassId class_id, SEL selector, void* receiver, std::string& diagnostic) const;
    const char* selector_name(SEL selector) const;
    const ClassEntry* find_class(ClassId class_id) const;

private:
    std::vector<std::string> selectors_{""};
    std::unordered_map<std::string, SEL> selector_ids_;
    std::vector<ClassEntry> classes_{{0, 0, "", {}}};
    std::unordered_map<std::string, ClassId> class_ids_;
};

MetadataResult register_mapped_metadata(Runtime& runtime,
                                         std::uintptr_t mapping_base,
                                         std::uint64_t image_min_address,
                                         std::uint64_t image_max_address,
                                         const std::vector<mnc::SectionInfo>& sections);

SignatureInfo classify_type_encoding(const std::string& encoding);

} // namespace mnc::objc
