# 버전별 목표 · 태스크 · 기준 (v0.5 ~ v0.25+ · v1.0.0, 2026-11-03, ADR-0096 · ADR-0099)

> **계획 문서.** 버전마다 사용자에게 보이는 목표 하나, 그 버전의 태스크 (상태 · 선행 · 완료 기준은 [task-registry](task-registry.md)),
> 출시 기준 ([release-gates](release-gates.md) 의 그 버전 행). 버전 번호는 [versioning](versioning.md) — v0.10 은 v0.9 다음이다.
> 트랙 A (Engine Runtime) · B (Creator Studio) · C (Platform Services) 는 서로 기다리지 않는다 — 한 트랙이 늦으면 그 트랙의 태스크만 다음
> 버전으로 옮긴다 (등록부의 목표 칸을 고치고, 원래 칸은 그대로). v0.22 ~ v0.24 · GA 의 검증 계획은 [stabilization](stabilization.md).
> **"태스크" 줄이 그 버전의 배정이다** — 끝나지 않은 태스크는 정확히 한 버전의 "태스크" 줄에 있고 그 버전이 등록부 목표와 같다 (check_docs R).
> 미완료 태스크를 남긴 채 버전이 끝났다고 하지 않는다 — 옮길 때는 새 목표를 등록부에 적는다.

---

## v0.5 — Technical Alpha: 엔진 핵심 · CLI 로 만든 게임이 다른 폴더에서 같은 결과 ✅ 0.5.0 ([노트](../releases/v0.5.0.md))

```text
단계      Technical Alpha (versioning 2장) — 내부 개발자 · CLI 허용
목표      A 가 sbx 로 만든 엔진 에셋 게임을 B 가 다른 폴더 · 빈 캐시에서 열어 같은 자동 플레이 해시를 얻는다. 충돌 보고 · 진단이 남는다
끝난 것   M1 (진단 · 배포) · M2 (게임플레이 API · Runtime v2 계약) · M8 (표준 라이브러리) · NG-11 (문서 검사기) — 등록부 Done
태스크    M7-T6 ✅ Technical Alpha A → B (CTest tech_alpha_e2e) · M7-T5 ✅ 버전 종료 0.5.0
빠진 것   제작자 GUI 흐름 (28 G1 ~ G9) · 게임 UI · 자기 PNG — Closed Creator Alpha (v0.13) 몫
```

## v0.6 — 에셋 · 패키지 · SaveGame 기반 ✅ 0.6.0 ([노트](../releases/v0.6.0.md))

```text
목표      자기 PNG 를 넣은 게임이 다른 폴더 · 빈 캐시에서 보인다. 같은 프로젝트 → 같은 패키지 digest. 에셋 id · 경로 충돌은 조용히 넘기지 않는다.
          게임 버전이 바뀐 세이브를 옮기는 계약
태스크    NG-12 ✅ 에셋 id 충돌 · NG-13 ✅ 경로 정책 · M6-T9 ✅ 프로젝트 에셋 (.meta · .sbpkg 3) · NG-01 ✅ 빌드 스냅숏 · NG-02 ✅ Asset Group ·
          NG-14 ✅ 해시 · digest 분리 · NG-08 ✅ Export Profile (Windows) · RT-06-12 ✅ 비동기 로드 · NG-04 ✅ SaveGame 마이그레이션 계약
```

## v0.7 — 게임 UI Runtime · 다국어 기반

```text
목표      게임이 HUD · 메뉴 · 버튼을 띄우고, 버튼이 서버가 판정하는 신호를 보낸다. 문자열 · 폰트 · 키 · 배율을 바꿀 수 있다
태스크    M3-T1 ✅ · M3-T2 ✅ · M3-T3 ✅ · M3-T4 ✅ · M3-T5 ✅ · M3-T6 ✅ · NG-06 입력 컨트롤 · M4-T1 ✅ · M4-T2 · M4-T3 · M4-T4
```

## v0.8 — Studio 기본 환경: 홈 · 빌드 화면 · 복구 · 테스트 실행

```text
목표      CLI 없이 프로젝트를 만들고 · 열고 · 빌드하고 · 서버 + N 클라이언트로 돌린다. 충돌해도 작업을 되찾는다
태스크    ST-06-1 Editor Services · M6-T1 홈 · M6-T3 빌드 화면 · M6-T6 레이아웃 · M6-T7 자동 저장 · ST-06-2 · ST-06-9 TestService ·
          ST-06-11 · M5-T1 · M5-T2 · M5-T3 리플레이 · NG-07 Studio 시험 층 · NG-03 Player 모드
```

## v0.9 — Script IDE

```text
목표      Studio 안에서 스크립트를 고치고 자동 완성 · hover · 오류 줄 · 재생 디버거로 고친다. 한글 입력 · 커서 위치가 맞다
태스크    NG-05 텍스트 위치 계약 · M6-T4 줄 표 (.sbc 11) · M6-T8 편집기 · ST-06-7 재생 디버거 · ST-06-8 언어 서비스 색인
```

## v0.10 — Creator Foundation

```text
목표      Studio 에서 게임을 만드는 핵심 도구 (에셋 · UI 디자이너 · 템플릿) 와 스크립트 Runtime API (Capability 실행 검사 · client 수명주기 ·
          onAction · 타이머 · defineState) 를 갖춘다. 고급 물리 · 충돌 · 애니메이션은 요구하지 않는다
태스크    ST-06-3 Asset Browser · ST-06-4 Game UI Designer · M6-T2 템플릿 3종 · RT-06-1 Capability 실행 검사 · RT-06-7 client 수명주기 · 이벤트 ·
          RT-06-8 onAction · 입력 Context · RT-06-10 타이머 · defineState · NG-18 통합 시험
완료 기준 1 Studio 에서 프로젝트 생성 · 열기 · 저장 (v0.8 회귀)            2 프로젝트 에셋 관리 (가져오기 · 옮기기 · 참조 · 지우기 경고)
          3 UI Designer 로 HUD · 버튼 · 메뉴                               4 UI 이벤트 → 스크립트 연결 (신호 · ctx.ui)
          5 client 입력과 서버 권위 시뮬레이션의 책임 분리 (client 는 신호만) 6 타이머 · 상태가 세이브 → 로드 · 리플레이 해시를 지킨다
          7 변조 .sbc 가 Capability 를 우회하지 못한다 (호출 토큰 — 24 V3)   8 템플릿마다 new → check → build → 자동 플레이 통과
통합 시험 NG-18 — 버튼 · 오목형 게임: 생성 → UI 편집 → 스크립트 → 실행 → 저장 → 재실행 → 패키지 검증
```

## v0.11 — Gameplay Runtime

```text
목표      물리 단계 콜백 · PhysicsCommand · 충돌 · 트리거 이벤트 · onRender Draw API · 기본 오디오 출력
태스크    RT-06-2 server onPrePhysics · RT-06-3 PhysicsCommand · RT-06-4 collider layer · mask · trigger · RT-06-5 충돌 · 트리거 Enter/Exit ·
          RT-06-9 onRender · Draw API · NG-17 오디오 출력 백엔드 · NG-19 통합 시험
완료 기준 1 물리 단계 (onPrePhysics → 물리 → 충돌 이벤트) 구분 · 문서화      2 PhysicsCommand 적용 순서 결정론 (해시)
          3 충돌 · 트리거 이벤트 순서 안정 (saveId 순 · 세이브 정합)         4 onRender 가 권위 ECS 를 바꾸지 못함 (Capability · 24 C1)
          5 Draw 명령 수 · 메모리 상한 (1,024 — 넘으면 거절 · 셈)            6 오디오 출력이 세 OS 에서 동작 (수동 QA 기록)
          7 헤드리스 서버는 오디오 장치 없이 동작 (CTest)                    8 충돌 이벤트 폭주에서 서버 안정 (큐 상한 · 24 Q1)
통합 시험 NG-19 — 충돌 · 트리거 · 오디오 미니게임, 서버 · 클라이언트 · 렌더 · 스크립트의 책임 · 스레드 경계 확인
```

## v0.12 — Advanced Creator Tools

```text
목표      다양한 2D 게임 — Prefab 오버라이드 · 그룹 · 데이터 폼 · 타일맵 · 애니메이션 · 고급 입력 · 믹서
태스크    ST-06-5 Prefab Override · ST-06-6 Hierarchy 검색 · 그룹 · ST-06-10 데이터 폼 · 트리거 그래프 · NG-15 Tilemap · RT-06-6 ShapeCast ·
          RT-06-11 AccessSet · 프로파일 · RT-06-13 게임패드 · Combo · RT-06-14 애니메이션 상태 기계 · RT-06-15 Audio Mixer · 2D 공간 음향 ·
          NG-20 통합 시험
우선순위  P0 NG-20 · P1 ST-06-5 · NG-15 · RT-06-14 (통합 시험이 쓴다 — 2026-11-03 P2 → P1) 먼저. P2 (ST-06-6 · ST-06-10 · RT-06-6 · RT-06-11 ·
          RT-06-13 · RT-06-15) 는 v0.12 시작 때 필요성을 판정 — 미루면 등록부의 목표를 v0.14 이후로 고치고 이 절에서 뺀다 (v0.13 을 막지 않게)
완료 기준 1 Prefab 오버라이드 저장 · 다시 열기                           2 에셋 · Prefab 참조가 옮기기 · 이름 바꾸기 뒤에도 그대로
          3 Tilemap 생성 · 편집 · 저장 · 빌드                             4 데이터 폼이 실제 콘텐츠 스키마와 왕복
          5 애니메이션 상태 전이 안정 (해시)                               6 고급 입력이 기존 입력 · UI 소비 규칙과 충돌하지 않음
          7 Mixer 가 v0.11 오디오 백엔드를 씀                              8 세이브 · 리플레이 호환 (옛 세이브 로드 · 마이그레이션)
통합 시험 NG-20 — 가져오기 → Prefab → 맵 편집 → 스크립트 → 애니메이션 → 빌드 → 실행 → 저장 · 로드
```

## v0.13 — Closed Creator Alpha

```text
단계      Closed Creator Alpha — 초대한 외부 제작자 (진입은 release-gates 3장)
목표      외부 제작자가 Studio 만으로 (CLI 없이) 실제 게임을 만들어 다른 PC 의 친구가 실행한다
원칙      새 핵심 기능을 만들지 않는다 — 통합 · 버그 · 사용성 · 데이터 복구 · 안정성. 기능이 모자라면 앞 버전 태스크로 돌려보낸다
태스크    M7-T1 omok · M7-T2 hud_guard · M7-T3 A → B E2E · M6-T5 튜토리얼 · ST-06-12 사용성 시험 · M7-T4 Alpha 채널 · 차단 목록
필수 검증 프로젝트 생성 · 에셋 가져오기 · 월드 편집 · 스크립트 · 자동 완성 · UI 제작 · 테스트 플레이 · 디버깅 · 패키징 · 다른 PC 실행 ·
          자동 저장 · 복구 · 외부 제작자 사용성 시험 · Studio 접근성 (28 G1 ~ G10)
```

## v0.14 — 계정 · 레지스트리 · 서명

```text
목표      계정으로 로그인하고, 서명된 불변 릴리스를 레지스트리에 올리고 받는다 (서비스 저장소 — D-06)
태스크    PS-12-1 저장소 경계 · PS-12-2 계정 · 인증 · PS-12-3 레지스트리 · 서명 · PS-12-4 검색 · PS-12-5 신고 · 검토
```

## v0.15 — Public Creator Alpha: 게시 · 설치 · 출시 채널

```text
단계      Public Creator Alpha (진입은 release-gates 3장)
목표      Studio 에서 게시 → Player 가 설치 · 업데이트 · 되돌리기. 채널 (Alpha · Beta · Stable) · 단계 배포 · 롤백
태스크    PS-13-1 게시 · PS-13-2 설치 · 업데이트 · NG-09 출시 채널 · 롤백
```

## v0.16 — 멀티플레이 · 방

```text
목표      인증된 플레이어가 보안 전송으로 방에 들어가고, 자체 서버를 등록하고, 무료 세션을 받는다
태스크    PS-14-1 보안 전송 (D-07) · PS-14-2 방 · JoinTicket · PS-14-3 · PS-14-4 · PS-14-5 · PS-14-6 · PS-14-7
```

## v0.17 — 예측 · Closed Beta

```text
단계      Closed Beta (진입은 release-gates 3장)
태스크    RT-07-1 · RT-07-2 · RT-07-3 · RT-07-4 · RT-07-5 · RT-07-6 · RT-07-7 (network.model "predicted" · client 고정 tick · InputFrame · 예측 · 시간 동기)
```

## v0.18 — 호스팅 · NetworkModule · 스트리밍

```text
태스크    PS-16-1 상시 호스팅 (D-09) · RT-08-1 NetworkModule (X1 ~ X5) · RT-08-2 · RT-08-3 · RT-08-4 · RT-08-6
```

## v0.19 — 결제 (테스트 모드) · 에셋 마켓

```text
태스크    PS-17-1 Entitlement · 결제 대행 · PS-17-2 에셋 마켓 · 라이선스
```

## v0.20 — 정산 · 분석 · 등급전

```text
태스크    PS-18-1 정산 장부 · PS-18-2 분석 · PS-18-3 등급전 · 순위표 · 업적 · PS-18-4 추천 · 리플레이 공유
```

## v0.21 — 팀 · 협업 · 플러그인

```text
단계      Public Beta 후보 (release-gates 3장으로 판정 — 번호로 정하지 않는다)
태스크    PS-19-1 팀 ACL · CI/CD · ST-07-1 초안 · ST-07-2 문서 동시 편집 · ST-07-3 sbx lsp · NG-10 Editor Plugin API v1 · PS-19-2 도구 다국어
```

## v0.22 — Reliability & Performance

```text
목표      장시간 실행 · 메모리 · 스레드 안정 · 결정론 · 성능 기준선 (stabilization 1장)
태스크    RH-22-1 서버 Soak · RH-22-2 Studio Soak · RH-22-3 Sanitizer 매트릭스 · RH-22-4 누수 · 종료 경합 · RH-22-5 성능 기준선 ·
          RH-22-6 네트워크 조건 · RH-22-7 장기 · 교차 툴체인 결정론 · RT-09-1 성능 회귀 CI · RT-09-2 권장 사양 · PS-20-2 운영 콘솔 ·
          RT-09-3 · RT-09-4 · RT-09-5 · RT-08-5 (Review — 측정이 예산 초과를 보일 때만, 아니면 v1.x 로)
```

## v0.23 — Security & Release Hardening

```text
목표      스크립트 · 사용자 콘텐츠 · 네트워크 · 서버 · 계정 · 공급망 · 업데이트 보안, 데이터 복구 · 운영, 실기 하드웨어 (stabilization 2 · 3장)
태스크    RH-23-1 퍼징 · RH-23-2 샌드박스 탈출 시험 · RH-23-3 네트워크 · 서버 · 계정 보안 · RH-23-4 공급망 · RH-23-5 복구 훈련 ·
          RH-23-6 운영 런북 (Missing) · RH-23-7 실기 매트릭스 · RH-23-8 개인정보 정책 (Missing) · PS-20-1 서명 업데이트 · PS-20-3 감사 준비 ·
          NG-16 1.0 전환 정책
```

## v0.24 — Release Candidate

```text
목표      기능 동결 RC 를 전체 게이트로 재검증한다 (stabilization 4장)
태스크    RH-24-1 RC 정책 · RH-24-2 RC 전체 회귀 · RH-24-3 외부 감사 조치 · RH-24-4 문서 · 지원 정책
```

## v0.25 — 추가 수정 · GA 후보 재검증 (필요할 때만)

```text
목표      v0.24 RC 가 GA 기준을 통과하지 못하면 차단 항목을 고쳐 RC 를 다시 낸다 (v0.25 · v0.26 …). 새 기능은 넣지 않는다
태스크    (없음 — RC 판정에서 나온 차단 항목을 그때 등록부에 더한다)
```

## v1.0 — General Availability

```text
목표      release-gates 6장의 GA 차단 조건이 0 이고 승인 기록이 있는 빌드에 v1.0.0 을 붙인다 (날짜 · 직전 버전을 미리 정하지 않는다)
태스크    RH-24-5 GA 최종 승인 · v1.0.0 출시
```

## v1.x — GA 뒤

```text
3D (D-16) · 측정으로 미룬 병렬화 (RT-09-3 ~ 5 · RT-08-5 가 옮겨 오면) 등
```
