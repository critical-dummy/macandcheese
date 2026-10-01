# Mac&Cheese

Mac&Cheese는 Windows에서 macOS 애플리케이션이 기대하는 실행 환경을 재구현하는 **Windows-native compatibility framework/runtime**입니다. macOS 자체, 가상 머신, WSL, Hackintosh가 아닙니다.

## 0.2.0 현재 구현

이번 릴리즈는 정적 분석/추출 단계에서 Windows-native Mach-O loading 준비 단계로 전진했습니다.

- 64-bit little-endian Mach-O 및 FAT/universal 이미지 파싱
- x86_64/arm64 slice 식별과 의존성·rpath 분석
- UDIF/DMG, zlib 압축 extent, HFS+ 탐색·추출
- Windows HANDLE 기반 Darwin/POSIX 정수 FD 테이블
- Mach-O load plan과 `LC_MAIN` entry 계산
- Windows `VirtualAlloc` 기반 segment mapping
- dyld legacy rebase opcode 해석
- x86_64 `LC_DYLD_CHAINED_FIXUPS` starts/rebase chain 해석
- chained external bind가 필요한 경우 명시적 차단
- dylib registry, `@rpath`, `@loader_path`, `@executable_path` 후보 해석
- 제한된 Objective-C selector/class/category/protocol metadata 분석
- 지원하지 않는 기능을 성공으로 가장하지 않는 진단 출력

현재 ChatGPTInstaller 같은 최신 앱은 segment mapping과 chained rebase chain까지 진행되지만, `libSystem`, Objective-C, Swift, Foundation, AppKit 등의 **chained external symbol binding**이 필요하므로 실행은 아직 차단됩니다.

## 빌드

Windows 10/11에서 PowerShell로:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build-windows.ps1 -Clean
```

일반적인 반복 빌드:

```powershell
.\build-windows.ps1
```

스크립트는 Visual Studio 18 2026/Visual Studio 17 2022, MinGW Makefiles, Ninja를 자동 탐색하고 Release 빌드와 CTest를 실행합니다.

## 사용

```powershell
.\build\Release\mnc-inspect.exe "Application.app"
.\build\Release\mnc-run.exe --diagnose "Application.app"
.\build\Release\mnc-run.exe --diagnose "disk.dmg"
.\build\Release\mnc-run.exe --extract "disk.dmg" "C:\Temp\extracted"
```

진단 명령은 아직 구현되지 않은 기능을 명확히 보고합니다. `--diagnose`가 성공했다고 해서 앱 코드가 실행되었다는 뜻은 아닙니다.

## 현재 차단 지점

```text
DMG/HFS+ 읽기                         구현
Mach-O segment mapping                구현
legacy dyld rebase                    구현
x86_64 chained rebase                 구현
chained external bind                 진행 중
Objective-C full objc_msgSend ABI    진행 중
Swift runtime                         미구현
Foundation/AppKit Windows backend     미구현
실제 macOS 앱 lifecycle 실행          차단
```

Mac&Cheese의 최종 목표는 `mnc-inspect`가 아니라, macOS 앱이 기대하는 ABI/API와 의미론을 유지하면서 Windows-native backend 위에서 앱 lifecycle을 유지하는 subsystem입니다.

## 라이선스

MIT License
