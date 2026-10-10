# Changelog

## 0.2.0 — 2026-10-02

- Windows-native Mach-O load plan과 `VirtualAlloc` segment mapping을 추가했습니다.
- legacy dyld rebase opcode 처리를 추가했습니다.
- x86_64 `LC_DYLD_CHAINED_FIXUPS` starts/rebase chain 해석을 추가했습니다.
- chained external import table과 symbol 문자열 해석을 추가했습니다.
- chained/legacy bind 결과를 symbol별로 진단하고, 미해결 required symbol은 명시적으로 차단합니다.
- 미해결 weak import는 dyld 의미론에 맞춰 null pointer로 처리합니다.
- dylib registry와 `@rpath`/`@loader_path`/`@executable_path` 경로 후보 해석을 통합했습니다.
- 제한된 Objective-C metadata scanner와 type encoding 분류를 추가했습니다.
- `build-windows.ps1 -Clean` 전체 캐시 정리 옵션을 추가했습니다.
- Visual Studio Build Tools fallback 탐색을 추가했습니다.
- CMake FetchContent zlib timestamp 경고를 정리했습니다.
- 현재 Windows Release 빌드에서 CTest 2/2 통과를 확인했습니다.

## 실행 상태

최신 ChatGPTInstaller 계열 Mach-O는 segment mapping과 chained import 해석 단계까지 도달합니다. 외부 dylib symbol registry에 제공자가 등록되지 않은 경우, symbol 이름과 library ordinal을 포함한 진단으로 실제 앱 실행을 차단합니다.
