# 태스크 등록부 (2026-11-03, ADR-0096)

> **목표 버전 · 상태의 정본.** 태스크 ID 는 바꾸지 않는다 — 목표 버전이 옮겨져도 `RT-06-3` 은 `RT-06-3` 이다 (원래 버전 칸이 이력).
> 자세한 카드 (모듈 · API · 형식 · 결정론 · 보안 · 시험) 는 [20](../20-CREATOR-ALPHA.md) 10장 (M1 ~ M8), [23](../23-ROADMAP.md) (RT · ST · v0.5 카드),
> 새 태스크 (NG · PS) 는 [releases](releases.md) 의 그 버전 절. 상태: **Done** (코드 · 시험 근거), **Planned** (계획 — 카드 있음),
> **Proposed** (2차 감사가 더한 것 — 이 커밋에서 계획에 받아들임), **Review** (측정 조건을 채워야 시작). 트랙: A Engine Runtime · B Creator
> Studio · C Platform Services. P: P0 출시 차단 · 호환 계약, P1 버전 목표의 핵심, P2 미룰 수 있음, P3 측정 · 수요가 있으면.
> tools/check_docs.py (CTest docs_consistency) 가 계획 문서의 ID 가 여기 있는지 · 목표 버전이 [버전 목록](versioning.md) 에 있는지 본다.

## 1. 등록부

| ID | 제목 | 트랙 | 상태 | 원래 | 목표 | P | 선행 | 완료 기준 (요지) |
|----|------|------|------|------|------|---|------|------------------|
| M1-T1 | 충돌 처리 · 로그 고리 · 보고 묶음 · 민감정보 제거 | A | Done | v0.5 | v0.5 | P0 | — | CTest crash_report_server · client |
| M1-T2 | sbx doctor · sbx report · 콜스택 · 문맥 | A | Done | v0.5 | v0.5 | P0 | M1-T1 | CTest project_doctor |
| M1-T3 | 사용자 설정 · 안전 모드 · 새 버전 알림 | A | Done | v0.5 | v0.5 | P1 | — | CTest client_safe_mode |
| M1-T4 | CI 녹색 · 배포 묶음 · 릴리스 피드 URL | A | Done | v0.5 | v0.5 | P0 | — | CI 다섯 잡 · CTest release_bundle |
| M2-T1 | 엔진 컴포넌트 접근 표 · 읽기 | A | Done | v0.5 | v0.5 | P0 | — | test_engine_components |
| M2-T2 | 엔진 컴포넌트 쓰기 규칙 | A | Done | v0.5 | v0.5 | P0 | M2-T1 | test_engine_components |
| M2-T3 | spawn · destroy · add · remove · 상한 | A | Done | v0.5 | v0.5 | P0 | M2-T2 | test_script_structural |
| M2-T4 | random · near · time | A | Done | v0.5 | v0.5 | P0 | M2-T1 | test_script_random |
| M2-T5 | 플레이어 · 세션 · owner · action · signal (조각 1 ~ 4b) | A | Done | v0.5 | v0.5 | P0 | M2-T3 | test_player_signal · test_player_table · test_script_callbacks |
| M2-T6 | client 입력 · send · 소리 이벤트 · D-14 | A | Done | v0.5 | v0.5 | P0 | M2-T5 · M2-T7 | test_client_scripts · test_client_script_session |
| M2-T7 | Runtime v2 계약 동결 (콜백 표 · capability · network.model) | A | Done | v0.5 | v0.5 | P0 | M2-T5 | test_runtime_contract |
| M8-T1 | 표준 라이브러리 분석 · 매트릭스 · ADR-0085 | A | Done | v0.5 | v0.5 | P1 | — | 19 25장 |
| M8-T2 | 내장 표 · INTR · Array 메서드 | A | Done | v0.5 | v0.5 | P1 | M8-T1 | test_stdlib_array |
| M8-T3 | 함수 참조 · 화살표 · 콜백 메서드 | A | Done | v0.5 | v0.5 | P1 | M8-T2 | test_stdlib_callbacks |
| M8-T4 | Map · Set | A | Done | v0.5 | v0.5 | P1 | M8-T3 | test_stdlib_collections |
| M8-T5 | Stack · Queue · Deque · PriorityQueue · Heap · BitSet | A | Done | v0.5 | v0.5 | P1 | M8-T4 | test_stdlib_seq |
| M8-T6 | 알고리즘 · 수학 · vec2 | A | Done | v0.5 | v0.5 | P1 | M8-T2 | test_stdlib_algo |
| M8-T7 | Graph | A | Done | v0.5 | v0.5 | P1 | M8-T5 | test_stdlib_graph |
| M8-T8 | 엔진 질의 · 경로 요청 · raycast · overlap | A | Done | v0.5 | v0.5 | P1 | M8-T2 | test_script_engine_queries |
| M8-T9 | 예제 stdlib_tour · 벤치 · 안내 | A | Done | v0.5 | v0.5 | P1 | M8-T8 | CTest sample_stdlib_tour |
| NG-11 | 문서 · 로드맵 일관성 검사 (링크 · ADR · 태스크 ID · 목표 버전 · 상태) | A | Done | — | v0.5 | P1 | — | CTest docs_consistency |
| M7-T6 | Technical Alpha A → B — CLI 빌드 · 엔진 에셋 게임 · 다른 폴더 · 빈 캐시 | A | Proposed | — | v0.5 | P0 | M2-T7 | CTest: 패키지 → 다른 폴더 · 빈 캐시 → 같은 자동 플레이 해시 |
| M7-T5 | 버전 종료 0.5.0 (Technical Alpha) | A | Planned | v0.5 | v0.5 | P0 | M7-T6 | 릴리스 노트 · releases v0.5 기준 판정 |
| M6-T9 | 프로젝트 에셋 · .meta 안정 id · PNG 가져오기 · .sbpkg 3 · 루트 차례 | B | Planned | v0.5 | v0.6 | P0 | NG-12 · NG-13 | 자기 PNG 게임이 다른 폴더 · 빈 캐시에서 보인다 |
| NG-12 | 에셋 id 충돌 정책 (같은 id · 다른 내용 = 오류, 복제 = 새 id) | B | Proposed | — | v0.6 | P0 | — | 충돌 픽스처 4 경우 시험 |
| NG-13 | 경로 정책 (대소문자 · Unicode NFC · 예약 이름 · 링크 · 길이 · 탈출) | B | Proposed | — | v0.6 | P0 | — | 경로 픽스처 시험 (세 OS 규칙을 한 검사로) |
| NG-01 | 빌드 스냅숏 일관성 (리비전 고정 · 바뀜 감지 · 재현 · 정리) | B | Proposed | — | v0.6 | P0 | M6-T9 | 빌드 중 소스 수정 시험 · 같은 입력 = 같은 digest |
| NG-02 | 동적 에셋 참조 — Asset Group · 라벨 (정적 참조 + 선언) | B | Proposed | — | v0.6 | P1 | M6-T9 | 그룹 포함 · 누락 경고 · 크기 보고 시험 |
| NG-14 | 시뮬레이션 해시 · 표시 digest 분리 (핸드셰이크 경고) | A | Proposed | — | v0.6 | P1 | M6-T9 | 다른 PNG · 같은 규칙 = 접속 + 경고, 다른 규칙 = 거절 |
| NG-08 | Export Profile (대상 OS · 아키텍처 · 백엔드 · 검증 — Windows 먼저) | B | Proposed | — | v0.6 | P1 | M6-T9 | 프로필 검증 · 미검증 플랫폼 표시 시험 |
| NG-04 | SaveGame 마이그레이션 계약 (게임 버전 · 체인 · 백업 · 실패 복구) | A | Proposed | — | v0.6 | P1 | — | 옛 세이브 → 체인 → 같은 해시 · 실패 = 백업 그대로 |
| RT-06-12 | 비동기 에셋 로드 · 캐시 | A | Planned | v0.6 | v0.6 | P2 | M6-T9 | 로드 순서 무관 표시 |
| M3-T1 | ui/ 타깃 (SandboxGameUI) · 문서 모델 · 검사 U1 ~ U8 | A | Planned | v0.5 | v0.7 | P0 | — | 20 10장 Phase 3 |
| M3-T2 | 배치 엔진 (헤드리스 골든) | A | Planned | v0.5 | v0.7 | P0 | M3-T1 | 20 10장 |
| M3-T3 | ImGui 그리기 목록 백엔드 · 입력 라우팅 | A | Planned | v0.5 | v0.7 | P0 | M3-T2 | 20 10장 |
| M3-T4 | 바인딩 · ctx.ui | A | Planned | v0.5 | v0.7 | P0 | M3-T3 | 20 10장 |
| M3-T5 | 이벤트 (버튼 → 신호) | A | Planned | v0.5 | v0.7 | P0 | M3-T4 | 20 10장 (신호는 M2-T5 ✅) |
| M3-T6 | 스타일 · 테마 · 배율 | A | Planned | v0.5 | v0.7 | P1 | M3-T3 | 20 10장 |
| NG-06 | 게임 UI 입력 컨트롤 (TextInput · Slider · Checkbox · Dropdown · Scroll · 포커스 · 게임패드 · IME) | A | Proposed | — | v0.7 | P1 | M3-T3 | 컨트롤마다 상태 · 이벤트 · 포커스 시험 |
| M4-T1 | 문자열 표 · 로케일 대체 | A | Planned | v0.5 | v0.7 | P1 | — | 20 10장 |
| M4-T2 | 폰트 대체 | A | Planned | v0.5 | v0.7 | P1 | M4-T1 | 20 10장 |
| M4-T3 | 키 다시 묶기 | A | Planned | v0.5 | v0.7 | P1 | — | 20 10장 |
| M4-T4 | UI 배율 | A | Planned | v0.5 | v0.7 | P1 | M3-T6 | 20 10장 |
| ST-06-1 | Editor Services 층 · Command · Undo 통합 · check_includes 규칙 | B | Planned | v0.6 | v0.8 | P0 | — | 규칙 시험 · Undo 왕복 |
| M6-T1 | Studio 홈 · 새 프로젝트 · 열기 · 최근 | B | Planned | v0.5 | v0.8 | P0 | ST-06-1 | CTest studio_new_project |
| M6-T3 | "패키지" 패널 · Build 화면 · 누락 검사 | B | Planned | v0.5 | v0.8 | P0 | NG-01 · ST-06-1 | CTest studio_build |
| M6-T6 | 레이아웃 저장 · Studio 설정 | B | Planned | v0.5 | v0.8 | P1 | — | 경로 · 깨진 파일 시험 |
| M6-T7 | 자동 저장 · 크래시 복구 | B | Planned | v0.5 | v0.8 | P0 | — | CTest studio_recovery |
| ST-06-2 | 명령 팔레트 · 단축키 표 · 테마 | B | Planned | v0.6 | v0.8 | P2 | ST-06-1 | 충돌 진단 시험 |
| ST-06-9 | TestService — 서버 + N 클라이언트 · 지연 모의 | B | Planned | v0.6 | v0.8 | P1 | — | CTest 서버 + 2 헤드리스 |
| ST-06-11 | Scene/Game 뷰 분리 · 떼어 낸 창 | B | Planned | v0.6 | v0.8 | P3 | ST-06-1 | 수동 QA |
| M5-T1 | --replay · 로컬 재생 · 결속 | B | Planned | v0.5 | v0.8 | P1 | — | 20 10장 Phase 5 |
| M5-T2 | 재생 · 멈춤 · seek · 타임라인 | B | Planned | v0.5 | v0.8 | P1 | M5-T1 | 20 10장 |
| M5-T3 | --record · 보고 묶음에 | B | Planned | v0.5 | v0.8 | P2 | M5-T1 | 20 10장 |
| NG-07 | Studio 시험 층 (헤드리스 Studio · GUI smoke · E2E · 실기 QA 증거) | B | Proposed | — | v0.8 | P0 | ST-06-1 | 헤드리스 흐름 시험 하나 · QA 양식 |
| NG-03 | Player 모드 (패키지 열기 · 최근 게임 · 더블클릭 · 오류 화면) | B | Proposed | — | v0.8 | P1 | — | 헤드리스 Player 로 패키지 열기 시험 |
| M6-T4 | 오류 메시지 · 줄 표 (.sbc 10) · 함수별 프로파일 · IScriptExecutor | A | Planned | v0.5 | v0.9 | P0 | — | 20 10장 |
| M6-T8 | 스크립트 편집기 — 자동 완성 · hover · 밑줄 · 저장 위치 구분 | B | Planned | v0.5 | v0.9 | P0 | NG-05 · M6-T4 | 후보 · hover · 저장 왕복 |
| NG-05 | 텍스트 위치 계약 (UTF-8 바이트 · 코드 포인트 · UTF-16 · 화면 열 · IME) | B | Proposed | — | v0.9 | P0 | — | 위치 변환 · 한글 조합 시험 |
| ST-06-7 | 재생 디버거 | B | Planned | v0.6 | v0.9 | P1 | M6-T4 | 같은 해시 · 훅 비용 < 2 % |
| ST-06-8 | 언어 서비스 색인 · 이동 · 이름 바꾸기 · 빠른 수정 | B | Planned | v0.6 | v0.9 | P2 | M6-T8 | 응답 측정 |
| ST-06-3 | Asset Browser 완성 | B | Planned | v0.6 | v0.10 | P1 | M6-T9 | 참조 · 옮기기 · 지우기 시험 |
| ST-06-4 | Game UI Designer | B | Planned | v0.6 | v0.10 | P1 | M3-T5 · ST-06-1 | 문서 왕복 · 수동 QA |
| ST-06-5 | Prefab 인스턴스 오버라이드 · 변형 (설계 ADR 먼저) | B | Planned | v0.6 | v0.10 | P1 | NG-04 | det 시험 |
| ST-06-6 | 편집기 그룹 · Hierarchy 검색 | B | Planned | v0.6 | v0.10 | P2 | ST-06-1 | 10k 측정 |
| ST-06-10 | 데이터 폼 · 트리거 그래프 캔버스 | B | Planned | v0.6 | v0.10 | P2 | ST-06-1 | 스키마 → 폼 왕복 |
| M6-T2 | 템플릿 3종 | B | Planned | v0.5 | v0.10 | P0 | M3-T5 | 템플릿마다 new → check → test |
| RT-06-1 | 호출 토큰 · Capability 실행 검사 | A | Planned | v0.6 | v0.10 | P0 | M2-T7 | 변조 .sbc 시험 |
| RT-06-2 | server onPrePhysics (8b) | A | Planned | v0.6 | v0.10 | P1 | RT-06-1 | 단계 순서 · 해시 |
| RT-06-3 | PhysicsCommand | A | Planned | v0.6 | v0.10 | P1 | RT-06-2 | 적용 순서 · 해시 |
| RT-06-4 | collider layer · mask · trigger | A | Planned | v0.6 | v0.10 | P1 | — | 골든 그대로 |
| RT-06-5 | 충돌 · 트리거 Enter/Exit (16a) | A | Planned | v0.6 | v0.10 | P1 | RT-06-4 | 순서 · 세이브 정합 |
| RT-06-6 | ShapeCast | A | Planned | v0.6 | v0.10 | P2 | — | raycast 표 |
| RT-06-7 | client 수명주기 · onEvent · onLateUpdate | A | Planned | v0.6 | v0.10 | P1 | RT-06-1 | 순서 |
| RT-06-8 | onAction · 입력 Context | A | Planned | v0.6 | v0.10 | P1 | M3-T3 | 우선순위 · 소비 |
| RT-06-9 | onRender · Draw API | A | Planned | v0.6 | v0.10 | P1 | RT-06-7 | 상한 · 거절 |
| RT-06-10 | 타이머 · defineState | A | Planned | v0.6 | v0.10 | P1 | RT-06-1 | 세이브 → 로드 해시 |
| RT-06-11 | AccessSet 선언 · 단계 프로파일 | A | Planned | v0.6 | v0.10 | P2 | — | 선언 밖 단언 |
| RT-06-13 | 게임패드 · Hold · Double Tap · Combo | A | Planned | v0.6 | v0.10 | P2 | RT-06-8 | 트리거 판정 |
| RT-06-14 | 2D 애니메이션 클립 · 상태 기계 | A | Planned | v0.6 | v0.10 | P2 | RT-06-7 | 상태 전이 |
| RT-06-15 | 오디오 믹서 · 2D 위치 | A | Planned | v0.6 | v0.10 | P2 | NG-17 | — |
| NG-15 | 2D 타일맵 · 타일셋 (GM-39 — 스프라이트 타일 칠하기) | B | Proposed | — | v0.10 | P2 | M6-T9 | 칠하기 · 저장 왕복 · 해시 |
| NG-17 | 오디오 출력 백엔드 · sound importer · sounds.json (ADR-0095) | A | Proposed | — | v0.10 | P2 | M6-T9 | 세 OS 출력 수동 QA · 이벤트 → 소리 시험 |
| M7-T1 | samples/omok | B | Planned | v0.5 | v0.11 | P0 | M3-T5 · M6-T9 | 20 10장 Phase 7 |
| M7-T2 | samples/hud_guard | B | Planned | v0.5 | v0.11 | P0 | M3-T4 | 20 10장 |
| M7-T3 | A → B E2E (Studio 빌드 · 자기 에셋 · 진단) | B | Planned | v0.5 | v0.11 | P0 | M6-T3 · M6-T9 | 20 10장 + 툴체인 간 리플레이 (SG-06) |
| M7-T4 | Alpha 채널 · 외부 시험 · 차단 목록 | B | Planned | v0.5 | v0.11 | P0 | M7-T3 | 20 10장 |
| M6-T5 | 튜토리얼 | B | Planned | v0.5 | v0.11 | P1 | M6-T2 | 문서의 명령을 CTest 가 |
| ST-06-12 | 제작자 사용성 시험 (28 2장) | B | Planned | v0.6 | v0.11 | P0 | M7-T3 | 시험 기록 · 합격선 |
| PS-12-1 | 저장소 경계 D-06 (서비스 저장소 · 엔진 저장소) | C | Planned | v0.6 | v0.12 | P0 | — | ADR |
| PS-12-2 | 계정 · 인증 | C | Planned | v0.6 | v0.12 | P0 | PS-12-1 | 인증 위협 시험 (24) |
| PS-12-3 | 패키지 레지스트리 · 불변 릴리스 · 서명 | C | Planned | v0.6 | v0.12 | P0 | PS-12-2 | 24 P3 |
| PS-12-4 | 게임 검색 · 목록 · 프로필 | C | Planned | v0.6 | v0.12 | P1 | PS-12-3 | — |
| PS-12-5 | 패키지 · 게임 신고 · 차단 · 검토 큐 | C | Planned | v0.6 | v0.12 | P1 | PS-12-2 | — |
| PS-13-1 | Studio 에서 게시 (빌드 → 서명 → 레지스트리) | C | Proposed | — | v0.13 | P0 | PS-12-3 · M6-T3 | 게시 E2E |
| PS-13-2 | Player 설치 · 업데이트 · 무결성 · 삭제 | C | Proposed | — | v0.13 | P0 | NG-03 · PS-12-3 | 설치 · 손상 · 되돌리기 시험 |
| NG-09 | 출시 채널 (Alpha · Beta · Stable) · 단계 배포 · 롤백 · 세이브 호환 | C | Proposed | — | v0.13 | P0 | PS-13-1 · NG-04 | 롤백 · 실패 복구 시험 |
| PS-14-1 | 보안 전송 (D-07) | C | Planned | v0.7 | v0.14 | P0 | — | 24 N2 |
| PS-14-2 | Directory · Room · JoinTicket | C | Planned | v0.7 | v0.14 | P0 | PS-14-1 · PS-12-2 | — |
| PS-14-3 | 자체 서버 등록 · Heartbeat | C | Planned | v0.7 | v0.14 | P1 | PS-14-2 | — |
| PS-14-4 | 무료 세션 할당 · 할당량 | C | Planned | v0.7 | v0.14 | P1 | PS-14-2 | — |
| PS-14-5 | 플레이어 데이터 API (서버) | C | Planned | v0.7 | v0.14 | P1 | PS-12-2 | — |
| PS-14-6 | 친구 · 초대 · 파티 | C | Planned | v0.7 | v0.14 | P2 | PS-12-2 | — |
| PS-14-7 | 원격 관전 · 방 안 신고 · 음소거 | C | Planned | v0.7 | v0.14 | P2 | PS-14-2 | — |
| RT-07-1 | network.model "predicted" · net.predicted | A | Planned | v0.7 | v0.15 | P0 | M2-T7 | 값 · 거절 |
| RT-07-2 | Client 고정 Tick · client onTick | A | Planned | v0.7 | v0.15 | P1 | RT-07-1 · RT-06-7 | FPS 무관 횟수 |
| RT-07-3 | InputFrame · ack | A | Planned | v0.7 | v0.15 | P0 | RT-07-2 | 위조 tick |
| RT-07-4 | PredictedWorld · 맞춤 | A | Planned | v0.7 | v0.15 | P1 | RT-07-3 | 같은 입력 = 같은 예측 |
| RT-07-5 | 예측 계측 · 네트워크 프로파일 | A | Planned | v0.7 | v0.15 | P1 | RT-07-4 | 14 기록 |
| RT-07-6 | client onPrePhysics · onPostPhysics | A | Planned | v0.7 | v0.15 | P2 | RT-07-4 · RT-06-3 | — |
| RT-07-7 | 시간 동기 | A | Planned | v0.7 | v0.15 | P1 | RT-07-3 | 지터 시뮬레이션 |
| PS-16-1 | 상시 호스팅 (D-09) | C | Planned | v0.8 | v0.16 | P0 | PS-14-2 | 24 O3 |
| RT-08-1 | NetworkModule (서버만) | A | Planned | v0.8 | v0.16 | P1 | PS-16-1 · RT-06-10 | 24 X1 ~ X5 |
| RT-08-2 | Substep | A | Planned | v0.8 | v0.16 | P2 | RT-06-3 | 골든 그대로 |
| RT-08-3 | CCD · 재질 | A | Planned | v0.8 | v0.16 | P2 | RT-08-2 · RT-06-6 | — |
| RT-08-4 | 에셋 스트리밍 · 메모리 예산 | A | Planned | v0.8 | v0.16 | P1 | RT-06-12 | 메모리 상한 |
| RT-08-6 | Worker 스케줄 · 렌더 CPU 프로파일 | A | Planned | v0.8 | v0.16 | P2 | RT-06-11 | 14 기록 |
| PS-17-1 | Entitlement · 결제 대행 (테스트 모드) | C | Planned | v0.8 | v0.17 | P0 | PS-12-2 | 24 O4 |
| PS-17-2 | 에셋 마켓 · 라이선스 | C | Proposed | — | v0.17 | P1 | PS-17-1 · PS-12-3 | — |
| PS-18-1 | 정산 장부 | C | Planned | v0.8 | v0.18 | P0 | PS-17-1 | — |
| PS-18-2 | 플레이 분석 (동의) · 제작자 대시보드 | C | Planned | v0.8 | v0.18 | P1 | PS-12-2 | — |
| PS-18-3 | 등급전 · 레이팅 · 순위표 · 업적 · 통계 API (ADR-0088) | C | Planned | v0.8 | v0.18 | P1 | PS-14-2 | — |
| PS-18-4 | 추천 · 리플레이 공유 | C | Planned | v0.8 | v0.18 | P2 | PS-12-4 | — |
| PS-19-1 | 팀 ACL · CI/CD · 승인 · 감사 (D-10) | C | Planned | v0.9 | v0.19 | P0 | PS-12-2 | — |
| ST-07-1 | 스크립트 초안 · 커밋 | B | Planned | v0.7 | v0.19 | P1 | ST-06-1 | — |
| ST-07-2 | 문서 동시 편집 (D-17 — 잠금 · 3-way) | B | Planned | v0.7 | v0.19 | P1 | ST-06-1 | — |
| ST-07-3 | sbx lsp | B | Planned | v0.7 | v0.19 | P2 | ST-06-8 | LSP 왕복 |
| NG-10 | Editor Plugin API v1 (매니페스트 · 확장 지점 · 권한 · 격리 · 지원 중단) | B | Proposed | — | v0.19 | P2 | ST-06-1 | 24 P5 · 호환 검사 시험 |
| PS-19-2 | 도구 다국어 | C | Planned | v0.9 | v0.19 | P2 | M4-T1 | — |
| RT-09-1 | 성능 회귀 CI | A | Planned | v0.9 | v0.20 | P0 | — | PR 마다 핵심 벤치 |
| RT-09-2 | 권장 사양 자동 분석 | A | Planned | v0.9 | v0.20 | P1 | RT-09-1 | 매니페스트 recommended |
| RT-09-3 | ECS 단계 안 병렬 | A | Review | v0.9 | v0.20 | P3 | RT-06-11 | Worker 0 · 1 · N 해시 |
| RT-09-4 | 스크립트 Worker | A | Review | v0.9 | v0.20 | P3 | — | — |
| RT-09-5 | 잠 (sleep) | A | Review | v0.9 | v0.20 | P3 | — | 같은 해시 |
| RT-08-5 | Render 스레드 분리 | A | Review | v0.8 | v0.20 | P3 | 측정 | CPU 렌더 > 6 ms |
| PS-20-1 | 서명 자동 업데이트 · 되돌리기 | C | Planned | v0.9 | v0.20 | P0 | PS-12-3 | 24 O2 |
| PS-20-2 | 운영 콘솔 | C | Planned | v0.9 | v0.20 | P1 | PS-16-1 | — |
| PS-20-3 | 외부 보안 감사 준비 · 위협 표 전부 재확인 | C | Proposed | — | v0.20 | P0 | — | 24 전체 |
| NG-16 | GA 전환 — 0.x 프로젝트의 engine 범위 (<1.0.0) 를 1.0 이 여는 정책 · 도구 | A | Proposed | — | v0.20 | P0 | — | 0.x 프로젝트를 1.0 으로 열기 시험 |

## 2. 옛 번호 · 바뀐 번호

같은 ID 를 다른 뜻으로 다시 쓰지 않는다 — 2026-11-03 Studio 감사 첫 판의 번호를 고칠 때 한 번 어겼다 (아래). ADR 은 고치지 않으므로 여기서 읽는다.

| 옛 번호 | 나오는 곳 | 지금 | 메모 |
|---------|-----------|------|------|
| M6-T10 | ADR-0090 결과 줄 | M6-T9 | 감사 첫 판 번호 |
| M6-T7 (ADR-0090 의 "누락 검사") | ADR-0090 결과 줄 | M6-T3 | 감사 첫 판에서 GUI 빌드였다 — 지금 M6-T7 은 자동 저장 (ID 재사용 — 다시 하지 않는다) |
| M6-T9 (ADR-0091 의 "자동 완성") | ADR-0091 결과 줄 | M6-T8 | 같은 이유 — 지금 M6-T9 는 프로젝트 에셋 |
| M3-T7 | 18 6.6 · 20 10장 | ST-06-4 | "에디터 UI 미리보기 · 편집" 을 UI Designer 로 합침 |
