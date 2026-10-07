# ADR-0009. Windows 툴체인을 MSVC로

- 상태: **Accepted** · 날짜: 2026-10-05
- 관련: [15-BUILD](../15-BUILD.md)

## 맥락

RTS는 CLion 번들 MinGW(g++)로 빌드했고, 코드와 무관한 간헐 컴파일 실패(임시 파일 경쟁)를 `-pipe`·재시도 스크립트로
우회해 왔습니다. 새 프로젝트는 D3D12 Debug Layer, PIX, DXC, D3D12MA, WinPixEventRuntime을 쓰며 이 도구들의 1급 지원 환경은 MSVC입니다.

## 결정

Windows는 MSVC(최신 Visual Studio 툴셋) 또는 clang-cl. MinGW는 지원하지 않습니다.
CLion을 쓰는 경우 "Visual Studio" 툴체인 프로필을 사용합니다. 구성·빌드는 `CMakePresets.json`으로만.

## 근거

```text
- Windows SDK 의 D3D12/DXGI 헤더·라이브러리, PIX 이벤트 런타임, 디버그 심볼(PDB)이 MSVC 기준.
- RTS 의 MinGW 간헐 실패를 새 프로젝트로 가져오지 않는다.
- C++23 지원 수준이 세 툴체인(MSVC, Clang, Apple Clang) 공통 부분집합으로 관리 가능.
```

## 결과

- 얻는 것: GPU 디버깅 도구 체인, 안정 빌드.
- 포기하는 것: RTS 개발 환경 재사용.
- 위험: 일부 C++23 기능의 MSVC/Clang 차이 → 12-CODING-STANDARDS 1장의 "주의" 목록.

## 대안

| 대안          | 기각 사유                      |
|---------------|--------------------------------|
| MinGW 유지    | 도구 지원·간헐 실패            |
| clang-cl 전용 | 허용(대안 프리셋). 기본은 MSVC |

## 재검토 조건

없음.
