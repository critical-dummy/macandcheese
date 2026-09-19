#include "mnc/bundle.hpp"
#include "mnc/macho.hpp"
#include <iostream>

static void usage() { std::cerr << "usage: mnc-inspect <Mach-O-file|Application.app>\n"; }
int main(int argc, char** argv) {
    if(argc != 2) { usage(); return 2; }
    try {
        std::filesystem::path p=argv[1];
        if(p.extension()==".app") {
            auto b=mnc::BundleInspector::inspect(p);
            std::cout << "bundle: " << b.root << "\nInfo.plist: " << b.info_plist << "\nExecutable: " << (b.executable.empty()?"<not found>":b.executable.string()) << "\nFramework files: " << b.frameworks.size() << "\nPlugin files: " << b.plugins.size() << "\n";
            for(const auto& d:b.diagnostics) std::cout << "[Mac&Cheese][WARN][Bundle] " << d << "\n";
            if(!b.executable.empty()) p=b.executable; else return 1;
        }
        auto image=mnc::MachOParser::parse_file(p);
        std::cout << "kind: "<<mnc::to_string(image.kind)<<"\ncpu: "<<mnc::to_string(image.cpu)<<"\nfile type: "<<image.file_type<<"\nentry offset: 0x"<<std::hex<<image.entry_point<<std::dec<<"\nsegments: "<<image.segments.size()<<"\n";
        for(const auto& s:image.segments) std::cout<<"  "<<s.name<<" vm=0x"<<std::hex<<s.vm_address<<" size=0x"<<s.vm_size<<std::dec<<"\n";
        for(const auto& d:image.dependencies) std::cout<<"dependency: "<<d<<"\n";
        for(const auto& r:image.rpaths) std::cout<<"rpath: "<<r<<"\n";
        for(const auto& d:image.diagnostics) std::cout<<"[Mac&Cheese][INFO][MachO] "<<d<<"\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"[Mac&Cheese][ERROR] "<<e.what()<<"\n"; return 1; }
}
