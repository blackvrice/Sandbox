# 버전별 목표 · 태스크 · 기준 (v0.5 ~ v0.21 · GA, 2026-11-03, ADR-0096)

> **계획 문서.** 버전마다 사용자에게 보이는 목표 하나, 그 버전의 태스크 (상태 · 선행 · 완료 기준은 [task-registry](task-registry.md)),
> 출시 기준 ([release-gates](release-gates.md) 의 그 버전 열). 버전 번호는 [versioning](versioning.md) — v0.10 은 v0.9 다음이다.
> 트랙 A (Engine Runtime) · B (Creator Studio) · C (Platform Services) 는 서로 기다리지 않는다 — 한 버전에 세 트랙의 일이 같이 있을 수 있고,
> 한 트랙이 늦으면 그 트랙의 태스크만 다음 버전으로 옮긴다 (등록부의 목표 칸을 고치고, 원래 칸은 그대로).
> 새 태스크 (NG · PS-13 · PS-17-2 · PS-20-3 · M7-T6) 의 카드는 이 문서와 [2차 감사](../audit/SECOND-PASS-GAP-ANALYSIS.md) 4장의 설계.

---

## v0.5 — Technical Alpha: 엔진 핵심 · CLI 로 만든 게임이 다른 폴더에서 같은 결과

```text
단계      Technical Alpha (versioning 2장) — 내부 개발자 · CLI 허용
목표      A 가 sbx 로 만든 엔진 에셋 게임을 B 가 다른 폴더 · 빈 캐시에서 열어 같은 자동 플레이 해시를 얻는다. 충돌 보고 · 진단이 남는다
끝난 것   M1 (진단 · 배포) · M2 (게임플레이 API · Runtime v2 계약) · M8 (표준 라이브러리) — 등록부 Done
남은 것   M7-T6 Technical Alpha A → B (CTest) → M7-T5 버전 종료 0.5.0
빠진 것   제작자 GUI 흐름 (28 G1 ~ G9) · 게임 UI · 자기 PNG — Closed Creator Alpha (v0.11) 몫. v0.5 에 "Creator Alpha" 라는 이름을 쓰지 않는다
기준      release-gates 2장 v0.5 열
```

## v0.6 — 프로젝트 에셋 · 패키지 무결성

```text
목표      자기 PNG 를 넣은 게임이 다른 폴더 · 빈 캐시에서 보인다. 같은 프로젝트 → 같은 패키지 digest. 에셋 id · 경로 충돌은 조용히 넘기지 않는다
태스크    NG-12 에셋 id 충돌 · NG-13 경로 정책 → M6-T9 프로젝트 에셋 (.meta · .sbpkg 3) → NG-01 빌드 스냅숏 · NG-02 Asset Group ·
          NG-14 해시 · digest 분리 · NG-08 Export Profile (Windows) · RT-06-12 비동기 로드
          NG-04 SaveGame 마이그레이션 계약 (트랙 A — 에셋과 독립, 같은 버전에 둔 것은 세이브 형식을 바꾸는 v0.10 RT-06-10 앞이라서)
기준      release-gates 2장 v0.6 열 (P3 이전 · 데이터 무결성)
```

## v0.7 — 게임 UI 런타임 · 다국어 기반

```text
목표      게임이 HUD · 메뉴 · 버튼을 띄우고, 버튼이 서버가 판정하는 신호를 보낸다. 문자열 · 폰트 · 키 · 배율을 바꿀 수 있다
태스크    M3-T1 ~ T6 (ui/ · 배치 · 백엔드 · 바인딩 · 이벤트 · 스타일) · NG-06 입력 컨트롤 · M4-T1 ~ T4
기준      release-gates 2장 v0.7 열
```

## v0.8 — Studio 기반: 홈 · 빌드 화면 · 복구 · 테스트 실행

```text
목표      CLI 없이 프로젝트를 만들고 · 열고 · 빌드하고 · 서버 + N 클라이언트로 돌린다. 충돌해도 작업을 되찾는다
태스크    ST-06-1 Editor Services → M6-T1 홈 · M6-T3 빌드 화면 · M6-T6 레이아웃 · M6-T7 자동 저장 · ST-06-2 · ST-06-9 TestService ·
          ST-06-11 · M5-T1 ~ T3 리플레이 · NG-07 Studio 시험 층 · NG-03 Player 모드
기준      release-gates 2장 v0.8 열 (Studio Document 버전 1 — versioning 5장)
```

## v0.9 — 스크립트 IDE

```text
목표      Studio 안에서 스크립트를 고치고 자동 완성 · hover · 오류 줄 · 재생 디버거로 고친다. 한글 입력 · 커서 위치가 맞다
태스크    NG-05 텍스트 위치 계약 → M6-T4 줄 표 (.sbc 10) → M6-T8 편집기 → ST-06-7 재생 디버거 · ST-06-8 언어 서비스 색인
기준      release-gates 2장 v0.9 열
```

## v0.10 — 콘텐츠 도구 · Runtime v2 실행

```text
목표      Asset Browser · UI 디자이너 · Prefab 오버라이드 · 데이터 폼 · 템플릿, 그리고 물리 단계 · 충돌 이벤트 · 타이머 · client 수명주기
태스크    트랙 B: ST-06-3 · ST-06-4 · ST-06-5 · ST-06-6 · ST-06-10 · M6-T2
          트랙 A: RT-06-1 (Capability 실행 검사 — 먼저) → RT-06-2 ~ RT-06-11 · RT-06-13 · RT-06-14 · RT-06-15 · NG-17 오디오 출력
나눌 때   트랙 A 가 늦으면 RT-06-2 ~ RT-06-15 를 v0.11 로 (RT-06-1 은 남긴다 — V3 실행 검사가 Closed Alpha 의 보안 기준)
기준      release-gates 2장 v0.10 열
```

## v0.11 — Closed Creator Alpha

```text
단계      Closed Creator Alpha — 초대한 외부 제작자
목표      외부 제작자가 Studio 만으로 (CLI 없이) 오목 · HUD 데모 수준의 게임을 만들어 다른 PC 의 친구가 실행한다 — 옛 v0.5 의 목표
태스크    M7-T1 omok · M7-T2 hud_guard · M7-T3 A → B E2E · M6-T5 튜토리얼 · ST-06-12 사용성 시험 · M7-T4 Alpha 채널 · 차단 목록
기준      release-gates 2장 v0.11 열 + 3장 Closed Creator Alpha 진입 (28 G1 ~ G9 · 20 13장)
```

## v0.12 — Hub 기반: 계정 · 레지스트리

```text
목표      계정으로 로그인하고, 서명된 불변 릴리스를 레지스트리에 올리고 받는다 (서비스 저장소 — D-06)
태스크    PS-12-1 저장소 경계 → PS-12-2 계정 · 인증 → PS-12-3 레지스트리 · 서명 · PS-12-4 검색 · PS-12-5 신고 · 검토
기준      release-gates 2장 v0.12 열 (인증 · P3)
```

## v0.13 — Public Creator Alpha: 게시 · 설치 · 출시 채널

```text
단계      Public Creator Alpha — 누구나 만들고 공유
목표      Studio 에서 게시 → Player 가 설치 · 업데이트 · 되돌리기. 채널 (Alpha · Beta · Stable) · 단계 배포 · 롤백
태스크    PS-13-1 게시 · PS-13-2 설치 · 업데이트 · NG-09 출시 채널 · 롤백
기준      release-gates 2장 v0.13 열 + 3장 Public Creator Alpha 진입
```

## v0.14 — 인증된 멀티플레이 · 방

```text
목표      인증된 플레이어가 보안 전송으로 방에 들어가고, 자체 서버를 등록하고, 무료 세션을 받는다
태스크    PS-14-1 보안 전송 (D-07) → PS-14-2 방 · JoinTicket → PS-14-3 ~ PS-14-7
기준      release-gates 2장 v0.14 열 (N2 — 인터넷 방은 보안 전송 없이 열지 않는다)
```

## v0.15 — 예측 (Closed Beta 시작)

```text
단계      Closed Beta
태스크    RT-07-1 ~ RT-07-7 (network.model "predicted" · client 고정 tick · InputFrame · PredictedWorld · 시간 동기)
기준      release-gates 2장 v0.15 열
```

## v0.16 — 호스팅 · NetworkModule · 스트리밍

```text
태스크    PS-16-1 상시 호스팅 (D-09) · RT-08-1 NetworkModule (X1 ~ X5) · RT-08-2 · RT-08-3 · RT-08-4 · RT-08-6
기준      release-gates 2장 v0.16 열 (O3 · X1 ~ X5)
```

## v0.17 — 결제 (테스트 모드) · 에셋 마켓

```text
태스크    PS-17-1 Entitlement · 결제 대행 · PS-17-2 에셋 마켓 · 라이선스
기준      release-gates 2장 v0.17 열 (O4 — 테스트 모드만)
```

## v0.18 — 정산 · 분석 · 등급전

```text
태스크    PS-18-1 정산 장부 · PS-18-2 분석 · PS-18-3 등급전 · 순위표 · 업적 · PS-18-4 추천 · 리플레이 공유
기준      release-gates 2장 v0.18 열
```

## v0.19 — 팀 · 협업 · 확장

```text
단계      Public Beta (v0.19 부터 — 성숙 단계는 기준으로 정한다)
태스크    PS-19-1 팀 ACL · CI/CD · ST-07-1 초안 · ST-07-2 문서 동시 편집 · ST-07-3 sbx lsp · NG-10 Editor Plugin API v1 · PS-19-2 도구 다국어
기준      release-gates 2장 v0.19 열 (P5 플러그인 권한)
```

## v0.20 — 안정화 · 성능 · 보안 감사 준비

```text
태스크    RT-09-1 성능 회귀 CI · RT-09-2 권장 사양 · PS-20-1 서명 자동 업데이트 · PS-20-2 운영 콘솔 · PS-20-3 보안 감사 준비 ·
          NG-16 1.0 전환 정책 · (측정되면) RT-09-3 · RT-09-4 · RT-09-5 · RT-08-5
기준      release-gates 2장 v0.20 열
```

## v0.21 이후 · GA

```text
v0.21     GA 후보 — release-gates 4장을 판정한다. 통과하지 못한 항목은 v0.21 · v0.22 … 의 태스크로 (번호를 아끼지 않는다)
v1.0.0    GA 기준을 모두 통과한 빌드에 붙인다 (날짜 · 직전 버전을 미리 정하지 않는다 — ADR-0096)
v1.x      3D (D-16) 등 GA 뒤의 것
```
