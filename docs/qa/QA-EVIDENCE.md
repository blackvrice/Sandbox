# QA 증거 양식 (층 4 — 실기 QA)

> [28-CREATOR-UX-GATES](../28-CREATOR-UX-GATES.md) 3.1 층 4 · [ADR-0120](../adr/0120-studio-test-layers-gui-smoke-gated.md). [MANUAL-QA](MANUAL-QA.md) 의 줄을 [x] 로
> 바꿀 때 아래 한 줄을 남긴다. 헤드리스 · CI 통과는 여기 적지 않는다 (그건 CTest 기록).

## 양식

```text
날짜 | 항목 (MANUAL-QA 의 줄) | 커밋 | 명령 | OS · 빌드 | GPU · 렌더 백엔드 | 해상도 · 배율 | 결과 (통과 · 실패 · 부분) | 본 것 · 남은 것
```

## 기록

```text
2026-11-10 | v0.8 UI-1 셸 1366×768 | f0eb6b7 전 작업 트리 | SandboxClient --world <프로젝트> --width 1366 --height 768 | Windows 11 26320 · Debug |
            D3D12 (하드웨어) | 1366×768 · 100 % | 부분 | 메뉴 · 툴바 · 상태 막대 · 도크 (목록 · 인스펙터 · 편집 · 아래) · 모드 띠, 월드 선택 35 개 =
            목록 · 인스펙터, 끝낸 뒤 default.ini 도킹 11 창. 메뉴 클릭 · 다시 열기 · 1920×1080 · 배율은 하지 못함 (다른 전체 화면 창)
```
