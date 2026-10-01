# Changelog

## 0.2.0 — 2026-10-02

- Windows-native Mach-O load plan과 `VirtualAlloc` segment mapping을 추가했습니다.
- legacy dyld rebase opcode 처리를 추가했습니다.
- x86_64 `LC_DYLD_CHAINED_FIXUPS` starts/rebase chain 해석을 추가했습니다.
- chained external bind는 resolver가 준비될 때까지 명시적으로 차단합니다.
- dylib registry와 `@rpath`/`@loader_path`/`@executable_path` 경로 후보 해석을 통합했습니다.
- 제한된 Objective-C metadata scanner와 type encoding 분류를 추가했습니다.
- `build-windows.ps1 -Clean` 전체 캐시 정리 옵션을 추가했습니다.
- Visual Studio Build Tools fallback 탐색을 추가했습니다.
- CMake FetchContent zlib timestamp 경고를 정리했습니다.
- 현재 Windows Release 빌드에서 CTest 2/2 통과를 확인했습니다.

## 실행 상태

최신 ChatGPTInstaller 계열 Mach-O는 segment mapping과 chained rebase 단계까지 도달합니다. 외부 dylib chained bind, Swift/Foundation/AppKit runtime이 아직 필요하므로 실제 앱 실행은 차단됩니다.
