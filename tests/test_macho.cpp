#include "mnc/macho.hpp"
#include "mnc/fd.hpp"
#include "mnc/dmg.hpp"
#include "mnc/hfsplus.hpp"
#include "mnc/objc.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <fstream>

static void put32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) { for(int i=0;i<4;++i)b[p+i]=std::uint8_t(v>>(8*i)); }
static void put64(std::vector<std::uint8_t>& b, std::size_t p, std::uint64_t v) { put32(b,p,std::uint32_t(v)); put32(b,p+4,std::uint32_t(v>>32)); }
static void putbe32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) { for(int i=0;i<4;++i)b[p+i]=std::uint8_t(v>>(24-8*i)); }
static void putbe64(std::vector<std::uint8_t>& b, std::size_t p, std::uint64_t v) { for(int i=0;i<8;++i)b[p+i]=std::uint8_t(v>>(56-8*i)); }
static void objc_test_imp(void*) {}
int main() {
    std::vector<std::uint8_t> b(32+72+24,0);
    put32(b,0,0xfeedfacf); put32(b,4,0x01000007); put32(b,12,2); put32(b,16,2); put32(b,20,96);
    put32(b,32,0x19); put32(b,36,72); const char name[]="__TEXT"; for(int i=0;name[i];++i)b[40+i]=name[i]; put64(b,56,0x100000000); put64(b,64,0x1000); put64(b,72,0); put64(b,80,0x1000); put32(b,92,5); put32(b,96,0);
    put32(b,104,0x80000028); put32(b,108,24); put64(b,112,0x200);
    auto image=mnc::MachOParser::parse_bytes(b);
    assert(image.kind==mnc::BinaryKind::Thin64); assert(image.cpu==mnc::CpuType::X86_64); assert(image.segments.size()==1); assert(image.segments[0].name=="__TEXT"); assert(image.entry_point==0x200);
    bool failed=false; try { mnc::MachOParser::parse_bytes({1,2,3,4}); } catch(const std::runtime_error&) { failed=true; } assert(failed);
    auto path = std::filesystem::temp_directory_path() / "mnc-fd-test.tmp";
    mnc::darwin::FileDescriptorTable fds;
    auto fd = fds.open_file(path, mnc::darwin::OpenMode::CreateOrTruncate);
    assert(fd >= 3);
    const char text[] = "Mac&Cheese FD\n";
    assert(fds.write(fd, text, sizeof(text) - 1) == static_cast<std::ptrdiff_t>(sizeof(text) - 1));
    auto duplicate = fds.duplicate(fd); assert(duplicate >= 3); assert(fds.valid(duplicate));
    assert(fds.close(duplicate)); assert(fds.close(fd));
    std::filesystem::remove(path);
    auto dmg_path = std::filesystem::temp_directory_path() / "mnc-dmg-test.dmg";
    const std::string xml = "<?xml version=\"1.0\"?><plist><key>blkx</key></plist>";
    std::vector<std::uint8_t> dmg(512 + xml.size() + 512, 0);
    std::copy(xml.begin(), xml.end(), dmg.begin() + 512);
    auto trailer = dmg.size() - 512;
    dmg[trailer] = 'k'; dmg[trailer + 1] = 'o'; dmg[trailer + 2] = 'l'; dmg[trailer + 3] = 'y';
    putbe32(dmg, trailer + 4, 4); putbe32(dmg, trailer + 8, 512);
    putbe64(dmg, trailer + 216, 512); putbe64(dmg, trailer + 224, xml.size()); putbe64(dmg, trailer + 492, 1);
    { std::ofstream out(dmg_path, std::ios::binary); out.write(reinterpret_cast<const char*>(dmg.data()), static_cast<std::streamsize>(dmg.size())); }
    auto dmg_report = mnc::image::DmgImage::inspect(dmg_path);
    assert(dmg_report.is_udif); assert(dmg_report.version == 4); assert(dmg_report.has_property_list); assert(dmg_report.has_block_map);
    std::filesystem::remove(dmg_path);
    auto hfs_path = std::filesystem::temp_directory_path() / "mnc-hfs-test.img";
    std::vector<std::uint8_t> hfs_image(2048, 0);
    hfs_image[1024] = 'H'; hfs_image[1025] = '+';
    putbe32(hfs_image, 1056, 7); putbe32(hfs_image, 1060, 3); putbe32(hfs_image, 1064, 4096); putbe32(hfs_image, 1068, 2048); putbe32(hfs_image, 1072, 1024);
    { std::ofstream out(hfs_path, std::ios::binary); out.write(reinterpret_cast<const char*>(hfs_image.data()), static_cast<std::streamsize>(hfs_image.size())); }
    mnc::image::DmgReport raw_report; raw_report.is_udif = true; raw_report.image_size = hfs_image.size(); raw_report.sector_count = 4; raw_report.extents.push_back({0, 0, 4, 0, hfs_image.size()});
    auto hfs_report = mnc::filesystem::probe_hfs_plus(hfs_path, raw_report);
    assert(hfs_report.recognized); assert(hfs_report.block_size == 4096); assert(hfs_report.file_count == 7);
    std::filesystem::remove(hfs_path);
    mnc::objc::Runtime objc;
    const auto selector = objc.register_selector("description");
    const auto root = objc.register_class("NSObject");
    const auto child = objc.register_class("TestObject", root);
    std::string objc_diagnostic;
    assert(objc.register_method(root, selector, objc_test_imp, objc_diagnostic));
    assert(objc.send0(child, selector, nullptr, objc_diagnostic));
    assert(!objc.send0(child, objc.register_selector("missing"), nullptr, objc_diagnostic));
    assert(mnc::objc::classify_type_encoding("v16@0:8").kind == mnc::objc::SignatureKind::VoidReceiver);
    assert(mnc::objc::classify_type_encoding("i16@0:8").kind == mnc::objc::SignatureKind::IntReceiver);
    assert(mnc::objc::classify_type_encoding("v24@0:8@16").kind == mnc::objc::SignatureKind::Unsupported);
    std::cout << "mnc-tests: all checks passed\n";
}
