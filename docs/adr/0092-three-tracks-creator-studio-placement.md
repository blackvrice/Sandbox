# ADR-0092. 세 트랙 (Engine Runtime · Creator Studio · Platform Services) 과 Studio 기능 배치 — v0.5 에 CLI 없는 기본 흐름

- 상태: **Accepted — 일부 Superseded** (버전 배치 — 버전 이름 유지 · M6 를 v0.5 에 — 트랙은 유지 → [ADR-0096](0096-roadmap-versioning-small-milestones.md)) · 날짜: 2026-11-03
- 관련: [23-ROADMAP](../23-ROADMAP.md), [ADR-0078](0078-v05-creator-alpha-scope-expansion.md) (v0.5 범위 — 이 ADR 이 M6 에 태스크를 더한다),
  [ADR-0082](0082-v06-v10-feature-placement.md) · [ADR-0088](0088-v06-v10-placement-update-runtime-v2.md) (배치 — 유지), [ADR-0089](0089-studio-editor-services-layer-imgui-first.md) ~
  [ADR-0091](0091-script-language-service-in-sandboxscript.md), [28-CREATOR-UX-GATES](../28-CREATOR-UX-GATES.md)

## 맥락

v0.5 Creator Alpha 의 완료 기준 C1 (A 제작 → B 실행) 과 새 알파 목표 (CLI 없이) 를 감사로 대조하니 (audit/ALPHA-READINESS) 홈 · 빌드 · 에셋 ·
자동 저장 · 자동 완성이 v0.5 계획에 없거나 CLI 에 기대고 있었다. 요청은 v0.5 를 "Technical Alpha / Creator 기반", v0.6 을 "Closed Creator Alpha"
로 다시 이름 붙이는 안을 냈다.

## 결정

```text
1. 작업을 세 트랙으로 나눈다 — A Engine Runtime (22 · ECS · VM · 물리 · 네트워크 · 렌더 · 저장 · 결정론), B Creator Studio (25 ~ 28),
   C Platform Services (Hub · 계정 · 배포 · 마켓 · 호스팅 · 운영). 트랙마다 병렬로 갈 수 있고, 맞닿는 계약 (명령 · 형식 · 패키지) 만 순서를 맞춘다
2. 버전 이름 · 핵심 목표는 그대로 (v0.5 Creator Alpha · v0.6 Hub · v0.7 Multiplayer · v0.8 Marketplace & Hosting · v0.9 Business & Stability ·
   v1.0 GA — ADR-0039 · 0082). 요청의 이름 (Technical Alpha · Closed Creator Alpha) 은 트랙 B 의 단계 이름으로 쓴다: v0.5 = 트랙 B "기본 흐름
   (Technical Alpha)", v0.6 = 트랙 B "Closed Creator Alpha" — 버전 이름을 바꾸면 릴리스 노트 · 문서 · ADR 의 참조가 모두 바뀌는데 얻는 것이 없다
3. v0.5 M6 에 P0 · P1 태스크 넷을 더하고 하나를 넓힌다: T6 레이아웃 · Studio 설정, T7 자동 저장 · 복구, T8 스크립트 편집기 자동
   완성 · hover · 저장 위치 구분 (오버레이 · 팩 파일), T9 프로젝트 에셋 · .meta · 가져오기 · .sbpkg 형식 3 (C1 차단 해소). 기존 M6-T3
   ("패키지" 패널 — 검사 · 시험 · 빌드) 에 누락 에셋 검사 · Build 화면을 더한다 (중복 태스크를 만들지 않는다). 기존 M6-T1 (홈) 은 P0
4. v0.6 트랙 B (Closed Creator Alpha): Editor Services 층 · Command 팔레트 · Undo 통합 · Asset Browser 완성 · UI Designer · Prefab 오버라이드 ·
   그룹 · 재생 디버거 · 언어 서비스 색인 · 서버 + N 테스트 · 지연 모의 · 데이터 폼 · 트리거 그래프 · 제작자 사용성 시험. 트랙 C (Hub) 와 병렬
5. v0.6 이 넘치면 트랙 B 의 P2 부터 v0.7 로 (23 8장) — Hub P0 (D-06 · 레지스트리) 는 밀지 않는다
6. 3D 는 v1.x 결정 D-16 (비용 · 의존 · 근거 — 지금 2D API 를 깨지 않게: 좌표는 Vec2, 3D 는 새 컴포넌트로 더하기만)
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | 버전 목표 유지 + 트랙 · 단계 이름 | 문서 · 릴리스 참조 안정, Studio 일이 Hub 일과 병렬 |
| B | 요청대로 버전 이름 변경 (v0.5 Technical Alpha · v0.6 Closed Creator Alpha) | v0.6 Hub · v0.7 Multiplayer 의 목표가 밀리거나 이름이 둘이 된다 |
| C | Studio 를 v0.6 으로 전부 | v0.5 C1 · 새 알파 목표 (CLI 없이) 를 v0.5 가 못 지킨다 |

## 결과

- v0.5 범위가 커진다 (M6 +4, T3 확장) — 미룰 순서는 23 8장 (T6 · T8 의 고급 부분 먼저 밀고, T1 · T3 · T7 · T9 는 밀지 않는다 — C1 · 데이터 손실).
- 20 10장 · 18 6.6 · 21 1장 · 24 (P5 ~ P8 · D3) 갱신.
