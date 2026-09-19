#include "mnc/runtime.hpp"
#include "mnc/bundle.hpp"
#include "mnc/dmg.hpp"
#include "mnc/hfsplus.hpp"
#include "mnc/apfs.hpp"
#include "mnc/macho.hpp"
#include "mnc/loader.hpp"
#include "mnc/dylib.hpp"
#include "mnc/objc.hpp"

#include <stdexcept>
#include <vector>

namespace mnc::runtime {
CompatibilityRuntime::CompatibilityRuntime() = default;

bool CompatibilityRuntime::extract_dmg(const std::filesystem::path& input,
                                       const std::filesystem::path& destination,
                                       std::vector<std::string>& diagnostics) const {
    try {
        const auto dmg = image::DmgImage::inspect(input);
        diagnostics = dmg.diagnostics;
        if (!dmg.is_udif) { diagnostics.push_back("input is not a UDIF DMG"); return false; }
        const auto hfs = filesystem::probe_hfs_plus(input, dmg);
        diagnostics.push_back(hfs.diagnostic);
        if (!hfs.recognized) { diagnostics.push_back("only HFS+/HFSX extraction is enabled in this build"); return false; }
        std::string extraction_diagnostic;
        const bool ok = filesystem::extract_hfs_volume(input, dmg, hfs, destination, extraction_diagnostic);
        diagnostics.push_back(extraction_diagnostic);
        return ok;
    } catch (const std::exception& error) {
        diagnostics.push_back(error.what());
        return false;
    }
}

LaunchReport CompatibilityRuntime::prepare(const std::filesystem::path& input) const {
    LaunchReport report; report.input = input;
    try {
        if (input.extension() == ".dmg") {
            auto dmg = image::DmgImage::inspect(input);
            if (!dmg.is_udif) { report.status = LaunchStatus::Unsupported; report.diagnostics = dmg.diagnostics; return report; }
            report.diagnostics = dmg.diagnostics;
            auto hfs = filesystem::probe_hfs_plus(input, dmg);
            report.diagnostics.push_back(hfs.diagnostic);
            if (hfs.recognized) {
                report.diagnostics.push_back("HFS+ allocation block size=" + std::to_string(hfs.block_size));
                report.diagnostics.push_back("HFS+ catalog start block=" + std::to_string(hfs.catalog_start_block) + ", node size=" + std::to_string(hfs.catalog_node_size) + ", root node=" + std::to_string(hfs.catalog_root_node) + ", first leaf=" + std::to_string(hfs.catalog_first_leaf) + ", last leaf=" + std::to_string(hfs.catalog_last_leaf) + ", leaf records=" + std::to_string(hfs.catalog_leaf_records));
                report.diagnostics.push_back("HFS+ catalog leaf nodes=" + std::to_string(hfs.catalog_leaf_nodes_scanned) + ", records parsed=" + std::to_string(hfs.catalog_leaf_record_types) + ", folders=" + std::to_string(hfs.catalog_folder_records) + ", files=" + std::to_string(hfs.catalog_file_records));
                report.diagnostics.push_back("HFS+ first leaf kind=" + std::to_string(hfs.catalog_first_leaf_kind) + ", node records=" + std::to_string(hfs.catalog_first_leaf_node_records) + ", offsets=" + std::to_string(hfs.catalog_first_record_offset) + "/" + std::to_string(hfs.catalog_second_record_offset));
                report.diagnostics.push_back("HFS+ catalog entries discovered=" + std::to_string(hfs.catalog_entries.size()));
                report.diagnostics.push_back("DMG is readable by the Mac&Cheese extraction subsystem");
            } else {
                auto apfs = filesystem::probe_apfs(input, dmg);
                report.diagnostics.push_back(apfs.diagnostic);
                if (apfs.gpt_detected) report.diagnostics.push_back("GPT partition map detected");
                if (apfs.gpt_partition_end_sector >= apfs.gpt_partition_start_sector && apfs.gpt_partition_start_sector != 0) {
                    report.diagnostics.push_back("GPT partition range=" + std::to_string(apfs.gpt_partition_start_sector) + "-" + std::to_string(apfs.gpt_partition_end_sector) + ", type=" + apfs.partition_type);
                }
                if (apfs.recognized) report.diagnostics.push_back("APFS start sector=" + std::to_string(apfs.apfs_start_sector) + ", block size=" + std::to_string(apfs.block_size));
                else report.diagnostics.push_back("DMG filesystem is not currently recognized as HFS+/HFSX/APFS");
            }
            report.status = LaunchStatus::Blocked;
            report.diagnostics.push_back("DMG extraction is available with: mnc-run --extract <disk.dmg> <destination>");
            return report;
        }
        std::filesystem::path executable = input;
        AppBundle bundle;
        bool has_bundle = false;
        if (input.extension() == ".app") {
            bundle = BundleInspector::inspect(input);
            has_bundle = true;
            report.diagnostics = bundle.diagnostics;
            if (bundle.executable.empty()) { report.status = LaunchStatus::InvalidInput; report.diagnostics.push_back("bundle has no executable candidate"); return report; }
            executable = bundle.executable;
        }
        auto image = MachOParser::parse_file(executable);
        report.executable = executable;
        report.diagnostics.push_back("Mach-O image parsed");
        const auto plan = loader::make_load_plan(executable, image);
        report.diagnostics.push_back("Mach-O load plan: segments=" + std::to_string(plan.segments.size()) + ", address range=0x" + std::to_string(plan.image_min_address) + "-0x" + std::to_string(plan.image_max_address) + ", entry=0x" + std::to_string(plan.entry_address));
        report.diagnostics.insert(report.diagnostics.end(), plan.diagnostics.begin(), plan.diagnostics.end());
        report.diagnostics.push_back("resolved dependencies=" + std::to_string(plan.resolved_dependencies.size()) + ", unresolved dependencies=" + std::to_string(plan.unresolved_dependencies.size()));
#ifdef _WIN32
        if (plan.valid) {
            auto mapped = loader::MappedImage::map_file(executable, plan);
            if (mapped.valid()) {
                report.diagnostics.push_back("Windows segment mapping succeeded: base=0x" + std::to_string(mapped.result().base_address) + ", load bias=0x" + std::to_string(mapped.result().load_bias));
                report.diagnostics.push_back("mapped image was released without executing code");
            } else {
                report.diagnostics.push_back("Windows segment mapping failed: " + mapped.result().error);
            }
            if (has_bundle) {
                dyld::DylibRegistry registry;
                std::vector<loader::MappedImage> images;
                auto main_image = loader::MappedImage::map_file(executable, plan);
                if (!main_image.valid()) {
                    report.diagnostics.push_back("dyld integration: main image mapping failed: " + main_image.result().error);
                } else {
                    const auto main_bias = main_image.result().load_bias;
                    std::string registration;
                    registry.register_image(executable, main_bias, registration, -1);
                    report.diagnostics.push_back("dyld registry: " + registration);
                    images.push_back(std::move(main_image));
                    objc::Runtime objc_runtime;
                    const auto metadata = objc::register_mapped_metadata(objc_runtime, images.front().result().base_address, plan.image_min_address, plan.image_max_address, image.sections);
                    report.diagnostics.push_back("Objective-C metadata: classes=" + std::to_string(metadata.classes_discovered) + ", protocols=" + std::to_string(metadata.protocols_discovered) + ", methods=" + std::to_string(metadata.methods_discovered));
                    report.diagnostics.insert(report.diagnostics.end(), metadata.diagnostics.begin(), metadata.diagnostics.end());
                    std::vector<std::filesystem::path> framework_search_paths{bundle.root / "Contents" / "Frameworks"};
                    for (std::size_t dependency_index = 0; dependency_index < image.dependencies.size(); ++dependency_index) {
                        const auto& dependency = image.dependencies[dependency_index];
                        const auto framework = registry.resolve_path(dependency, executable, image.rpaths, framework_search_paths);
                        if (framework.empty()) {
                            report.diagnostics.push_back("dyld path unresolved: " + dependency);
                            continue;
                        }
                        try {
                            const auto framework_image = MachOParser::parse_file(framework);
                            const auto framework_plan = loader::make_load_plan(framework, framework_image);
                            auto mapped_framework = loader::MappedImage::map_file(framework, framework_plan);
                            if (!mapped_framework.valid()) { report.diagnostics.push_back("dyld image mapping failed: " + framework.string() + ": " + mapped_framework.result().error); continue; }
                            const auto framework_bias = mapped_framework.result().load_bias;
                            registry.register_image(framework, framework_bias, registration, static_cast<std::int64_t>(dependency_index + 1));
                            report.diagnostics.push_back("dyld registry: " + registration);
                            images.push_back(std::move(mapped_framework));
                        } catch (const std::exception& framework_error) {
                            report.diagnostics.push_back("dyld registry skipped framework " + framework.string() + ": " + framework_error.what());
                        }
                    }
                    if (!images.front().apply_bindings(registry, executable)) report.diagnostics.push_back("dyld non-lazy binding blocked: " + images.front().result().error);
                    else report.diagnostics.push_back("dyld non-lazy binding applied; lazy binding remains deferred");
                }
            }
        }
#else
        report.diagnostics.push_back("Windows segment mapping not attempted on this host");
#endif
        report.diagnostics.push_back("native page mapping, rebasing/binding, and dyld initialization remain blocked");
        report.status = LaunchStatus::Blocked;
        return report;
    } catch (const std::exception& error) {
        report.status = LaunchStatus::InvalidInput;
        report.diagnostics.push_back(error.what());
        return report;
    }
}
const char* to_string(LaunchStatus status) {
    switch (status) { case LaunchStatus::ExecutableReady: return "executable-ready"; case LaunchStatus::Blocked: return "blocked"; case LaunchStatus::InvalidInput: return "invalid-input"; case LaunchStatus::Unsupported: return "unsupported"; }
    return "unknown";
}
} // namespace mnc::runtime
