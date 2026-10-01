# Mac&Cheese 기술 조사 및 구현 기준

## 결론

Mac&Cheese는 WSL을 실행하는 래퍼가 아니라 Windows-native 호환성 서브시스템으로 구현해야 한다. 이 목표는 기술적으로 가능하지만, 단일 로더로 해결되지 않는다. 디스크 이미지, 파일시스템, Mach-O 및 dyld, Darwin ABI, Objective-C와 Swift runtime, Foundation과 AppKit을 서로 분리된 계층으로 구현하고 실제 앱에서 관찰되는 요구사항을 기준으로 확장해야 한다.

Apple의 공식 문서는 Objective-C runtime을 동적 객체 언어의 기반으로 설명하고, Core Foundation을 컬렉션, 문자열, URL, property list, run loop, stream, socket 같은 기본 서비스를 제공하는 계층으로 정의한다.[1] [2] Swift 5 이후 ABI 안정성은 Swift runtime이 대상 운영체제의 구성요소가 된다는 뜻이지, 다른 운영체제에서 Swift runtime을 자동으로 사용할 수 있다는 뜻은 아니다.[3]

## DMG와 파일시스템

DMG는 UDIF 컨테이너다. 일반적인 읽기 경로는 trailer의 `koly` signature를 확인하고, trailer가 가리키는 resource fork와 block map을 해석한 뒤, 압축 또는 암호화된 섹터를 논리 디스크 블록으로 변환하는 순서다. 그 위에 HFS+ 또는 APFS reader를 둬야 `.app`을 찾을 수 있다.

이번 구현에서는 512바이트 trailer의 version, header size, data fork, resource fork, XML offset/length, sector count를 big-endian 필드로 읽고 이미지 범위를 검증한다. UDIF trailer 필드 배치는 공개 포맷 역공학 자료의 구조 정의와 일치시키되, 해당 자료 자체의 라이선스가 Mac&Cheese 코드에 전이된다고 가정하지 않는다.[8]

공개 `libdmg-hfsplus`는 DMG와 HFS+를 함께 다루는 참고 구현이지만 GPL-3.0이며, 자체 README도 실험적 코드라고 명시한다.[4] 따라서 Mac&Cheese에 코드를 복사하는 대신 포맷 구조와 테스트 전략을 참고하고, 배포 가능한 구현은 독립적으로 작성하거나 라이선스를 분리해야 한다.

APFS는 macOS High Sierra 이후 기본 파일시스템이며 cloning, snapshots, space sharing, sparse files 같은 기능을 제공한다.[5] 공개 `go-apfs`는 Windows와 Linux에서 마운트나 커널 드라이버 없이 APFS 구조를 읽는 것을 목표로 하는 MIT 프로젝트다.[6] 이는 Mac&Cheese가 채택할 수 있는 독립적인 read-only parser 설계의 유용한 참고점이다. 단, APFS 암호화 콘텐츠는 키가 없으면 복호화할 수 없으므로 “모든 DMG 읽기”를 보장하는 방식으로 보고해서는 안 된다.

## Mach-O와 dyld

Mach-O 실행에는 헤더만 읽는 것으로 충분하지 않다. 최소 loader는 segments와 sections를 주소 공간에 배치하고, load commands에서 의존성을 수집하며, dyld 정보에서 rebasing과 binding을 수행해야 한다. 최신 바이너리는 전통적인 bind/rebase opcode 외에 chained fixups와 exports trie를 사용할 수 있으므로 loader 설계에 별도 parser가 필요하다.

Apple의 공개 dyld 저장소는 실제 loader 구조와 테스트를 참고할 수 있지만 Apple Public Source License 2.0이 적용된다.[7] 따라서 구조와 알고리즘을 연구하되, Mac&Cheese core에 직접 포함할 때는 라이선스 의무를 별도로 검토해야 한다. 현재 프로젝트의 Mach-O parser는 이 계층의 진입점일 뿐이며, 실행 가능 상태를 의미하지 않는다.

## Darwin, Objective-C, Swift

Mac&Cheese는 Windows `HANDLE`을 macOS 앱에 직접 노출하지 않고, 자체 FD table과 process/thread/memory abstraction을 제공해야 한다. 이는 현재 구현된 `mnc::darwin::FileDescriptorTable`의 방향과 일치한다. 다음 단계에서는 파일 FD뿐 아니라 pipe, socket, timer, process 상태를 같은 의미론 계층에 통합해야 한다.

Objective-C runtime은 class, selector, method lookup, message dispatch, object allocation, metadata, autorelease를 제공해야 한다.[1] Core Foundation은 CFArray, CFDictionary, CFString, CFURL, CFBundle, CFRunLoop, CFSocket 등 앱이 자주 사용하는 기본 표면을 포함한다.[2] Swift ABI 안정성은 호출 규약과 runtime metadata를 안정화하지만, Foundation과 Objective-C bridge까지 자동으로 제공하지는 않는다.[3]

## 구현 순서

첫째, DMG reader는 trailer와 block map을 읽고, 압축되지 않은 UDIF와 zlib 계열 블록을 논리 블록 reader로 노출해야 한다. 둘째, HFS+ reader와 APFS read-only reader는 공통 `ImageFileSystem` 인터페이스를 구현하고, path lookup과 regular-file stream을 제공해야 한다. 셋째, bundle loader는 외부 디렉터리와 이미지 내부 경로를 같은 namespace로 취급해야 한다.

그 다음 Mach-O loader는 Windows `VirtualAlloc` 기반 segment mapping과 load-command 검증을 구현한다. 이 단계에서 실행하지 못하는 경우에는 필요한 relocation, import, architecture, protection 상태를 진단해야 한다. 이후 Objective-C runtime과 Core Foundation의 작은 수직 슬라이스를 만들어 실제 테스트 앱을 통과시키는 방식으로 Foundation과 AppKit을 확장한다.

## 라이선스와 진실성 기준

공개 구현은 포맷·ABI·테스트 참고 자료로 사용할 수 있지만, GPL 또는 Apple Public Source License 코드를 무심코 복사해서는 안 된다. 구현된 기능과 진단 전용 기능은 API와 CLI에서 명확히 구분한다. DMG signature를 찾았다는 사실은 DMG 내부 앱을 실행할 수 있다는 뜻이 아니며, Mach-O CPU를 읽었다는 사실은 dyld 초기화가 되었다는 뜻이 아니다.

Mac&Cheese의 배포판은 Wine처럼 지원 범위를 명시할 수 있다. 공개 API를 사용하는 비암호화 앱과 read-only DMG부터 지원하고, APFS 암호화, private framework, entitlement, DRM, 하드웨어 종속 기능은 별도의 제한으로 보고한다.

## References

[1]: https://developer.apple.com/documentation/objectivec "Objective-C Runtime — Apple Developer Documentation"
[2]: https://developer.apple.com/documentation/corefoundation "Core Foundation — Apple Developer Documentation"
[3]: https://swift.org/blog/abi-stability-and-apple/ "Evolving Swift On Apple Platforms After ABI Stability — Swift.org"
[4]: https://github.com/planetbeing/libdmg-hfsplus "planetbeing/libdmg-hfsplus — DMG and HFS+ implementation"
[5]: https://developer.apple.com/documentation/foundation/about-apple-file-system "About Apple File System — Apple Developer Documentation"
[6]: https://github.com/deploymenttheory/go-apfs "deploymenttheory/go-apfs — read-only APFS tools"
[7]: https://github.com/opensource-apple/dyld "opensource-apple/dyld — Apple open-source dynamic loader"
[8]: https://www.mothersruin.com/software/Archaeology/reverse/udif.html "UDIF-format Disk Images — Archaeology"
