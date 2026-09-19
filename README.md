# Mac&Cheese

**A Windows-native macOS application compatibility framework/runtime.**

Mac&Cheese is an experimental C++ framework that aims to run macOS applications natively on Windows by reconstructing the execution environment those applications expect.

It is inspired by the idea behind Wine, but approaches the problem in the opposite direction:

> **macOS applications → Windows**

Mac&Cheese does **not** virtualize macOS, boot macOS, or emulate an entire Apple computer. Instead, the project progressively reconstructs the relevant binary, runtime, filesystem, and framework layers required by macOS applications.

## What is Mac&Cheese?

A macOS application is more than a `.app` directory.

Its executable may depend on:

* Mach-O binaries
* `dyld`
* Objective-C runtime
* Darwin/POSIX interfaces
* system libraries
* CoreFoundation
* Foundation
* graphics and text frameworks
* application frameworks
* resources stored inside macOS filesystem images

Mac&Cheese works downward through these layers, identifying what an application actually depends on and rebuilding the necessary behavior on Windows.

The goal is not to reproduce macOS visually.

The goal is to reproduce the **execution environment** an application needs.

## Current Architecture

```text
macOS Application
        │
        ▼
    .app Bundle
        │
        ▼
   Mach-O Executable
        │
        ├── Mach-O parser
        ├── dyld / dylib resolution
        ├── image loader
        └── Objective-C metadata/runtime
        │
        ▼
 macOS-compatible runtime
        │
        ▼
 Windows
 ├── Win32
 ├── Direct3D
 └── DirectWrite
```

Filesystem images are handled separately before the application bundle reaches the runtime:

```text
DMG
 │
 ├── UDIF
 │    ├── Trailer
 │    ├── XML property list
 │    └── blkx extents
 │
 └── Filesystem
      ├── HFS+
      └── APFS
             │
             ▼
          .app bundle
```

## Implemented

### Mach-O

Mac&Cheese currently contains a native Mach-O parser supporting the structures needed for further loading work.

Implemented/recognized structures include:

* 64-bit Mach-O
* x86_64
* ARM64 identification
* FAT / universal binaries
* load commands
* segments
* sections
* dynamic library dependencies
* `@rpath` information
* symbol tables
* rebase information
* bind information
* weak bind information
* lazy bind information
* chained fixups metadata

### Dynamic Libraries

`DylibRegistry` provides the initial foundation for dynamic library loading and symbol resolution.

It handles concepts such as:

* image registration
* install-name resolution
* `@rpath`
* search paths
* library ordinals
* symbol resolution

### Native Image Loader

The loader builds a `LoadPlan` from a Mach-O image and provides mapped-image handling.

The current implementation includes:

* segment mapping
* load bias calculation
* image address ranges
* entry-point information
* rebasing metadata
* binding metadata
* chained-fixup metadata
* dependency tracking

### Objective-C Runtime

Mac&Cheese contains an initial Objective-C runtime representation.

The current implementation includes:

* selectors
* classes
* superclass relationships
* methods
* categories
* protocols
* Objective-C type-encoding classification
* discovery of Objective-C metadata from mapped Mach-O sections

Actual method/IMP registration and complete Objective-C ABI compatibility are still under development.

### macOS Application Bundles

The bundle inspector understands the basic structure of a macOS `.app` bundle, including:

```text
Application.app/
└── Contents/
    ├── Info.plist
    ├── MacOS/
    ├── Frameworks/
    └── PlugIns/
```

It can locate the application executable and associated framework/plugin files.

### DMG

Mac&Cheese also contains a native DMG/UDIF reader.

The implementation currently handles:

* UDIF trailer detection
* DMG metadata
* XML property-list data
* `blkx` block maps
* logical sector mapping
* raw/zero extents
* zlib-compressed extents
* filesystem probing

This work originally became necessary while obtaining actual macOS application bundles for testing.

### HFS+

HFS+ support has progressed beyond simple filesystem detection.

The implementation currently includes:

* HFS+ volume header parsing
* allocation block information
* catalog B-tree discovery
* catalog leaf nodes
* file records
* folder records
* path reconstruction
* file extents
* file extraction
* volume extraction

This allows Mac&Cheese to recover actual files from supported DMG images, including application bundles.

### APFS

APFS support is currently at the probing stage.

The implementation can investigate:

* GPT partition maps
* APFS partition candidates
* APFS container `NXSB`
* APFS block size

Full APFS filesystem extraction is not implemented yet.

## Project Status

Mac&Cheese is **not finished**.

The project is currently building the lower layers required before a real macOS application can execute successfully.

The current direction is approximately:

```text
[Done / In Progress]

DMG / UDIF
    ↓
HFS+
    ↓
.app bundle
    ↓
Mach-O
    ↓
dyld / dylibs
    ↓
image loader
    ↓
Objective-C runtime
    ↓
Darwin / POSIX
    ↓
CoreFoundation / Foundation
    ↓
graphics / text frameworks
    ↓
AppKit
    ↓
Windows backend
    ↓
macOS application execution
```

Not every layer shown above is implemented yet.

## Design Principles

### Native

Mac&Cheese is written as a native Windows C++ project.

The project does not depend on a web application layer for its runtime.

### Reconstruct the behavior, not the appearance

Mac&Cheese does not attempt to make Windows *look* like macOS.

Instead, it studies the structures and behaviors that macOS applications depend on and reconstructs those requirements in a Windows environment.

### Progressive implementation

The project is intentionally built layer by layer.

When one layer exposes another dependency, that dependency becomes the next implementation target.

## Build

Mac&Cheese currently targets:

* Windows 10 / 11
* x86-64
* C++17
* MSVC
* CMake

The repository includes a Windows build script:

```powershell
.\build-windows.ps1
```

The project produces components including:

```text
mnc_core.lib
mnc-run.exe
mnc-inspect.exe
mnc-tests.exe
```

## Tools

### `mnc-inspect`

Inspect a Mach-O executable or application bundle.

```text
mnc-inspect <Mach-O-file|Application.app>
```

### `mnc-run`

The runtime entry point for preparing and eventually launching macOS applications.

### Tests

The project uses CTest for automated tests.

## What Mac&Cheese Is Not

Mac&Cheese is not:

* macOS
* a macOS virtual machine
* Hackintosh
* a full Apple hardware emulator
* a macOS installer
* a `.app` to `.exe` converter
* a web-based macOS simulator

It is a **compatibility framework/runtime**.

## Name

The name is intentionally a wordplay:

**Mac + Cheese = Mac&Cheese**

The project name is not intended to describe the underlying architecture.

## License

Mac&Cheese is distributed under the **Mozilla Public License 2.0 (MPL-2.0)**.

See [`LICENSE`](LICENSE) for the complete license text.

## Development

Mac&Cheese is an ongoing experimental project.

The implementation is expected to change substantially as additional parts of the macOS application environment are reconstructed.

The repository represents the actual implementation and current development state of the project.

---

**Mac&Cheese**

*macOS applications, reconstructed for Windows.*
