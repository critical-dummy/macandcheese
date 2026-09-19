#include "mnc/objc.hpp"

#include <cstdint>

namespace mnc::objc {

SEL Runtime::register_selector(const std::string& name) {
    const auto existing = selector_ids_.find(name);
    if (existing != selector_ids_.end()) return existing->second;
    const SEL id = selectors_.size();
    selectors_.push_back(name);
    selector_ids_.emplace(name, id);
    return id;
}

ClassId Runtime::register_class(const std::string& name, ClassId superclass) {
    const auto existing = class_ids_.find(name);
    if (existing != class_ids_.end()) return existing->second;
    if (superclass >= classes_.size()) superclass = 0;
    const ClassId id = classes_.size();
    classes_.push_back({id, superclass, name, {}});
    class_ids_.emplace(name, id);
    return id;
}

bool Runtime::register_method(ClassId class_id, SEL selector, IMP0 implementation, std::string& diagnostic) {
    if (class_id == 0 || class_id >= classes_.size()) { diagnostic = "cannot register method on unknown Objective-C class"; return false; }
    if (selector == 0 || selector >= selectors_.size()) { diagnostic = "cannot register unknown Objective-C selector"; return false; }
    if (!implementation) { diagnostic = "cannot register null Objective-C IMP"; return false; }
    classes_[class_id].methods[selector] = implementation;
    diagnostic = "Objective-C method registered: " + classes_[class_id].name + " " + selectors_[selector];
    return true;
}

IMP0 Runtime::lookup_method(ClassId class_id, SEL selector) const {
    while (class_id != 0 && class_id < classes_.size()) {
        const auto& entry = classes_[class_id];
        const auto method = entry.methods.find(selector);
        if (method != entry.methods.end()) return method->second;
        class_id = entry.superclass;
    }
    return nullptr;
}

bool Runtime::send0(ClassId class_id, SEL selector, void* receiver, std::string& diagnostic) const {
    const auto implementation = lookup_method(class_id, selector);
    if (!implementation) { diagnostic = "unrecognized Objective-C selector '" + std::string(selector_name(selector)) + "'"; return false; }
    implementation(receiver);
    diagnostic = "Objective-C 0-argument message dispatched";
    return true;
}

const char* Runtime::selector_name(SEL selector) const { return selector < selectors_.size() ? selectors_[selector].c_str() : "<unknown-selector>"; }
const ClassEntry* Runtime::find_class(ClassId class_id) const { return class_id < classes_.size() ? &classes_[class_id] : nullptr; }

SignatureInfo classify_type_encoding(const std::string& encoding) {
    SignatureInfo result;
    if (encoding.empty()) { result.diagnostic = "empty Objective-C type encoding"; return result; }
    std::size_t cursor = 0;
    const auto skip_digits = [&]() { while (cursor < encoding.size() && (encoding[cursor] == '-' || (encoding[cursor] >= '0' && encoding[cursor] <= '9'))) ++cursor; };
    char return_type = encoding[cursor++];
    while (cursor < encoding.size() && (encoding[cursor] == 'r' || encoding[cursor] == 'n' || encoding[cursor] == 'N' || encoding[cursor] == 'o' || encoding[cursor] == 'O' || encoding[cursor] == 'R' || encoding[cursor] == 'V')) return_type = encoding[cursor++];
    skip_digits();
    if (cursor >= encoding.size() || encoding[cursor++] != '@') { result.diagnostic = "encoding does not contain Objective-C receiver"; return result; }
    skip_digits();
    if (cursor >= encoding.size() || encoding[cursor++] != ':') { result.diagnostic = "encoding does not contain Objective-C selector argument"; return result; }
    skip_digits();
    if (cursor != encoding.size()) { result.diagnostic = "method has explicit arguments not supported by the initial dispatcher"; return result; }
    result.argument_count = 0;
    if (return_type == 'v') result.kind = SignatureKind::VoidReceiver;
    else if (return_type == '^') result.kind = SignatureKind::PointerReceiver;
    else if (return_type == 'c' || return_type == 'i' || return_type == 's' || return_type == 'l' || return_type == 'q' || return_type == 'C' || return_type == 'I' || return_type == 'S' || return_type == 'L' || return_type == 'Q' || return_type == 'B') result.kind = SignatureKind::IntReceiver;
    else { result.diagnostic = "return type is not supported by the initial dispatcher"; return result; }
    result.diagnostic = "0-explicit-argument Objective-C signature classified";
    return result;
}

MetadataResult register_mapped_metadata(Runtime& runtime, std::uintptr_t mapping_base, std::uint64_t image_min_address, std::uint64_t image_max_address, const std::vector<mnc::SectionInfo>& sections) {
    MetadataResult result;
    const auto address_in_image = [&](std::uintptr_t address, std::size_t size) { return address >= mapping_base && address <= mapping_base + (image_max_address - image_min_address) && size <= mapping_base + (image_max_address - image_min_address) - address; };
    const auto mapped_address = [&](std::uint64_t vm_address) { return mapping_base + (vm_address - image_min_address); };
    const auto read_ptr = [&](std::uintptr_t address, std::uintptr_t& value) { if (!address_in_image(address, sizeof(std::uintptr_t))) return false; value = *reinterpret_cast<const std::uintptr_t*>(address); return true; };
    for (const auto& section : sections) {
        if (section.name != "__objc_classlist" || section.size < sizeof(std::uintptr_t)) continue;
        const auto list = mapped_address(section.address);
        const auto count = section.size / sizeof(std::uintptr_t);
        for (std::uint64_t index = 0; index < count; ++index) {
            std::uintptr_t class_address = 0;
            if (!read_ptr(list + index * sizeof(std::uintptr_t), class_address)) { result.diagnostics.push_back("Objective-C classlist pointer is outside mapped image"); break; }
            std::uintptr_t superclass_address = 0;
            read_ptr(class_address + 8, superclass_address);
            std::uintptr_t data_address = 0;
            if (!read_ptr(class_address + 32, data_address)) { result.diagnostics.push_back("Objective-C class_t data pointer is outside mapped image"); continue; }
            data_address &= ~std::uintptr_t(0x7);
            std::uintptr_t name_address = 0;
            if (!read_ptr(data_address + 24, name_address) || !address_in_image(name_address, 1)) { result.diagnostics.push_back("Objective-C class name pointer is outside mapped image"); continue; }
            const auto* name = reinterpret_cast<const char*>(name_address);
            std::string class_name;
            for (std::size_t n = 0; n < 1024 && address_in_image(name_address + n, 1) && name[n]; ++n) class_name.push_back(name[n]);
            if (class_name.empty()) continue;
            ClassId superclass_id = 0;
            if (superclass_address != 0) {
                std::uintptr_t superclass_data = 0, superclass_name_address = 0;
                if (read_ptr(superclass_address + 32, superclass_data)) {
                    superclass_data &= ~std::uintptr_t(0x7);
                    if (read_ptr(superclass_data + 24, superclass_name_address) && address_in_image(superclass_name_address, 1)) {
                        const auto* superclass_name_text = reinterpret_cast<const char*>(superclass_name_address);
                        std::string superclass_name;
                        for (std::size_t n = 0; n < 1024 && address_in_image(superclass_name_address + n, 1) && superclass_name_text[n]; ++n) superclass_name.push_back(superclass_name_text[n]);
                        if (!superclass_name.empty()) superclass_id = runtime.register_class(superclass_name);
                    }
                }
            }
            const auto class_id = runtime.register_class(class_name, superclass_id);
            ++result.classes_discovered;
            std::uintptr_t methods_address = 0;
            if (!read_ptr(data_address + 32, methods_address) || methods_address == 0) continue;
            if (!address_in_image(methods_address, 8)) continue;
            const auto count_methods = *reinterpret_cast<const std::uint32_t*>(methods_address + 4);
            const auto entry_size = *reinterpret_cast<const std::uint32_t*>(methods_address);
            if (entry_size < 24 || count_methods > 100000 || !address_in_image(methods_address, 8 + std::size_t(entry_size) * count_methods)) continue;
            for (std::uint32_t method_index = 0; method_index < count_methods; ++method_index) {
                const auto entry = methods_address + 8 + std::size_t(entry_size) * method_index;
                std::uintptr_t selector_address = 0;
                if (!read_ptr(entry, selector_address) || !address_in_image(selector_address, 1)) continue;
                std::string selector_name;
                const auto* selector_string = reinterpret_cast<const char*>(selector_address);
                for (std::size_t n = 0; n < 1024 && address_in_image(selector_address + n, 1) && selector_string[n]; ++n) selector_name.push_back(selector_string[n]);
                if (selector_name.empty()) continue;
                runtime.register_selector(selector_name);
                ++result.methods_discovered;
                result.diagnostics.push_back("Objective-C method discovered but IMP registration is deferred: " + class_name + " " + selector_name);
            }
            (void)class_id;
        }
    }
    for (const auto& section : sections) {
        if (section.name != "__objc_catlist" || section.size < sizeof(std::uintptr_t)) continue;
        const auto list = mapped_address(section.address);
        const auto count = section.size / sizeof(std::uintptr_t);
        for (std::uint64_t index = 0; index < count; ++index) {
            std::uintptr_t category_address = 0;
            if (!read_ptr(list + index * sizeof(std::uintptr_t), category_address)) { result.diagnostics.push_back("Objective-C category list pointer is outside mapped image"); break; }
            std::uintptr_t category_name_address = 0, class_address = 0;
            if (!read_ptr(category_address, category_name_address) || !read_ptr(category_address + 8, class_address)) continue;
            if (!address_in_image(category_name_address, 1)) continue;
            std::string category_name;
            const auto* category_name_text = reinterpret_cast<const char*>(category_name_address);
            for (std::size_t n = 0; n < 1024 && address_in_image(category_name_address + n, 1) && category_name_text[n]; ++n) category_name.push_back(category_name_text[n]);
            if (category_name.empty()) continue;
            std::string class_name = "<unknown-class>";
            std::uintptr_t class_data = 0, class_name_address = 0;
            if (read_ptr(class_address + 32, class_data)) {
                class_data &= ~std::uintptr_t(0x7);
                if (read_ptr(class_data + 24, class_name_address) && address_in_image(class_name_address, 1)) {
                    const auto* class_name_text = reinterpret_cast<const char*>(class_name_address);
                    class_name.clear();
                    for (std::size_t n = 0; n < 1024 && address_in_image(class_name_address + n, 1) && class_name_text[n]; ++n) class_name.push_back(class_name_text[n]);
                }
            }
            runtime.register_class(class_name);
            ++result.classes_discovered;
            result.diagnostics.push_back("Objective-C category discovered: " + class_name + "(" + category_name + ")");
            for (const auto method_offset : {std::uint64_t(16), std::uint64_t(24)}) {
                std::uintptr_t methods_address = 0;
                if (!read_ptr(category_address + method_offset, methods_address) || methods_address == 0 || !address_in_image(methods_address, 8)) continue;
                const auto entry_size = *reinterpret_cast<const std::uint32_t*>(methods_address);
                const auto method_count = *reinterpret_cast<const std::uint32_t*>(methods_address + 4);
                if (entry_size < 24 || method_count > 100000 || !address_in_image(methods_address, 8 + std::size_t(entry_size) * method_count)) continue;
                for (std::uint32_t method_index = 0; method_index < method_count; ++method_index) {
                    const auto entry = methods_address + 8 + std::size_t(entry_size) * method_index;
                    std::uintptr_t selector_address = 0;
                    if (!read_ptr(entry, selector_address) || !address_in_image(selector_address, 1)) continue;
                    const auto* selector_text = reinterpret_cast<const char*>(selector_address);
                    std::string selector_name;
                    for (std::size_t n = 0; n < 1024 && address_in_image(selector_address + n, 1) && selector_text[n]; ++n) selector_name.push_back(selector_text[n]);
                    if (selector_name.empty()) continue;
                    runtime.register_selector(selector_name);
                    ++result.methods_discovered;
                    result.diagnostics.push_back("Objective-C category method discovered but IMP registration is deferred: " + class_name + "(" + category_name + ") " + selector_name);
                }
            }
        }
    }
    for (const auto& section : sections) {
        if (section.name != "__objc_protolist" || section.size < sizeof(std::uintptr_t)) continue;
        const auto list = mapped_address(section.address);
        const auto count = section.size / sizeof(std::uintptr_t);
        for (std::uint64_t index = 0; index < count; ++index) {
            std::uintptr_t protocol_address = 0;
            if (!read_ptr(list + index * sizeof(std::uintptr_t), protocol_address)) { result.diagnostics.push_back("Objective-C protocol list pointer is outside mapped image"); break; }
            std::uintptr_t name_address = 0;
            if (!read_ptr(protocol_address + 8, name_address) || !address_in_image(name_address, 1)) continue;
            const auto* name_text = reinterpret_cast<const char*>(name_address);
            std::string protocol_name;
            for (std::size_t n = 0; n < 1024 && address_in_image(name_address + n, 1) && name_text[n]; ++n) protocol_name.push_back(name_text[n]);
            if (protocol_name.empty()) continue;
            ++result.protocols_discovered;
            runtime.register_selector(protocol_name);
            result.diagnostics.push_back("Objective-C protocol discovered: " + protocol_name);
            for (const auto method_offset : {std::uint64_t(24), std::uint64_t(32), std::uint64_t(40), std::uint64_t(48)}) {
                std::uintptr_t methods_address = 0;
                if (!read_ptr(protocol_address + method_offset, methods_address) || methods_address == 0 || !address_in_image(methods_address, 8)) continue;
                const auto entry_size = *reinterpret_cast<const std::uint32_t*>(methods_address);
                const auto method_count = *reinterpret_cast<const std::uint32_t*>(methods_address + 4);
                if (entry_size < 16 || method_count > 100000 || !address_in_image(methods_address, 8 + std::size_t(entry_size) * method_count)) continue;
                for (std::uint32_t method_index = 0; method_index < method_count; ++method_index) {
                    const auto entry = methods_address + 8 + std::size_t(entry_size) * method_index;
                    std::uintptr_t selector_address = 0, types_address = 0;
                    if (!read_ptr(entry, selector_address) || !read_ptr(entry + 8, types_address) || !address_in_image(selector_address, 1) || !address_in_image(types_address, 1)) continue;
                    const auto* selector_text = reinterpret_cast<const char*>(selector_address);
                    const auto* types_text = reinterpret_cast<const char*>(types_address);
                    std::string selector_name, type_encoding;
                    for (std::size_t n = 0; n < 1024 && address_in_image(selector_address + n, 1) && selector_text[n]; ++n) selector_name.push_back(selector_text[n]);
                    for (std::size_t n = 0; n < 1024 && address_in_image(types_address + n, 1) && types_text[n]; ++n) type_encoding.push_back(types_text[n]);
                    if (selector_name.empty()) continue;
                    runtime.register_selector(selector_name);
                    ++result.methods_discovered;
                    const auto signature = classify_type_encoding(type_encoding);
                    result.diagnostics.push_back("Objective-C protocol method discovered: " + protocol_name + " " + selector_name + " types=" + type_encoding + "; " + signature.diagnostic + "; IMP registration is deferred");
                }
            }
        }
    }
    if (result.classes_discovered == 0) result.diagnostics.push_back("no Objective-C __objc_classlist section was discovered");
    return result;
}

} // namespace mnc::objc
