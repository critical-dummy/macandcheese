# Mac&Cheese

Mac&Cheese is a **Windows-native compatibility framework/runtime** that progressively reimplements the execution environment expected by macOS applications on Windows. It is not macOS itself, a virtual machine, a Hackintosh, or a web app.

## Current Implementation (0.1.0)

This release represents the initial runtime stage and includes a Wine-like launcher entry point. `mnc-run` inspects input applications and determines whether they can be executed, but it does not yet map or execute Mach-O binaries.

- Parsing of 64-bit little-endian Mach-O headers and load commands
- x86_64 and arm64 CPU identification
- Inspection of segments, `LC_MAIN`, dylib dependencies, and rpaths
- Initial selection of the first supported 64-bit slice in FAT/universal binaries
- Discovery of `.app/Contents`, `Info.plist`, `MacOS`, `Frameworks`, and `PlugIns`
- Explicit diagnostics for unsupported operations
- The `mnc-run` launcher CLI with explicit execution-blocking diagnostics
- An internal integer file descriptor table that does not expose Windows `HANDLE` values to applications
- A shared `CompatibilityRuntime` input layer
- DMG UDIF trailer detection with explicit diagnostics indicating that extraction is unsupported

Mach-O mapping and relocation, dyld symbol binding, the Objective-C runtime, and execution on Windows have not yet been implemented. This tool is not a substitute for a functional compatibility runtime.

## Building

On Windows 10/11, use the Visual Studio Developer Command Prompt:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Validation build on Linux:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Automated Windows Build

In PowerShell, navigate to the project directory and run the following commands to perform an x64 Release build followed by the test suite:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build-windows.ps1
```

Visual Studio 2022 and CMake must be installed.

## Usage

```text
mnc-inspect SomeApplication.app
mnc-inspect SomeApplication.app/Contents/MacOS/SomeApplication
mnc-run --diagnose SomeApplication.app
mnc-run SomeApplication.app
mnc-run --diagnose SomeApplication.dmg
```

Currently, `mnc-run` does not silently fail on modern arm64/arm64e applications or pretend that they have been successfully executed. Actual execution will require the sequential implementation of Mach-O mapping, dyld-compatible dependency loading, CPU translation or native-architecture execution, the Objective-C/Swift runtime, and a Foundation/AppKit backend.

Mac&Cheese's Darwin/POSIX layer does not depend on WSL or Linux file descriptors. `mnc::darwin::FileDescriptorTable` wraps Windows `HANDLE` values as macOS-style integer file descriptors. Files, sockets, pipes, and process file descriptors will eventually be integrated into the same table.

The `[Mac&Cheese][WARN]` and `[ERROR]` diagnostics in the output are intended to prevent unimplemented functionality from being misrepresented as successful.

## Next Steps

1. Accurate FAT slice selection based on CPU architecture and endianness
2. A minimal binary/XML parser for `Info.plist` and bundle executable resolution
3. A virtual address-space abstraction and segment mapping based on Windows `VirtualAlloc`
4. A model for dyld dependency resolution, rpaths, and symbol tables
5. Test-first implementation of a small Objective-C runtime ABI and Foundation layer

The project's product goal is not merely `mnc-inspect`. Its ultimate goal is to run real macOS applications on a Windows-native backend while allowing those applications to call macOS-facing APIs provided by Mac&Cheese.
