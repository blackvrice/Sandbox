# DEVELOPMENT_LOG

작업 단위의 **이력**입니다: 무엇을, 왜 바꿨고, 어떻게 검증했고, 무엇이 남았나.
"지금 어떤 상태이고 무엇을 지켜야 하나"는 `docs/`에 있습니다. 둘을 섞지 않습니다.

새 항목은 위에 추가합니다.

---

## 2026-10-27 — v0.3 M6-T1: 스크립트 데모 samples/mana_regen · E2E (ADR-0069)

**무엇을**

- 계획의 M6-T1 (데모 + 보호 시험 + 10k 벤치) 을 셋으로 나눴다 — 이번은 데모 E2E, T2 = 보호 시험 · 벤치, T3 = 버전 종료 (18 6.2).
- samples/mana_regen (게임 모드 없음): 생태계 팩 복사본 + scripts/ — mana.ts (컴포넌트 eco.mana {current · max · regen}),
  regen.ts (server — 15 틱마다 채움 · 10 초마다 로그), newborn.ts (server — spawn 이벤트 → 마나 50), hud.ts (client — 토끼 위 값 ·
  "스크립트" 칸). 토끼 Prefab 에 eco.mana, 행동 eco.energize (조건 target.eco.mana.current >= 30 → 마나 −30 · 기운 +30), 키 R.
  스크립트를 넣은 콘텐츠 루트로 `sbx new --from ecosystem_small --content-root` — 시작 월드의 토끼 75 마리가 마나 50.
  playthrough.json (8 배속 · 기운 주기 20 번 · 1800 틱 · expect tick · 툴체인별 해시), README.
- CTest sample_mana_regen: sample_playthrough.cmake 에 RESULT_MATCH · SERVER_MATCH · CLIENT_MATCH 매개변수와 샘플 이름의 패키지
  파일을 더해 같은 스크립트로 — 헤드리스 서버 = 로컬 플레이 (프로젝트 · 패키지), 서버 끝 줄의 스크립트 시스템 · 트랩 0, 클라이언트
  끝 줄의 client 시스템.
- unit_core test_mana_regen_sample 4: 열기 · 자동 플레이 두 번 같다 (기운 주기 받아들여진 수까지) · 세이브 → 로드 → 이어 돌린 해시 ·
  로컬 세션 (멈춘 뒤 복제 월드의 마나 바이트 = 서버, client 글자 수, Inspector 값 · ChangeComponent 편집).
- sbx check 의 스크립트 요약에 " · client onUpdate".
- 문서: ADR-0069, 18 (M6 셋으로 · T1 ✅ · 다음 M6-T2), 19 (0 · 23장), 13 5.1, 15 7장, 17, README.

**왜**

- v0.3 완료 기준 "스크립트로 Mana Component · Regeneration System" 을 사용자가 따라 할 예제와 자동 검증으로. 컴포넌트 · 주기 ·
  이벤트 · client · Rule 연결을 한 게임에서 다 쓴다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 63/63 통과 (+ sample_mana_regen 약 36 초, det_* 골든
  그대로). SandboxTests 595 케이스. check_includes 위반 0.
- 실행: `sbx check samples/mana_regen` 오류 0 · 경고 0 (시스템 3). `SandboxServer --world samples/mana_regen --script …` 와
  `SandboxClient --headless --world samples/mana_regen --script …` 둘 다 "게임 결과: 게임 없음 · tick 1801 · hash
  0x51ddbf1f7d1ab7e9", 서버 로그 "토끼 75 · 마나 합 6480" (t300) … "토끼 124 · 마나 합 12084" (t1800), 클라이언트 끝 줄
  "[eco.mana.hud] 토끼 124 · 마나 가득 108".
- 창 (ImGui) 에서 R 키 · 글자를 눈으로 보지는 못했다 — 행동 · 표시는 자동 플레이와 세션 시험이 본다.

**남은 일**

- M6-T2: 보호 시험 (무한 루프 · 권한 위반 · 불법 바이트코드 · 핸들 위조) · 10k 대상 벤치 (sbx_bench 스크립트 단계). M6-T3: 버전 종료.

---

## 2026-10-27 — v0.3 M5-T4: 스크립트 프로파일 — 주입한 진단 시계, ServerStats 스크립트 표, "스크립트 프로파일" 패널 (ADR-0068, 프로토콜 14)

**무엇을**

- core/script/ScriptClock (u64 (*)() noexcept — 단조 나노초): ScriptSystemAdapter · ClientScriptSystems 의 setClock,
  SimulationWorld::setScriptClock (다시 만든 어댑터에도). 시계가 있으면 호출마다 앞뒤를 재어 통계에만 (ScriptSystemCounters::nanos ·
  maxNanos, 틱 · 프레임 통계 nanos, 클라이언트 시스템별 지난 연료 · 시간). describeScriptSystems · describeClientScriptSystems 에
  " · 시간 <합> ms · 최대 <호출 하나> ms" (시계가 있을 때만 — sbx_sim_check 끝 줄은 그대로).
- ServerHost: 단계마다 그 월드에 steady_clock 시계를 넣고, ServerStats (1 초) 끝에 windowSeconds · 스크립트 시스템 표 (창의 호출 ·
  연료 · 시간 = 누적 차이, 누적 트랩 · 예산 · 건너뜀 · 최대 연료 · 최대 시간 · 꺼짐, 64 줄 · scriptsOmitted). 프로토콜 13 → 14.
- 클라이언트: IWorldSession::scriptProfile — 서버 표 + client 시스템의 최근 1 초. apps/client/ui/ScriptPanel 의 "스크립트 프로파일"
  패널 (서버 · 클라이언트 표 — 호출/초 · 연료/호출 · ms/초 · ms/호출 · 최대 ms · 최대 연료 · 트랩 · 예산 · 건너뜀 · 상태,
  scriptProfileRow). M5-T3 의 "스크립트" 칸 안 통계 접기는 이 패널로 옮겼다 (ScriptOverlayInfo::systems 없앰).
- sbx_net_probe: 받은 서버 스크립트 표를 줄마다.
- 시험: test_client_scripts +1 (시계 — 해시 그대로), test_client_script_session +1 (프로파일 세션 · 패널 · 줄 계산),
  test_messages +1 (표 왕복 · 상한), 프로토콜 14. 문서: ADR-0068, 08, 19 (0 · 9 · 19 · 21 · 22장), 18 (M5-T4 ✅ · M5 완료 · 다음
  M6-T1), 10 9장, 15, 17, 13.

**왜**

- 통계에 시간이 없어 "어느 스크립트가 느린가" 를 볼 수 없었고, 끝 줄로만 보였다. 게임을 돌리면서 서버 · 클라이언트 표를 본다.
- 04 (시뮬레이션은 벽시계를 보지 않는다) 를 약속이 아니라 경계로: core 는 시계를 모르고, 부르는 쪽이 넣은 값이 통계로만 흐른다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — kSimVersion 4).
  SandboxTests 591 케이스. check_includes 위반 0.
- 실행 (스크래치 clientroot — 생태계 + Mana 스크립트 + client 시스템 eco.hud):
  `SandboxServer --world ecosystem_small --content-root <clientroot> --port 7791 --ticks 240 --exit` 에
  `sbx_net_probe --connect 127.0.0.1:7791 --content-root <clientroot> --seconds 3` → 1 초마다 "스크립트 eco.mana.regen: 창 1.03 초
  호출 1 · 연료 4564 · 1.289 ms | … 최대 1.430 ms", 서버 끝 줄 "스크립트 eco.mana.regen: 호출 8 · … · 시간 10.68 ms · 최대 1.510 ms".
  `SandboxClient --headless … --frames 120` 끝 줄 "클라이언트 스크립트 eco.hud: 호출 116 · … · 시간 259.15 ms · 최대 6.030 ms"
  (Debug — 프레임 하나 약 2.2 ms, 글자 75).
- 성능: 시뮬레이션 경로는 호출마다 시계 포인터 확인 하나 (시계를 넣으면 steady_clock 두 번) — 벤치는 돌리지 않았다. sbx_bench
  스크립트 단계는 M6-T1 (저장소 콘텐츠에 스크립트가 아직 없다).
- 패널을 창에서 눈으로 보지는 못했다 — 그리기는 헤드리스 ImGui 시험이 부른다.

**남은 일**

- M6-T1: 데모 samples/mana_regen · 보호 시험 · 10k 대상 벤치 (스크립트 단계 — 시계를 넣어 시스템별 시간). 함수별 프로파일 · 기록
  그래프 · 패널에서 시스템 끄고 켜기 · Native 시스템별 시간 (ServerStats) 은 [계획].

---

## 2026-10-26 — v0.3 M5-T3: 클라이언트 스크립트 최소 — domain "client" · onUpdate, 복제 월드 읽기 · 화면 글자 (ADR-0067)

**무엇을**

- 계획의 M5-T3 (IClientContext 최소 + 프로파일) 을 둘로 나눴다 — 이번은 클라이언트 스크립트, 프로파일은 M5-T4 (18 6.2).
- Host 표: 주인 ClientContext · 도메인 Client 항목 8 — client.tick · frame · dt · read · has · query (복제 월드, netId 순) · label ·
  print (새 효과 display). 더하기만이라 kHostApiVersion 1 그대로, 지문 0388e85856c3e9f3 → e02f109682ecb304. AbiType::ClientContext
  를 검사기 · 코드 생성 · 검증기 · .sbc 읽기 · d.ts 에.
- 검사기: defineSystem({ domain: "client", onUpdate(ctx) }) — domain 을 먼저 읽어 (리터럴, 키 순서와 무관) 메서드의 ctx 를
  ClientContext 로. client 시스템의 onTick · onStart · onStop · onEvent · intervalTicks · events · eventsPerTick, server 시스템의
  onUpdate, 상수 이름 domain 은 S426 (한 잘못에 진단 하나).
- .sbc 형식 4 → 5: 시스템 표에 도메인 (u8) · onUpdate. 검증기가 client = onUpdate 하나 · (ClientContext?) → void · 주기 1 · 구독
  없음, server = onUpdate 없음. 역어셈블 "system <id> <파일> client onUpdate fn n".
- 바인딩 계약을 도메인마다: BasicHostBinding<Host> · HostBindingShape · checkHostBindingShapes(…, 도메인) — 서버 hostBindings 는
  Server 항목만, 다른 도메인 항목의 구현은 문제. 컴포넌트 값 → VM 칸 변환을 ScriptValues.hpp 로 꺼내 서버 · 클라이언트가 나눈다.
- core/script/ClientScriptHost (IHost — 복제 월드 읽기, 줄마다 256 바이트 · 프레임마다 글자 1024 · 줄 64, 트랩이면 그 호출의 표시
  버림) · ClientScriptSystems (프레임마다 onUpdate, 프레임 연료 2,000,000, 트랩 3 번이면 끔 — 메모리에만, 통계 ·
  describeClientScriptSystems).
- 서버: ScriptSystemAdapter 가 client 시스템을 건너뛰고, SimulationWorld 는 server 시스템이 있을 때만 어댑터를 만든다
  (ScriptPrograms::systemCount(도메인)). 어댑터를 다시 만들 때 새 것을 먼저 만들고 바꿔 낀다 (옛 자리를 다시 써서 "다시 만들었다"
  시험이 우연에 기대던 것).
- 클라이언트: NetworkSession 이 콘텐츠 (오버레이 포함) 에 client 시스템이 있을 때 ClientScriptSystems 를 두고 스냅숏 적용 뒤
  돌린다 (바이트코드가 바뀌면 새로). IWorldSession::scriptOverlay → apps/client/ui/ScriptPanel 이 글자를 개체의 그린 자리 위에,
  줄을 "스크립트" 칸에 (패널을 숨겨도). 헤드리스 끝 줄에 시스템 통계 · 마지막 프레임 줄.
- d.ts: SystemDefinition.domain?: "server" | "client", onUpdate?(ctx: ClientContext), interface ClientContext.
- 시험: test_client_scripts 5 · test_client_script_session 1, 기존 시험의 형식 5 · 지문 · 서버 바인딩 수 · S426 문구 갱신.
  문서: ADR-0067, 19 (0 · 9 · 10 · 12 · 14 · 17 · 19 · 21장), 18 (M5-T3 ✅ · T4), 10 9장, 11 11장, 15, 17, 13.

**왜**

- 게임 제작자가 서버 규칙과 함께 화면 쪽 표시 (개체 위 값 · HUD 줄) 를 같은 언어로 쓰게. 읽기 · 표시만이라 서버 권위 ·
  결정론 · 해시 · 세이브에 닿지 않는다 — 쓰기 · 명령은 서버 검증을 다시 거쳐야 해서 최소 범위 밖.
- 도메인을 주인 타입으로 가르면 서버 항목 (ctx.write · ctx.log) 을 클라이언트 코드에서 부르는 일이 타입 검사에서 막힌다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — kSimVersion 4).
  SandboxTests 588 케이스. check_includes 위반 0.
- 실행 (스크래치 clientroot — 생태계 팩 + M4 의 Mana 스크립트 + client 시스템 eco.hud: 마나 있는 개체마다 글자 · 합계 줄):
  `SandboxClient --headless --world ecosystem_small --content <clientroot> --frames 120` → "클라이언트 스크립트 eco.hud: 호출 116 ·
  트랩 0 · … 최대 4348", "마지막 프레임 116: 글자 75 · 줄 1", "[eco.hud] tick 58 frame 116 rabbits 75 mana 3825".
  `SandboxServer --world ecosystem_small --content-root <clientroot> --ticks 60 --exit` 의 끝 줄은 서버 시스템 둘뿐 (eco.hud 없음).
- 성능: 서버 시뮬레이션 경로는 client 시스템을 건너뛰는 비교 하나뿐이라 벤치를 돌리지 않았다. 클라이언트 프레임 비용은 위 실행의
  연료 (프레임 평균 약 4,300 · 토끼 75) — 시간 측정은 M5-T4 프로파일에서.
- 창 (ImGui) 의 글자 · 칸을 눈으로 확인하지는 못했다 — 그리기 함수는 헤드리스 ImGui 시험이 부르고, 자리 계산은 카메라 변환 그대로.

**남은 일**

- M5-T4: 프로파일 (서버 · 클라이언트 스크립트 시스템의 연료 · 시간을 에디터 · 패널에). 클라이언트 스크립트의 명령 보내기 · 입력 ·
  그리기 · 엔진 컴포넌트 읽기 · 개체 참조 풀기는 [계획] (M6 데모가 요구하면).

---

## 2026-10-26 — 문서 정리: 입구 · 상태 줄 · 소스 지도 · 남은 [계획] 표시를 코드와 맞춤

**무엇을**

- 입구: README.md (Phase 12 시점의 상태 문단 → 버전 표 · 핵심 기술 · 해 보기 · 스크립트 예 — 예는 sbx check 로 컴파일 확인),
  docs/README.md (지도의 성격 · 설명 · 0장 "지금 어디인가" · 읽는 순서에 스크립트 · 게임 제작, Phase 0 문구 뺌), AGENTS.md 1장
  ("지금" = 18-PLATFORM 0장, 스크립트 코드 → 19).
- 상태: 16-ROADMAP 을 v0.1 Phase 이력 문서로 (머리 · 4장의 Phase 3 티켓 목록 → 남은 실기 검증), 18-PLATFORM (기준일 · 0장 v0.3
  진행 · 1장 · 2장 표의 SandboxScript · 사용자 Component 행이 Planned → Implemented, 사용자 System · 스크립트 도구 행, 3장 모듈).
- 17-SOURCE-MAP 2장: 설계 때의 폴더 트리 → 실제 트리 (없는 Validator · CommandApplier · time/ · metrics/ · Miniaudio 를 빼거나
  [계획] 으로, script/ · core/game · trigger · project · package · scenarios · script 를 넣음), 4장 "이 기능은 어디에?" 줄 더함.
- 규범 문서 머리의 상태 줄: 01 · 02 · 03 · 04 · 09 · 10 (설계 → 규범) · 11 · 13 · 15 에 v0.2 · v0.3 을. 15 4장 타깃 표에
  SandboxScript.
- 구현된 것에 남은 [계획]: 02 (Prefab · Binary Visitor · Opaque · EntityRef 복제), 03 (4.1 검증 · 스크립트 단계), 19 (M1~M3 에
  구현된 9 곳 — 검사기 · f32 · 쓰기 버퍼 · VM · .sbc 머리), 11 (점수 이벤트), 13 (--replay-roundtrip), 06 ([계획 12B]),
  14 · 16 ([계획 15] — Phase 15 가 끝났다).
- 같은 날 따로: `script:` 커밋 — 진단 문구 · 주석의 낡은 마일스톤 (ctx.random [계획 v0.3 M2] 등).

**왜**

- 문서 입구 (README · 지도 · AGENTS) 가 Phase 0 · 12 시점에 머물러 지금 어디인지 · 무엇이 있는지 알 수 없었다. 버전 계획으로
  넘어간 뒤 16 과 18 의 몫이 겹쳤다 — 16 = 이력, 18 = 지금 · 다음으로 나눴다.

**검증**

- 문서 안 상대 링크 전부 (README · AGENTS · docs/*.md) 가 있는 파일을 가리킨다 (스크립트 확인 — 끊긴 링크 0).
- [계획] 표시마다 코드를 찾아 보았다 (예: --record-replay · 오토세이브 · Miniaudio · Chat · TagMask · D6 은 아직 없어 그대로).
- `script:` 커밋: 빌드 · ctest 62/62 (render_tests_warp 제외).

**남은 일**

- docs/adr/README.md 의 표 다시 맞춤 (공백) 은 이 작업 전부터 작업 트리에 있던 바깥 변경이라 넣지 않았다.

---

## 2026-10-26 — v0.3 M5-T2: 스크립트 오버레이 · 에디터 "콘텐츠" 창의 스크립트 편집 (ADR-0066, 프로토콜 13)

**무엇을**

- 오버레이 종류 Script (4): 항목 = 팩의 scripts/ 파일 하나, id = "<팩>.<경로 마디를 '.' 로>" (`scriptOverlayId` ·
  `scriptOverlayPath`), 값 {"id", "source"}. 세이브 overlay/scripts/<id>.json. 오버레이 지문은 v0.2 의 넷 다음 종류가 비어 있으면 넣지
  않는다 (스크립트 없는 오버레이 · 세이브 해시가 그대로 — 시험이 옛 계산과 맞춘다). 프로토콜 12 → 13.
- ContentLoader: 오버레이에 Script 항목이 있으면 0 단계에서 소스를 팩 파일 목록에 넣고 (바꾸기 · 더하기 — applyScriptOverlay) 다시
  컴파일. 컴포넌트 정의 지문 (정의 자리 빼고) · id 목록이 팩 DB 와 다르면 V11, 카탈로그는 팩 DB 의 것을 나눈다. 구독 태그 풀기는
  이번에 컴파일한 것에만. loadOverlay 는 Script 를 건너뛴다.
- SimulationWorld::syncScriptSystems: 콘텐츠의 바이트코드가 바뀌면 (생성 · 오버레이) 어댑터를 다시 만들고 이어진 트랩 · 꺼짐을
  digest 가 같은 팩만 옮긴다.
- 에디터 "콘텐츠" 창 종류 "스크립트": 팩의 scripts/*.ts + 더한 파일, 소스 편집, 검사 (오류마다 "  12 | <그 줄>" — scriptExcerpt),
  적용 (contentEditValue → EditContent Script), 이 파일의 시스템 요약, 새 파일 틀 (scriptTemplate).
- CommandJson 의 content 오류 문구에 trigger · script.
- 시험: test_script_overlay 3 · test_script_editor 2 · test_roles_overlay +1 (루프백 — kind 4), test_messages · test_roles_overlay 의
  프로토콜 · 종류 상한 갱신. 문서: ADR-0066, 08, 09 3장, 10 8장, 11, 19 (0 · 20장), 18 (M5-T2 ✅), 17, 13.

**왜**

- 스크립트를 고치려면 팩 파일을 바꾸고 서버를 다시 열어야 했다. 오버레이 경로 (권한 · 서버 검증 · 복제 · 되돌리기 · 세이브 ·
  리플레이) 를 그대로 쓰면 게임을 멈추지 않고 모두에게 같게 바뀐다.
- 컴포넌트 정의는 살아 있는 월드의 풀 배치라 오버레이로 바꾸지 않는다 — 시스템 코드 편집이 대부분이다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). SandboxTests 581 케이스.
  check_includes 위반 0.
- 실행 (스크래치 EditProj — sbx new + M4 의 Mana 스크립트): 자동 플레이 스크립트 (단계 60 에 eco.regen_system 을 intervalTicks 10 ·
  "v2" 로그로 바꾸고, 단계 100 에 "export default 3" 으로 깨뜨린다) 를 `SandboxServer --world EditProj --script editplay.json` 과
  `SandboxClient --world EditProj --script editplay.json --headless` 로 — 둘 다 "eco.mana.regen t90/t120/t150/t180: v2 regen …"
  (바꾼 시스템이 그 뒤로 10 틱마다, 깨뜨린 편집은 거절되어 v2 그대로), 끝 해시가 같다 (0x2c4188b3b238866f).
- 에디터 창 (ImGui) 을 손으로 눌러 보는 확인은 하지 못했다 — 이 기계의 화면에서 클라이언트 창을 잡지 못했다 (다른 앱이 열린
  사용자 데스크톱). 창의 검사 · 적용 · 틀 함수는 단위 시험이, 와이어 · 서버 적용 · 클라이언트 컴파일은 루프백 시험과 위 실행이 본다.

**남은 일**

- M5-T3: IClientContext 최소 (onUpdate) · 프로파일. 스크립트 편집기의 줄 번호 · 구문 색 (v0.5 검토), 16 KiB 를 넘는 파일, 오버레이로
  컴포넌트 정의 바꾸기 (월드 다시 열기와 함께) 는 [계획].

---

## 2026-10-26 — v0.3 M5-T1: sbx new 의 스크립트 SDK 파일 · sbx check 의 스크립트 요약 · P6 (ADR-0065)

**무엇을**

- 18-PLATFORM 6.2 의 M5-T1 (CLI + 에디터 + 프로토콜) 을 둘로 나눴다: T1 = CLI (sbx new · check), T2 = 에디터의 스크립트 오류 줄 ·
  적용 (오버레이 Script — 프로토콜 ++), 옛 T2 (IClientContext · 프로파일) → T3.
- `script::sdk::generateTsconfig` (strict · noEmit, include = sdk/sandbox.d.ts · content/*/scripts/**/*.ts). `project::createProject`
  가 sdk/sandbox.d.ts · tsconfig.json 을 만든다 (스크립트가 없어도).
- `project::validateProject` 5 단계 (P6 경고): sdk/sandbox.d.ts 가 이 엔진의 생성 결과와 다름 (CR 은 빼고 비교) · 스크립트가 있는데
  없음, tsconfig.json 이 JSON 아님. ProjectReport 에 스크립트 팩 · 컴포넌트 수 · 시스템 줄 · SDK 상태.
- sbx check: "스크립트: 팩 n · 컴포넌트 n · 시스템 n · SDK … 최신|다름|없음" 과 시스템마다 한 줄. sbx new 가 SDK 파일을 알린다.
- 시험: test_game_project +2. 문서: ADR-0065, 18 (5.2 P6 · 6.2 M5 나눔), 15 7장, 19 14장, 17, 13.

**왜**

- 스크립트 진단은 M3-T2 부터 sbx check 에 나오지만, 새 프로젝트에 IDE 가 읽을 SDK 선언 · tsconfig 가 없었고, 엔진을 올리면 프로젝트의
  d.ts 가 낡아도 알 길이 없었다. 경고 (P6) 로 둔 것은 IDE 파일이 게임 실행과 무관하기 때문이다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과. unit_project +2. check_includes 위반 0.
- 실행 (스크래치): `sbx new NewScripted --id studio.newscripted --from ecosystem_small` → sdk/ · tsconfig.json 생성 · 검사 통과,
  M4 의 스크립트 셋을 넣고 `sbx check` → "스크립트: 팩 1 · 컴포넌트 1 · 시스템 3 · SDK sdk/sandbox.d.ts 최신" 과 시스템 셋 줄
  (onTick (30 틱마다) · onStart · onEvent (death · spawn)), sdk/ 를 지우면 "sdk/sandbox.d.ts:/: 경고 P6 — 스크립트가 있는데 SDK
  선언이 없다".
- tsc 로 템플릿을 검증하지는 못했다 — 이 기계 (빌드 체인) 에 Node 가 없다. tsconfig 는 오래된 tsc 도 읽는 옵션만 썼다 (ADR-0065).

**남은 일**

- M5-T2: 에디터 "콘텐츠" 창의 스크립트 오류 줄 · 적용 (오버레이 Script — 프로토콜 ++). sbx sdk 가 tsconfig 를 보존하며 갱신하는 길은
  필요해지면.

---

## 2026-10-26 — v0.3 M4-T3: 스크립트 예산 · 트랩 정책 · 시스템별 통계 (ADR-0064) → M4 완료

**무엇을**

- 호출 예산에 Host 호출 상한 (`vm::Limits::maxHostCalls` 65,536 — 넘으면 HostError). ScriptHost 의 ctx.log 는 틱 256 줄
  (넘으면 버리고 `droppedLogs` 로 센다).
- `ScriptSystemAdapter`: 틱 연료 4,000,000 을 16b + 17d 의 모든 호출이 나눠 쓴다 — 호출마다 min(1,000,000, 남은 연료), 남은 연료로
  멈춘 호출은 "예산" (쓰기 버림, 트랩 정책 밖), 남은 연료가 0 이면 "건너뜀". 트랩이 3 번 이어지면 시스템을 끈다 (경고 한 줄).
  이어진 트랩 · 꺼짐 = 시뮬레이션 상태 → `state()` / `restoreState()` (팩 바이트코드 digest 가 다르면 버리고 다시 켠다).
  시스템별 누적 통계 (호출 · 트랩 · 예산 · 건너뜀 · 연료 합 · 최대 · 마지막 트랩) 와 `describeScriptSystems`.
- 세이브: world.json "scriptSystems" (선택 키 — 기본값이 아닌 시스템만). 로드가 되살리고, 바뀐 · 없는 시스템은 경고.
- SandboxServer · sbx_sim_check 가 끝날 때 시스템마다 한 줄.
- 고친 것: 트랩 로그의 함수 이름이 깨져 나왔다 (M4-T1 부터) — 삼항의 공통 타입이 std::string 이라 string_view 가 임시값을
  가리켰다. std::string 으로 받고 시험이 함수 이름을 본다.
- 시험: test_script_budget 4 케이스 (unit_script). 문서: ADR-0064, 09 2장, 15, 19 (0 · 13 · 14 · 17 · 19장), 18 (M4 완료, 다음
  M5-T1), 17, 13.

**왜**

- 호출 예산만으로는 시스템 수만큼 틱이 길어지고, 계속 망가진 시스템이 틱마다 연료를 태운다. 연료 (명령 수) 로 끊어 결정적이다
  (벽시계를 보지 않는다 — 04).
- 끈 상태를 메모리에만 두면 세이브 → 로드가 시스템을 다시 켜 두 월드가 갈린다 (D2). digest 로 고친 스크립트는 저절로 다시 켠다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). unit_script +4.
  `tools/check_includes.py`: 478 파일 위반 0.
- 실행 (스크래치 sysroot — M4-T1 · T2 의 재생 · 이벤트 시스템 + 틱 1 ~ 3 에 트랩 나고 그 뒤에 값을 쓰는 `eco.mana.buggy`):
  `sbx_sim_check --scenario ecosystem_small --content-root sysroot --ticks 300 --repeat 2 --save-at 135 --threads 0,1` →
  "eco.mana.buggy t3: 트랩이 3 번 이어져 시스템을 껐다 (마지막: … (ecosystem/scripts/buggy_system.ts:onTick pc 7))", D1 · D2 · D5
  일치 (꺼진 상태를 세이브가 잃으면 틱 136 부터 값을 써서 갈린다), 끝 해시가 M4-T2 실행과 같다 (꺼진 시스템은 아무것도 안 썼다),
  끝 줄 "스크립트 eco.mana.buggy: 호출 3 · 트랩 3 · 예산 0 · 건너뜀 0 · 연료 합 27 · 최대 9 · 꺼짐" 외 둘.
- 벤치: 스크립트 없는 월드의 경로는 그대로 (어댑터 없음). 틱 연료의 실제 시간은 M6-T1 벤치에서 맞춘다.

**남은 일**

- M5-T1: sbx check 스크립트 진단 · 템플릿 tsconfig · 에디터 스크립트 오류 · 적용. 시스템 켜고 끄기 명령 · 시스템별 시간 프로파일은
  M5 [계획]. 시스템별 예산 키는 M6 벤치 뒤 검토.

---

## 2026-10-26 — v0.3 M4-T2: onStart · onStop · onEvent — 단계 17d, 그 틱의 이벤트 (ADR-0063)

**무엇을**

- `script/api/SystemEvents`: 이벤트 종류 (death · spawn · areaEnter · score — events 키 · GameEventKind 멤버), GameEvent 필드 (이름
  순 = 칸), 예산 상한. 검사기 · 검증기 · 엔진 · d.ts 가 같이 읽는다.
- 검사기: defineSystem 에 onStart · onStop · onEvent 메서드, `events` 구독 (종류마다 하나 — tags { all · any · none } 이름,
  areaEnter 의 min · max [x, y] — f32 로 반올림), `eventsPerTick` (기본 256). onTick 은 선택이 됐다 (콜백이 하나는 있어야 한다).
  SDK enum GameEventKind · 구조체 GameEvent (onEvent 의 e — 적지 않으면 그 타입). 잘못은 S426.
- 바이트코드 .sbc 형식 4: 시스템 표에 콜백 함수 번호 (없으면 0xFFFFFFFF) · eventsPerTick · 구독. 검증기가 콜백 서명 (onEvent =
  (ServerContext?, GameEvent?) — 구조체 모양까지) · 구독 차례 · 사각형 · 태그 · 예산을 본다.
- VM: `Interpreter::call(함수, 인자, 칸)` — 구조체 매개변수를 칸 목록으로 이 호출의 아레나에 만든다 (Host 결과와 같은 잎 규칙,
  칸 수가 다르면 BadCall).
- 콘텐츠: 로드 1 단계 (태그) 뒤 구독의 태그 이름을 태그 비트로 풀어 `ScriptPackProgram::events` 에 — 없는 태그는 V7 (시스템 id ·
  정의 자리).
- 단계 17d (`core/simulation/SimulationScripts.cpp`, 17c 뒤): 시작 (세션이 Running 이 된 틱 · 세션 없으면 틱 1) · 끝 (Finished 가
  된 틱) · Died · EntitySpawned · 지금 개체 · 점수 칸을 모아 `ScriptSystemAdapter::runCallbacks` 로 — ① onStart ② onEvent (시스템 →
  종류 → 일어난 차례, eventsPerTick · 틱 4096 예산 — 넘으면 cut) ③ onStop. 단계 0 스냅숏을 트리거와 공용으로 — 스크립트가 death ·
  areaEnter · score 를 구독하면 세션 없는 월드에서도 뜬다.
- sandbox.d.ts: GameEventKind · GameEvent · TagFilter · SystemEvents, SystemDefinition 의 콜백 · events · eventsPerTick.
- 시험: test_script_callbacks 9 케이스 (unit_script), M4-T1 시험의 바뀐 문구 · 형식 번호.
- 문서: ADR-0063, 03 2장 (17d · 그림), 04 (simVersion 이력 — 올리지 않음), 11 (8 · 11장), 19 (0 · 9 · 11 · 12 · 13 · 17 · 18장),
  18 (M4-T2 ✅, 다음 M4-T3), 17, 13.

**왜**

- "그 틱에 일어난 일" (죽음 · 생성 · 점수 · 세션 끝) 은 17 · 17b · 17c 가 끝나야 다 있다. 16b 에서 지난 틱의 이벤트를 받으면 사이의
  세이브가 이벤트를 잃거나 대기열이 세이브 상태가 된다 — 17d 에서 그 틱 안에 답한다.
- 시작 · 끝을 틱 번호 · 세션 stateTick 으로만 알아내 세이브 → 로드한 월드가 onStart 를 다시 부르지 않는다 (D2).
- 이벤트 원천 (스냅숏 · Died · 영역 규칙) 을 트리거와 같게 — 콘텐츠의 트리거와 스크립트가 같은 일을 같은 뜻으로 본다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). unit_script +9.
  `tools/check_includes.py`: 477 파일 위반 0.
- 작업 중 `tests/unit/script/test_bytecode.cpp` 1 줄 앞에 바깥에서 들어간 '+' 가 있어 빌드가 깨졌다 — 사용자에게 묻고 지웠다
  (HEAD 와 같아져 이 커밋에는 없다).
- 실행 (스크래치): 저장소 ecosystem 팩 복사본 (토끼에 eco.mana · M4-T1 의 재생 시스템) 에 `eco.mana.events` (onStart 로그,
  events { death · spawn — tags all prey }, spawn 이면 그 토끼의 current = 50, death 면 위치 · 까닭 로그) 를 더해
  `sbx_sim_check --scenario ecosystem_small --content-root sysroot --ticks 300 --repeat 2 --save-at 135 --threads 0,1,4` →
  onStart 는 실행마다 t1 한 번 (로드한 월드는 다시 없음), D1 · D2 · D5 일치. `--ticks 1500 --save-at 700` → 늑대에게 잡힌 토끼
  53 마리의 "rabbit died at x, y cause 3" (t637 부터), tick 700 저장 → 로드 월드가 끝까지 같은 해시.

**남은 일**

- M4-T3: 예산 (연료 · 메모리 · 질의 · 이벤트 · HostCall — 호출 · 틱) · 트랩 정책 (시스템 끄기) · 시스템별 통계. 스크립트의 생성 ·
  파괴 · 점수 Host 항목이 생기면 17d 의 이어짐 (그 이벤트를 같은 틱 목록 끝에). 영역 나감 · 머묾은 트리거와 함께 [계획].

---

## 2026-10-26 — v0.3 M4-T1: defineSystem · ScriptSystemAdapter · 단계 16b (ADR-0062)

**무엇을**

- 검사기: `export default defineSystem({ id, domain?, intervalTicks?, onTick(ctx) { … } })` — 상수 정의를 `Program::systems` 로,
  onTick 메서드 하나 = 함수 하나 (ctx 를 적지 않으면 ServerContext, 반환 void). 진단 S426 (정의) · S427 (자리), S423 은 "export
  default 의 다른 식 · defineSystem 밖의 메서드" 로 좁혔다. 가져오지 않은 defineSystem 은 import 를 안내하는 S401.
- 바이트코드 .sbc 형식 3: 모듈의 시스템 표 (id 오름차순 — id · 파일 · onTick 함수 번호 · intervalTicks). 검증기가 id 규칙 · 순서 ·
  겹침 · 함수 번호 · onTick 서명 · 주기를 본다. 코드 생성이 표를 id 순으로 쓴다.
- 콘텐츠: 로드 0 단계가 팩마다 만든 모듈을 `ContentDatabase::scriptPrograms` (core/content/ScriptPrograms) 에 둔다 — 오버레이 DB 와
  나눈다. 팩 사이 같은 시스템 id 는 V1.
- `core/script/ScriptSystemAdapter`: 팩 모듈마다 인터프리터, ScriptHost 하나. 단계 16b (Native System 뒤, 17 Structural② 앞) 에서
  (팩 순서, id) 차례로 틱 % intervalTicks == 0 인 시스템의 onTick 을 부른다 (beginCall → call → endCall(트랩 아님)). 트랩이면 그
  호출의 쓰기만 버리고 다음 시스템으로, 로그는 시스템마다 첫 트랩 한 줄. SimulationWorld 는 콘텐츠에 시스템이 있을 때만 만든다.
- sandbox.d.ts 에 SystemDefinition · defineSystem (sbx sdk sdk 로 다시 만듦 — Host 표 지문은 그대로).
- 시험: test_script_systems 7 케이스 (unit_script), test_component_host 의 형식 번호 2 → 3.
- 문서: ADR-0062, 03 2장 (16b · 그림), 04 (simVersion 이력 — 올리지 않음), 11 (8 · 11장), 19 (0 · 1 · 9 · 11 · 12 · 14 · 17장),
  18 (M4-T1 ✅, 다음 M4-T2), 17, 13.

**왜**

- 스크립트가 컴포넌트를 정의 · 읽기 · 쓰기까지 하지만 시뮬레이션이 부르는 자리가 없었다. ADR-0057 7 의 조건 (단계 하나, 순서 = 팩
  순서 · id, 스크립트 없는 월드는 그대로) 을 구체화했다.
- 16b (17 앞) 는 앞으로의 생성 · 파괴 (ECB) 가 같은 틱에 반영될 자리이고, 17b · 17c 가 스크립트 결과를 같은 틱에 본다. 스케줄러의
  System 으로 넣지 않아 스크립트 없는 월드의 스케줄러 목록 · 프로파일이 그대로다.
- 주기를 틱 번호로만 정해 세이브에 스크립트 상태가 생기지 않는다 (ADR-0056 4).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — kSimVersion 4).
  unit_script +7. `tools/check_includes.py`: 474 파일 위반 0.
- 실행 (스크래치): ManaGame 프로젝트에 `eco.mana.regen` (intervalTicks 30 — 16장의 regen 을 부르고 90 틱마다 ctx.log) 을 더해
  `sbx check` 통과, `SandboxServer --world ManaGame --ticks 300 --exit --port 0` 이 "[script] eco.mana.regen t90/t180/t270" 로그 뒤
  tick 300. 저장소 ecosystem 팩 복사본 (토끼에 eco.mana) 으로 `sbx_sim_check --scenario ecosystem_small --content-root sysroot
  --ticks 300 --repeat 2 --save-at 135 --threads 0,1,4` 가 토끼 75 마리를 재생하며 D1 · D2 · D5 일치. intervalTicks 0 · stage 키를
  넣으면 `sbx check` 가 "content/ecosystem/scripts/regen_system.ts:5:18: 오류 S426 — …" 두 줄로 거절.
- 벤치: 스크립트 없는 월드의 틱 경로는 포인터 검사 하나라 sbx_bench 를 다시 재지 않았다. 스크립트 시스템의 비용 (10k 대상) 은 M6-T1.

**남은 일**

- M4-T2: onStart · onStop · onEvent, M4-T3: 예산 · 트랩 정책 (시스템 끄기) · 시스템별 통계. 스크립트의 생성 · 파괴 (ECB) 는 Host 항목과
  함께 [계획].

---

## 2026-10-26 — v0.3 M3-T4: 스크립트 컴포넌트의 세이브 자동 마이그레이션 (ADR-0061) → M3 완료

**무엇을**

- `persist::migrateScriptComponent` (core/persist/Migration): 저장 version < 스키마 version 이면 스키마에 없는 필드는 버리고 (이름을
  모은다), i32 · enum 이 된 필드의 정수인 실수 (3.0) 는 정수로. 없는 필드는 그대로 (읽기가 기본값). 저장 version > 스키마 =
  VersionMismatch ("스크립트를 되돌렸나").
- `loadWorld`: 스크립트 컴포넌트 (dynamicLayout) 는 MigrationRegistry 없이 위 함수로. 옮긴 뒤 엄격한 읽기가 실패하면 "'id' v1 → v2
  마이그레이션 뒤 읽지 못했다 (필드의 종류 · 범위가 바뀌었다)". 같은 version 에서 스키마에 없는 필드 = 실패 ("version 을 올린다"),
  저장 값에 없는 필드 (더하기만) = 기본값 + 옮긴 것으로 친다. 컴포넌트마다 경고 한 줄 (개체 수 · 버린 필드).
- 시험: test_script_content +2 케이스. 문서 09 6장, 19 (0 · 15장), 18 (M3 완료), 17, 13, ADR-0061.

**왜**

- 스크립트 컴포넌트는 제작자가 자주 바꾸는데 C++ 마이그레이션 함수를 줄 자리가 없다 (스크립트에 전역 실행이 없다). 더하기 · 빼기를
  자동으로 하고, 값이 바뀌는 변환은 오류로 드러낸다 (04 — 세이브 값을 조용히 바꾸지 않는다).
- 같은 version 의 "더하기만" 을 처음엔 그대로 읽게 했는데, 컴포넌트 바이트가 달라 로드 해시 비교 (D2) 가 "해시 불일치" 로 실패했다
  (시험이 잡았다) — 옮긴 것으로 쳐서 해시 비교를 건너뛰고 경고하도록 고쳤다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로). unit_content +2.
- 실행 (스크래치): 저장소 ecosystem 팩 복사본에 eco.mana v1 (current · old) 을 더하고 토끼 Prefab 에 붙여
  `sbx_sim_check --scenario ecosystem_small --content-root migroot --ticks 120 --save-at 60 --save-dir migsave` (D2 일치) →
  스크립트를 v2 (old 빼고 shield 더함) 로 → `SandboxServer --world migsave --content-root migroot --port 0 --ticks 90 --exit` 가
  "세이브: 스크립트 컴포넌트 'eco.mana' 를 v1 → v2 로 옮겼다 (개체 75) — 버린 필드: old" 경고 뒤 tick 90 까지. 같은 세이브를
  version 1 · old 없는 스크립트로 열면 "필드 'old' 가 스크립트 컴포넌트 'eco.mana' (version 1) 에 없다 — … version 을 올린다" 로 거절.

**남은 일**

- M4-T1: defineSystem · ScriptSystemAdapter · 단계 ADR. 이름 바꾸기 · 단위 변환 (스크립트 onMigrate) 은 v0.4 검토.

---

## 2026-10-26 — v0.3 M3-T3: 스크립트의 컴포넌트 읽기 · 질의 · 쓰기 + 호출 단위 쓰기 버퍼 (ADR-0060)

**무엇을**

- Host 표에 ctx.read(e, ref): T | null · ctx.has · ctx.query(ref): Entity[] · ctx.write(e, ref, T). 제네릭 ABI 타입 Component ·
  ComponentData · ComponentDataOrNull · EntityList (kHostApiVersion 1 그대로 — 더하기만, 지문 0388e85856c3e9f3).
- 검사기: TypeKind::Component (ComponentRef<T> — defineComponent 의 const 가 값이 된다), componentHostCall 이 ref 인자로 T 를 풀어
  나머지를 검사. 코드 생성: ComponentRef = 불투명 Component, LOADK + 모듈의 컴포넌트 표 → .sbc 형식 2. 검증기: 표의 id 규칙 ·
  겹침, LOADK, Entity[] 결과.
- VM: ComponentData 인자를 칸 목록으로 펼치고 (HostArg::fields), ComponentDataOrNull · EntityList 결과를 칸 목록에서 만든다
  (레지스터 타입을 따라 — 잎 모양 · 칸 수가 다르면 HostError), HostResult::extraFuel.
- ScriptHost: beginCall(tick, 이름, 월드) · endCall(commit). 호출마다 개체 핸들 표, 질의 saveId 순 (상한 65,536 · 개체마다 연료 1),
  읽기는 쓰기 버퍼 먼저, 쓰기는 바이트로 검사 (범위 · 유한 · f32 범위 · 개체 참조 → saveId) 해 버퍼에 — endCall(true) 가 적용,
  트랩이면 버림. 스크립트 컴포넌트만 (엔진 컴포넌트 · 붙이기 · 떼기는 [계획]).
- sandbox.d.ts 갱신 (read<T> … write<T>). ADR-0057 상태 줄에 "4 의 Query · 초안 setField 는 ADR-0060 이 대신" 표시.
- 시험: test_component_host 7 케이스 (unit_script), 기존 시험 갱신 (지문 고정값 · 형식 번호 · 컴포넌트 const 가 값이 된 것).

**왜**

- v0.3 완료 기준 "Mana 재생 시스템" 의 몸통 — 시스템 단계에서 부르기 (M4-T1) 전에 Host 쪽 의미 (순서 · 버퍼 · 검사 · 핸들) 를 시험으로 고정한다.
- 질의를 배열로, 쓰기를 값 통째로 한 것: 새 VM 객체 종류 · 필드 이름 문자열 없이 기존 규칙 (for-of · 구조체 맞추기 · d.ts 제네릭)
  으로 끝난다 (ADR-0060 5 · 6). VM 쪽 잎 모양 검사는 검증기가 T 를 모르는 대신 메모리 안전을 지킨다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — 엔진은 아직 스크립트를
  부르지 않는다, kSimVersion 그대로). unit_script 59 → 66 케이스.
- 실행: 스크래치 프로젝트 ManaGame (M3-T2) 에 regen.ts (query → read → Math.min → write) 를 더해 `sbx check` 오류 0, 일부러 빠진
  필드로 ctx.write 하면 "content/ecosystem/scripts/regen.ts:15:85: 오류 S404 — Mana 의 필드 'regen' 가 빠졌다" · 종료 코드 1.
  `SandboxServer --world ManaGame --port 0 --ticks 300 --exit` → tick 300 hash 0x6a9e46df903c0e1a (스크립트를 부르는 단계는 M4).
- `sbx sdk sdk` 로 sdk/sandbox.d.ts 를 다시 만들어 골든 시험 통과.

**남은 일**

- M3-T4: 스키마 version · 세이브 필드 마이그레이션. M4-T1: 시스템 단계에서 부르기 (그때 버퍼 적용이 틱 안 — 복제 changed 표시 맞음).
- 엔진 컴포넌트의 ComponentRef (권한 표), 붙이기 · 떼기 · 생성 · 파괴 (ECB), 여러 컴포넌트 질의, 질의 정렬 비용 측정 (M6 벤치).

---

## 2026-10-26 — v0.3 M3-T2: 콘텐츠 팩 scripts/ 컴파일 → 콘텐츠마다 카탈로그 → Prefab · 명령 · 세이브 · 복제

**무엇을**

- `core/content/ContentLoader`: 로드 0 단계 — 팩 순서로 `scripts/**/*.ts` 를 팩마다 검사 + 코드 생성. 진단은 콘텐츠 문제
  ("<팩 폴더>/scripts/a.ts:줄:열: 오류 S404 — …", 규칙 = 진단 코드). defineComponent 가 정의한 컴포넌트를 엔진 카탈로그의 복사에
  넣어 그 카탈로그로 Prefab · Rule · 트리거를 검사한다 (겹치는 id = V1). 다른 함수에 오류가 있어도 오류 없는 정의는 넣는다.
  오버레이는 스크립트를 다시 컴파일하지 않고 base 의 카탈로그를 나눈다. 동적 컴포넌트가 든 카탈로그를 받으면 그것을 빼고 시작한다.
- `ContentDatabase::worldCatalog(엔진)` · `scriptCatalog` · `scriptComponents`. 스크립트 컴포넌트가 없으면 카탈로그를 만들지 않는다
  (엔진 카탈로그 그대로 — 스크립트 없는 월드는 지금과 같다).
- 월드가 그 카탈로그를 쓴다: SimulationWorld 생성자 · persist::loadWorld · ClientWorld::create · 에디터 NetworkSession::catalog
  (복제 월드가 있으면 그 카탈로그 — Inspector · 되돌리기). ComponentCatalog::hasDynamic · withoutDynamic.
- `core/script/ScriptComponents`: 문제를 구조체 ScriptComponentProblem 으로 (모듈 · 줄 · 열 · id · 이유).
- 시험: test_script_content 8 케이스 (unit_content), test_replication +1 (unit_network). 문서 11 (1 · 1.2 · 8 · 11장), 19, 02, 18, 17, 13.

**왜**

- ADR-0059 6 의 "콘텐츠마다 카탈로그": 한 프로세스의 월드들 (에디터의 편집 · 테스트 플레이, 도구) 이 섞이지 않게 카탈로그를 콘텐츠
  DB 가 든다. 부르는 쪽 (서버 · 클라이언트 · 도구 · 에디터) 은 엔진 카탈로그를 그대로 넘기고, 월드를 만드는 세 곳이 고른다 —
  배선을 바꾸는 곳이 셋뿐이다.
- 진단을 콘텐츠 문제로 내면 sbx check · 서버 로드 · 클라이언트가 같은 목록을 본다 (v0.3 M5-T1 의 "sbx check 의 스크립트 진단" 중
  CLI 부분이 여기서 된다 — 에디터 "콘텐츠" 창 표시는 M5-T1 그대로).

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — 스크립트 없는 콘텐츠는
  카탈로그 · 순서가 같다, kSimVersion 그대로). unit_content +8, unit_network +1 (스크립트 컴포넌트가 복제 표에 · serverOnly 는
  빠짐 · 첫 스냅숏과 바뀐 값이 같은 바이트로 수렴).
- 실행 (스크래치 폴더): `sbx new ManaGame --id studio.mana --from ecosystem_small` → `content/ecosystem/scripts/mana.ts` 에 eco.mana,
  토끼 Prefab 에 `"eco.mana": {"current": 5}` → `sbx check` 오류 0 (경고 2 = 시작 월드가 다른 contentHash 로 저장됨 — 예상) →
  `SandboxServer --world ManaGame --port 0 --ticks 900 --exit` 두 번 = tick 900 hash 0x240b869605933f4e entities 548 (같다) →
  `sbx build` · `sbx check mana.sbpkg` · 패키지로 서버 = 같은 해시 → `SandboxClient --headless --world ManaGame --frames 120` =
  스냅숏 29 · 다시 맞춤 0. 기본값을 범위 밖 (500) 으로 바꾸면 `sbx check` 가
  "content/ecosystem/scripts/mana.ts:4:32: 오류 S424 — 기본값 500 이 범위 [0, 100] 밖이다" + Prefab V3 로 종료 코드 1.

**남은 일**

- M3-T3: Host ctx.read · has · query · 필드 쓰기 + 호출 단위 쓰기 버퍼. M3-T4: 스키마 version · 필드 마이그레이션.
- 바이트코드를 DB 에 두기 (M4-T1 — 시스템과 함께). 에디터 "콘텐츠" 창의 스크립트 오류 줄 (M5-T1).

---

## 2026-10-26 — v0.3 M3-T1: 스크립트 컴포넌트 정의 → 스키마 → 동적 풀 · 카탈로그 (ADR-0059, D-03)

**무엇을**

- ADR-0059 (D-03 결정): defineComponent 는 최상위 const 의 상수 정의 — 검사기가 실행하지 않고 스키마로 읽는다. 필드는 이름 순,
  저장은 바이트 배치 동적 풀, 형식은 내장 컴포넌트의 방문자를 그대로 (같은 JSON · 해시 · 복제 바이트), 카탈로그는 콘텐츠마다 [M3-T2].
- `script/api/ComponentSchema`: 정규 스키마 (id · version · 플래그 · 필드 — 종류 i32 · f32 · f64 · bool · vec2 · entity · enumOf, 기본값 ·
  범위 · enum 멤버), 이름 규칙 · 엔진 이름공간 · f32 반올림. Foundation 만.
- 검사기: SDK 이름 defineComponent · field · Vec2. `const X = defineComponent({ … })` → Program::components (스키마 · 심볼 · 값 구조체).
  새 진단 S424 (정의 모양 · id · version · 필드 · 기본값 · 범위 · 플래그) · S425 (정의 밖의 defineComponent · field, 컴포넌트를 값으로 —
  ctx.read 는 M3-T3). 심볼 종류 Component.
- `core/ecs/DynamicComponent`: DynamicComponentLayout (이름 순 · 자연 정렬 · stride 8 의 배수 · 기본값 바이트), reflectDynamic
  (칸 → 타입 있는 임시값 → 방문자 field()), DynamicComponentPool (ComponentPoolBase + 바이트 배열), dynamicComponentTypeId (이름 → 번호
  프로세스 표).
- `core/ecs/ComponentCatalog`: ComponentInfo 의 함수 칸을 std::function 으로 (부르는 곳은 그대로), dynamicLayout, addDynamic.
- `core/script/ScriptComponents`: 스키마 → DynamicComponentDesc → 카탈로그 (엔진 이름공간 · 겹친 이름은 "파일:줄: 컴포넌트 'id' — 이유").
- sandbox.d.ts: Vec2 · Field · FieldRange · field 이름공간 · ComponentDefinition · ComponentRef · defineComponent (sbx sdk sdk 로 갱신).
- 계획 변경 (18 6.2): M3 을 넷으로 — T1 정의 · 카탈로그 (이번), T2 콘텐츠 로드 · 콘텐츠마다 카탈로그 · Prefab · 세이브 · 복제 ·
  Inspector, T3 Host ctx.read · has · query · 쓰기 버퍼, T4 version · 필드 마이그레이션. 원래 T1 의 "Host 읽기 · 질의 · 쓰기 버퍼" 는 T3 으로.

**왜**

- 초안의 "Native 와 사용자 컴포넌트는 같은 스키마 · 레지스트리 계약": 카탈로그 한 곳을 지나면 세이브 · WorldHash · 복제 · Rule ·
  Inspector 경로가 이미 있다. 형식 규칙을 두 벌 쓰지 않으려고 같은 방문자를 부른다 — 어긋나면 조용히 세이브 · 복제가 달라진다.
- 이름 순은 선언 순서를 바꾸는 흔한 편집이 세이브 · 해시를 바꾸지 않게 (VM 구조체 배치도 이름 순 — ADR-0058).
- M3 을 나눈 것: 콘텐츠마다 카탈로그는 서버 · 클라이언트 · 에디터 · 도구의 카탈로그 배선 전체를 건드린다 — 정의 · 저장 형식을 먼저
  시험으로 고정하고 배선을 따로 커밋한다.

**검증**

- Windows MSVC Debug `ctest --preset windows-msvc-debug -E render_tests_warp`: 62/62 통과 (det_* 골든 그대로 — 시뮬레이션 동작 변경
  없음, kSimVersion 그대로). unit_script +8 (test_components), unit_ecs +5 (test_dynamic_component — 같은 C++ 컴포넌트와 JSON · 해시 ·
  바이트 · 오류 메시지가 같다). check_includes 468 파일 위반 0.
- 벤치 (ComponentInfo → std::function): 같은 기계 Windows MSVC Release `sbx_bench --only save.world` (50k 저장 · 로드 + 해시 검증).
  바꾸기 전 (git stash) 저장 501 · 542 ms, 로드 588 · 596 ms / 바꾼 뒤 저장 493 · 484 ms, 로드 555 · 577 ms — 차이 없음 (측정값, 두 번씩.
  바꾼 뒤 첫 실행 1319 / 1033 ms 는 빌드 직후 냉시작이라 뺐다).
- 실행: 헤드리스 서버 `SandboxServer --scenario random_walk_1k --ticks 60` → tick 60 hash 0x0028fa3b59616a46, 종료 코드 0
  (스크립트 없는 월드 그대로).

**남은 일**

- M3-T2: 콘텐츠 팩 scripts/ 컴파일 → 콘텐츠마다 카탈로그 → Prefab · 세이브 · 복제 · Inspector. M3-T3: Host 컴포넌트 항목 · 쓰기 버퍼.
  M3-T4: 마이그레이션. enum 필드의 JSON 값이 멤버인지 보는 검사는 아직 없다 (내장 enum 과 같다 — i32 범위만).
- tsc --strict 로 sandbox.d.ts + defineComponent 예제를 확인하는 CI 작업 (Node 있는 기계) [계획 — M2-T2 와 같다].

---

## 2026-10-26 — v0.3 M2-T2: Host 바인딩 core/script · 계약 검사 · sandbox.d.ts 생성 → M2 완료

**무엇을**

- `core/script/ScriptHost`: hostBindings (엔진 구현의 명시 목록 — ctx.tick · ctx.log), checkHostBindings (표의 항목마다 구현 정확히
  하나 · 서명 = 표 · 표에 없는 구현 · 함수 없음), ScriptHost (vm::IHost — beginCall 마다 새 ctx 핸들, 지난 · 위조한 · 열리지 않은
  ctx 는 HostError, ctx.log = 로그 "script" 범주 + recentLogs, 한 줄 1024 바이트를 UTF-8 경계에서 자른다). SandboxCore 가
  SandboxScript 를 링크한다.
- `script/sdk/SdkGenerator` + `sbx sdk [<폴더>]`: Host 표 · 불투명 타입 · SDK 내장 → sandbox.d.ts. Host 표에 매개변수 이름
  (paramNames — 정규형 · 지문에는 들어가지 않는다, 지문 그대로). 저장소 `sdk/sandbox.d.ts` (생성해 커밋) = 생성 결과 (골든 시험).
- 계획 변경: 호출 단위 쓰기 버퍼는 M3-T1 로 — 지금 Host 항목은 읽기 · 로그뿐이라 버퍼에 넣을 것이 없다 (18 6.2).
- 시험 test_script_host 6 케이스 · test_sbx_options +1. 문서 19 (14장), 18 (M2 완료), 01, 15, 17, 13.

**왜**

- ADR-0057 의 "넷이 어긋나지 않게": 검사기 (M1-T2) · 검증기 (M1-T3) 에 이어 바인딩 (계약 검사) · d.ts (골든) 가 같은 표를 본다.
- ctx 핸들을 호출마다 새로 내는 것은 "핸들은 호출이 끝나면 무효" (ADR-0057 4) — 스크립트가 전역에 ctx 를 둘 수는 없지만
  (ADR-0056 4) 위조한 불투명 값은 VM 경계에서 막는다.

**검증**

- Windows MSVC Debug: 빌드, unit_script 51 케이스, unit_tools 12, 전체 ctest 62/62 (render_tests_warp 제외).
- `sbx sdk sdk` 를 실제로 돌려 sdk/sandbox.d.ts 를 만들었다 (그 파일이 골든). 시뮬레이션 변경 없음.

**남은 일**

- v0.3 M3-T1: D-03 (동적 컴포넌트 풀) ADR, defineComponent → 스키마 → 카탈로그, ctx.read · ctx.query, 호출 단위 쓰기 버퍼.
- d.ts 를 tsc --strict 로 확인하는 시험은 Node 가 있는 CI 잡에서 [계획].

## 2026-10-26 — v0.3 M2-T1: SandboxScript 인터프리터 · JS 수 규칙

**무엇을**

- `script/vm/Interpreter`: 검증한 모듈을 돌린다 — 레지스터 칸 한 줄 (프레임마다 0 으로), 호출마다 새 아레나 (문자열 · 배열 ·
  구조체 · 상자, 상수 문자열은 표를 그대로), 예산 Limits (연료 · 깊이 · 칸 · 아레나 · 배열 · 문자열), 트랩 10 종 (CallResult —
  함수 · pc · 설명 · 쓴 연료, 예외 없음, 트랩 뒤에도 다시 부를 수 있다), Value (8 바이트 칸), IHost (CALLH — 문자열 인자는 내용으로).
- `script/vm/Numbers`: JS 수 규칙 — ToInt32, Number#toString (to_chars 의 가장 짧은 왕복 자릿수 + JS 의 지수 규칙), round (반은
  +∞ 쪽 · -0), pow (pow(1, NaN) = NaN 등 C 와 다른 곳), min/max (NaN · -0), UTF-16 길이, Math 실행 (자리 → 함수는 consteval 이름
  찾기라 표와 어긋나면 컴파일 오류). 검사기의 상수 접기도 같은 toInt32 · jsPow 를 쓴다 (컴파일 때 값 = 실행 때 값).
- 시험 test_vm 8 케이스. 문서 19 (13장 · 0 · 6 · 9장), 18, 17, 13.

**왜**

- v0.3 요구 7 (fuel · 메모리 · 스택 제한) 과 M2 의 실행. 엔진 바인딩 (M2-T2) 없이 가짜 Host 로 VM 만 시험할 수 있게 IHost 로 나눴다.
- JS 와 같은 뜻 (ADR-0056 3) 을 지키는 곳은 C 표준 함수와 다른 가장자리 (pow · round · min · toString) — 표로 고정했다.

**검증**

- Windows MSVC Debug: 경고 없이 빌드, unit_script 45 케이스 · 4193 단언, 전체 ctest 62/62 (render_tests_warp 제외).
- 같은 호출 = 같은 결과 · 같은 연료, 200 번 불러도 아레나가 쌓이지 않음. 시뮬레이션 변경 없음.
- 성능 벤치는 아직 없다 — 10k 대상 벤치는 v0.3 M6-T1 (sbx_bench).

**남은 일**

- v0.3 M2-T2: core/script Host 바인딩 (표 항목마다 구현 등록 + 계약 검사, 호출 단위 버퍼), sandbox.d.ts 생성기 · 골든.
- 예산 기본값은 M4-T3 에서 시스템 예산과 함께.

## 2026-10-21 — v0.3 M1-T3: SandboxScript 코드 생성 · 바이트코드 .sbc · 검증기 (ADR-0058) → M1 완료

**무엇을**

- ADR-0058: 레지스터 기계, 레지스터마다 타입 · 0 초기화, 8 바이트 칸, 구조체 배치는 필드 이름 순, 선형 검증기, 따로 된 IR 층 없음
  (검사기의 Program 이 타입 있는 IR), 팩 하나 = 모듈 하나, .sbc 에 체크섬 없음.
- `script/vm/Bytecode`: 명령 68 종 (타입별 산술 · 비교 · 변환 · 문자열화, 타입을 나누지 않는 배열 · 필드 · 상자, 흐름 · 호출 · Host
  호출), 타입 표 · 모듈 · export, serialize · deserialize (개수마다 상한 · 남은 크기 검사, 남는 바이트 거절) · disassemble.
- `script/vm/Verifier`: verify (번호 범위 · 피연산자 타입 · 서명 · 끝 명령 · pad · 타입 표 · UTF-8 · import = Host 표 · export
  겹침 · 머리 — 다르면 VersionMismatch), sameType (구조적, 재귀), loadModule.
- `script/lang/Codegen`: Program → Module — 지역 변수 레지스터, 문장마다 타입별 임시 재사용, 넣는 자리 변환 (I2F · 상자 · null ·
  좁힌 이름 UNBOX · Nullable(A) → Nullable(B)), ===/!== 의 null 가능 · 문자열 내용 비교, 짧은 회로, ??, 복합 대입 (비트 연산은
  number 도 i32 로 계산해 되돌린다), for-of (길이를 돌 때마다), switch, 가변 Math 는 둘씩. 만든 모듈은 바로 검증 (S502).
- `script/api/MathBuiltins`: Math 표를 검사기에서 옮겼다 (검증기 · VM 공용 — 자리 = MATHF 번호).
- 검사기: 구조체 필드는 readonly 도 양쪽으로 맞춘다 (배치가 같아야 해서), Math.min · max 에 i32 와 정수 상수를 섞으면 i32.
- 시험 test_bytecode 7 케이스 (+ test_checker 2 단언). 문서 19 (12장 · 5 · 8 · 9장), 18 (M1 완료), 17, 13, adr/README.

**왜**

- v0.3 M2 의 VM 이 돌릴 형식. 바이트코드는 파일로 올 수 있어 (믿지 않는 입력) 메모리 안전을 검증기에 모았다 — 흐름 분석 없이 한 번
  훑기로 끝나게 레지스터 타입을 고정했다.
- 디스어셈블이 드러낸 문제: C++ 함수 인자의 계산 순서 (MSVC 는 오른쪽부터) 에 gen(a) · gen(b) 를 맡겨 a() > b() 가 b 를 먼저
  불렀다 — JS 순서 위반이자 툴체인마다 다른 바이트. 하나씩 지역 변수로 차례를 고정하고 시험으로 묶었다.

**검증**

- Windows MSVC Debug: 경고 없이 빌드, unit_script 37 케이스 · 3459 단언 (모든 바이트 × 8 비트 뒤집기 포함), 전체 ctest 62/62
  (render_tests_warp 제외). 같은 소스 = 같은 바이트, 직렬화 왕복 = 같은 바이트.
- 시뮬레이션 변경 없음.

**남은 일**

- v0.3 M2-T1: 인터프리터 (연료 · 스택 · 호출 아레나 · 트랩 · JS Number#toString · UTF-16 길이).
- 최적화 (명령 융합 · 죽은 코드) 는 M6 벤치 뒤 [재검토 — ADR-0058].

## 2026-10-21 — v0.3 M1-T2: SandboxScript 이름 · 타입 검사기, Host API 표 첫 항목

**무엇을**

- `script/lang/Types`: TypeTable (기본 · T[] · T | null · 구조체 · enum · 불투명), 넣기 규칙 (i32 → number · enum → 수 · null → T | null,
  배열 원소 · 구조체 필드는 양쪽 — 참조라, 재귀 구조체는 비교 중인 짝을 가정).
- `script/lang/Checker`: checkProgram (팩의 스크립트 전부) → Program — 파일 경로 정렬 파싱, import 그래프 (resolveImportPath · 없는
  파일 · 없는 export · 순환 S416, 파싱 오류 모듈의 이름은 Error 타입), 모듈마다 의존 순서로 이름 · interface (재귀) · type 별칭 (순환
  S417) · 함수 서명 · enum · 최상위 const (소스 순서, 기본 타입 상수식 S415), 함수 몸 (반환 타입 추론 — 처음 부를 때, 재귀 S421).
  식: number · i32 규칙 (정수 상수는 i32 자리에, 비트 연산은 i32, 상수 접기는 실행 규칙대로 — i32 감싸기), null 좁히기 (&& · || · ! ·
  ? : · 빠져나가는 if, 대입 없는 이름만), 흐름 (모든 길 return · switch 떨어짐 · break/continue 자리), 객체 · 배열 리터럴 (기대 타입),
  Math 내장, 금지 이름 (S413, 이유와 대안). 마디마다 NodeSema (타입 · 심볼/필드/Host/내장/enum 멤버/length/push) · ConstValue.
- `script/api/HostApi`: ADR-0057 의 단일 정의 표 — ctx.tick · ctx.log, 불투명 타입 Entity · ServerContext, 정규형 · 지문 (kHostApiVersion 1).
  계획에 적은 "읽기 · 질의" 는 컴포넌트 타입이 필요해 v0.3 M3 로 옮겼다 (18 6.2).
- 시험 test_checker 14 케이스. 문서 19 (5 · 10 · 11장, S4xx 표), 17, 13, 18.

**왜**

- v0.3 요구 1 (타입 · 숫자 · 할당 · 모듈 · 결정론 규범) 의 검사 쪽. IR (M1-T3) 이 타입 · 상수 · 참조가 정해진 나무를 받게 한다.
- "SandboxScript 가 받으면 tsc 도 받는다" 를 지키려고 TS 보다 엄격한 쪽을 골랐다: 조건은 boolean, 좁히기는 대입 없는 이름만, 배열 ·
  필드는 양쪽으로, let 에 초기값 (19 11장의 "TS 와 다른 점").

**검증**

- Windows MSVC Debug: 경고 없이 빌드, unit_script 30 케이스 · 1833 단언, 전체 ctest 62/62 (render_tests_warp 제외).
- 잘린 소스 (3 바이트마다) · 타입이 엉망인 소스에서 죽지 않는다. 시뮬레이션 변경 없음.

**남은 일**

- v0.3 M1-T3: 타입 있는 IR → .sbc · 검증기. 숫자 → 문자열 (JS Number#toString) 은 VM (M2).
- 표 · 구조체 상수 (최상위), 문자열 첨자 · 메서드, 배열 pop 등은 [v0.3 뒤 검토].

## 2026-10-21 — v0.3 M1-T1: SandboxScript 렉서 · 파서 → AST · 진단, 모듈 script/, 규범 문서 19

**무엇을**

- 새 모듈 `script/` = 타깃 SandboxScript (Foundation 만, sbx_simulation_flags). 경계: `sbx_forbid_link(SandboxScript SandboxCore …)`,
  `check_includes.py` (script/ → foundation · script, 그래픽 · OS 헤더 금지 — 픽스처 script/BadCore.hpp, 자체 시험 8 → 10 건).
- `script/lang/`: Diagnostic ("파일:줄:열: 오류 Sxxx — …", 모음), Token · Lexer (예약어 · 문맥 낱말, 숫자 — 정확 반올림 (from_chars, 없으면
  C 로케일 strtod_l) · 진법 · 구분자 · 정수 표시, 이스케이프 · 서로게이트 짝, 템플릿 중괄호 깊이 스택, 줄바꿈 표시, ASCII 밖 · UTF-8 ·
  4 MiB 상한), Ast (평평한 마디 풀 · 자식 구간 · dump S 식), Parser (JS 의 ASI, 우선순위 · ** 오른쪽 결합 · >> 타입 인자 나누기,
  as const, 부분집합 밖 문법 S301~S336 을 알아보고 대안을 말한다, 대입 · ++ -- 는 식 문장 맨 위에만 (S334 — 나무 전체를 한 번),
  문장 단위 회복, 깊이 200 · 오류 64 상한).
- 시험 unit_script: test_lexer 6 · test_parser 10 케이스 (S3xx 표 전부, 잘린 Mana 소스의 모든 앞부분 · 의사 난수 바이트열 300 개).
- 문서: 19-SANDBOXSCRIPT (규범 — 0~9장, 타입 · VM 은 [계획]), 01 2장 (타깃 · 경계), 04 4.6, 13, 17, 18 (M1-T1 ✅), README, AGENTS 4장.

**왜**

- v0.3 요구 1 "언어 부분집합 규범 동결" 의 앞단. 검사기 (M1-T2) 가 받을 나무와 진단 형식을 먼저 고정한다.
- 부분집합 밖 문법을 "알 수 없는 낱말" 이 아니라 이유 · 대안과 함께 거절해야 TS 사용자가 고칠 수 있다 (ADR-0056 2).
- 소스는 믿지 않는 입력 (패키지로 온다) — 깊이 · 오류 · 크기 상한과 "잘린 · 아무 바이트 소스에서 죽지 않는다" 를 시험으로 묶었다.

**검증**

- Windows MSVC Debug (windows-msvc): 경고 없이 빌드, unit_script 16 케이스 · 1195 단언 통과, 전체 ctest 62/62 (render_tests_warp 제외),
  arch_include_lint · _selftest (10 건) 통과, 링크 경계 검사 10 타깃.
- clang-cl 은 이 PC 에 없어 windows-clangcl 프리셋을 돌리지 못했다 (Linux clang -Werror 는 CI 에서).
- 시뮬레이션 변경 없음 (골든 그대로).

**남은 일**

- v0.3 M1-T2: 이름 · 타입 검사 (최상위 상수식 · import 순환 · number/i32 · null 좁히기 · Host 표 첫 항목), 이름 수준 금지 (any · undefined …).
- 이름의 한글 등 ASCII 밖 글자 (S101) 는 [v0.3 뒤 검토].

## 2026-10-21 — v0.3 진입: D-01 언어 (ADR-0056) · D-04 Host API (ADR-0057) · v0.3 마일스톤

**무엇을**

- ADR-0056 (D-01): SandboxScript = TypeScript 의 엄격한 부분집합 (tsc --strict 도 받는 파일), 자체 C++ 컴파일러 (Node 없음),
  number = f64 · i32 = 32 비트 감싸기 (JS 의 | 0 · Math.imul), f32 는 필드 종류, GC · 예외 · 스크립트 전역 가변 상태 없음 —
  호출 아레나, 트랩 = 그 호출의 쓰기를 버림, 정적 import 만 (순환 오류), 소스가 패키지에 들어가고 로드 때 컴파일 · 검증기는 항상.
  모듈 script/ = SandboxScript 하나 (Foundation 만) + 바인딩 core/script.
- ADR-0057 (D-04): Host API 단일 정의 = C++ constexpr 표 (script/api) → 컴파일러 타입 검사 · 로드 때 이름으로 묶기 (인덱스
  영속 없음, .sbc 에 표 지문) · 바인딩 등록 검사 · sandbox.d.ts 생성 (골든 파일). 쓰기는 호출 단위 버퍼 (read-your-writes),
  질의는 saveId 오름차순, 엔진 관리 컴포넌트 · 범위 밖 · 죽은 개체 쓰기 = 트랩, 난수 Purpose::Script, hostApiVersion.
- 18: 0장 (다음 = v0.3 M1-T1), 1 · 3장, 6.1 (D-01 · D-04 결정, D-03 = M3-T1), 6.2 v0.3 마일스톤 M1~M6 · 완료 기준, 7장.
  03 6.4 (ADR 연결), 16 (버전 줄), adr/README.

**왜**

- v0.3 진입 조건 (18 6.1 "D-01 v0.3 진입 전"). 언어 · Host API 계약이 정해져야 컴파일러 · VM · 바인딩을 작은 태스크로 나눌 수 있다.
- 결정의 축은 04 (결정론 최우선): 스크립트 상태가 컴포넌트 밖에 없으면 세이브 · 리플레이 · 해시 · 복제가 새 책임을 지지 않는다.

**검증**

- 문서만 (코드 변경 없음). Windows MSVC Debug ctest 61/61 (render_tests_warp 제외) — 아래 커밋 직전 실행.

**남은 일**

- v0.3 M1-T1: 규범 문서 19-SANDBOXSCRIPT · 모듈 script/ · 렉서 · 파서 · 진단.
- D-03 (동적 풀) 은 M3-T1, 스크립트 단계는 M4-T1 의 ADR.

## 2026-10-21 — v0.2 M6-T2: 버전 종료 — 요구사항 done / deferred / removed, 제품 버전 0.2.0

**무엇을**

- 제품 버전 CMake `project(Sandbox VERSION 0.2.0)` (BuildInfo → `sbx --version` · `SandboxServer --version` = 0.2.0, 새 프로젝트의
  engine 기본 범위 ">=0.2.0 <1.0.0"). 샘플 rabbit_guard 의 ">=0.1.0 <1.0.0" 은 그대로 맞는다.
- CTest sample_playthrough: 프로젝트로 두 호스트를 비교한 뒤 `sbx build` 로 패키지를 만들어 `--world <.sbpkg>` 로도 헤드리스 서버 ·
  로컬 플레이가 같은 "게임 결과:" 줄을 쓰는지 본다 (작업 폴더 = 빌드 폴더의 sample_playthrough, 끝나면 지운다).
- 릴리스 노트 `docs/releases/v0.2.0.md` (새 폴더 — 이력, 버전마다 새 파일): 들어간 것 (M1~M6 · ADR), 호환성 번호 (제품 0.2.0 ·
  프로토콜 12 · kSimVersion 4 · 세이브 1 · 1 · 매니페스트 1 · 2 · 패키지 format 1), 초안 요구 3 · 5 · 9장의 done / deferred / removed
  표와 근거 시험, 미룬 것의 버전 재할당, 알려진 문제.
- 문서: 18 (0 · 1 · 2 · 4장, 5.1 M6-T2 ✅, 5.3 요구사항 검토, 7 · 8장), 13 5.1, 16 (버전 줄), 17 (docs/releases), README (문서 지도).

**왜**

- v0.2 DoD 4 "미구현 항목을 릴리스 노트에 명시하고 다음 버전으로 재할당". 요구 6 "하나의 GamePackage 를 헤드리스 서버와 로컬
  플레이에서" 는 M6-T1 이 프로젝트 폴더로만 비교했다 — 패키지 경로도 자동 시험으로 묶었다.
- 18 2장의 SaveGame · Visual Trigger · Edit ↔ Test Play 가 "Partial (M4 · M5 계획)" 으로 남아 있었다 — 구현에 맞춰 고쳤다.

**검증**

- Windows MSVC Debug (windows-msvc): 빌드, `chcp 65001` 뒤 ctest 61/61 통과 (render_tests_warp 제외 — 알려진 문제).
- sample_playthrough: 프로젝트 · 패키지 × SandboxServer --script · SandboxClient --headless --script 모두
  `게임 Finished · win · team 0 · rule 3 · 점수 -60 · tick 3691 · hash 0x02d03b52e9655a3f`.
- 골든 det_* 그대로 (시뮬레이션 코드 변경 없음, kSimVersion 4).

**남은 일**

- v0.3 진입: D-01 (SandboxScript 언어 부분집합) · D-04 (Host API 단일 정의) ADR → 18 6장을 마일스톤으로.
- v0.1 열린 항목 (실기 QA · 50k 판정 · Actions 결과) 그대로. 미룬 것 목록은 releases/v0.2.0 5장.

## 2026-10-21 — v0.2 M6-T1: 데모 미니게임 samples/rabbit_guard · 자동 플레이 스크립트 — 헤드리스 서버 = 로컬 플레이 (ADR-0055)

**무엇을**

- `samples/rabbit_guard/` — 커밋한 게임 프로젝트 (sbx new --from ecosystem_small --mode eco.protect 그대로) + README +
  `playthrough.json` (8 배속 · 시작 · 늑대가 처음 있던 자리에 덫 15 번 · 30 초마다 먹이, expect = Finished · win · rule 3 ·
  tick 3691 · 해시 (MSVC 키)).
- `core/scenarios/PlayScript`: 스크립트 읽기 (format 1 · steps {at, command — 리플레이 JSON} · maxTicks · expect — 해시는 툴체인
  키별), attachPlayScript (ScenarioRunner::replaceScenario — 서버 러너의 시나리오로), playFinished · describePlayResult ("게임 결과:"
  한 줄 — 상태 · 결과 · team · rule · 점수 · tick · hash) · checkPlayExpectation · runPlayScript.
- ServerHostDesc.stopWhenGameFinished + reachedStop (게임 Finished — 자동 재시작 모드 말고 — 이거나 stopAtTick). --exit 와
  --script 가 켠다 → `--ticks N --exit` 가 일찍 끝난 게임에서 멈추지 않던 문제도 고쳤다 (테스트 플레이 중은 제외).
- SandboxServer · SandboxClient `--script <파일>` (클라이언트는 LocalServerDesc.script · AppConfig::quitWhen — 헤드리스면
  로컬 서버가 멈출 때 끝, 서버 월드의 결과 줄). expect 가 다르면 종료 코드 1.
- 시험: test_play_script (3 — 저장소 샘플을 같은 프로세스에서 끝까지 두 번), test_server_options +1, CTest sample_playthrough
  (헤드리스 서버와 로컬 플레이의 결과 줄이 같다).
- 문서: ADR-0055, 13 5.1, 15 7장, 17, 18 (M6-T1 ✅ — 다음 M6-T2), adr/README, samples/rabbit_guard/README.

**왜**

- v0.2 완료 기준 "템플릿만으로 미니게임을 만들어 저장 · 실행 · 종료" 를 사람 없이 자동으로. 서버가 넣는 명령은 러너 단계에
  묶여 있어 실시간 서버 (Threaded) 와 고정 dt 로컬 플레이 (Inline) 가 같은 입력을 받는다.

**검증**

- Windows MSVC Debug: ctest 61/61 통과 (render_tests_warp 제외). 수동: SandboxServer --script 16 초 · SandboxClient --headless
  --script 9 초, 둘 다 "게임 Finished · win · team 0 · rule 3 · 점수 -60 · tick 3691 · hash 0x02d03b52e9655a3f".
  sbx check samples/rabbit_guard 오류 · 경고 0.
- 골든 · 와이어 · 세이브 형식 · kSimVersion 그대로.

**남은 일**

- M6-T2 버전 종료 (요구사항 정리 · 제품 버전 0.2.0). 봇 클라이언트 (접속자 입력) 자동 플레이, 스크립트 녹화, Linux 툴체인의 샘플
  해시 기록 — [계획].

---

## 2026-10-20 — v0.2 M5-T2: 게임 패키지 .sbpkg · 세이브 결속 → M5 완료 (ADR-0054, D-02 결정)

**무엇을**

- `foundation/hash/Sha256` (FIPS 180-4).
- `core/package/GamePackage`: .sbpkg = magic · format 1 · index(JSON, 키 정렬) · 파일 바이트. 파일마다 SHA-256, digest =
  SHA-256("<sha256> <size> <path>\n" path 순). buildPackage (openProject 검사 → 매니페스트 · 읽은 팩 · 시작 월드만 → 원자적
  쓰기), readPackageIndex · verifyPackage · extractPackage (캐시 <임시>/sbx-packages/<digest 16>/, 다시 쓸 때 파일마다 확인),
  packageSavesRoot (<이름>-saves). 압축 · 서명 없음 (D-02).
- `openWorldSource`: .sbpkg 를 검증 · 풀어 프로젝트로 연다 (WorldSource.package · projectDir — 재시작의 시작 월드는 풀어 둔 폴더).
  서버 · 로컬 서버 · 클라이언트 세이브 기본값이 패키지 옆.
- 세이브 결속: persist::GameBinding · SaveOptions.game → world.json "game" {gameId, version, package?}. ServerHost 가 저장 ·
  기록 시작 세이브에 쓰고, 불러오기에서 다른 게임 거절 · 다른 버전 · 패키지 빌드는 결과에 알린다. SaveSummary.game.
- sbx: `build <폴더> [--out]`, `check <파일.sbpkg>`.
- 시험: test_sha256 (2), test_game_package (4), test_world_ops +1 (결속), CTest project_package.
- 문서: ADR-0054, 09 (2 · 9장), 15 7장, 13, 17, 18 (D-02 결정 · M5 ✅ — 다음 M6-T1), adr/README.

**왜**

- 게임 하나를 파일 하나로 주고받고, 고친 파일을 거절하고, 같은 프로젝트는 같은 바이트 (초안 02 7장 A · B · E). 세이브가 어느
  게임의 것인지 알아 다른 게임 세이브를 막는다.

**검증**

- Windows MSVC Debug: ctest 60/60 통과 (render_tests_warp 제외). 수동: 데모 프로젝트를 두 번 빌드 → 같은 바이트, sbx check 오류 ·
  경고 0, `SandboxServer --world <패키지> --start-game --ticks 120 --exit` 정상 · 세이브 폴더가 패키지 옆, 한 바이트를 고친
  패키지는 sbx check · 서버 모두 "sha256 과 다르다" 로 거절.
- 골든 · 와이어 · kSimVersion · worldVersion · schemaVersion 그대로 (world.json "game" 은 선택 키).

**남은 일**

- M6-T1 데모 미니게임 프로젝트 (samples/). 압축 · 서명 · 흐름 읽기, 원격 클라이언트의 패키지 받기, 캐시 지우기, 게임 버전 간
  세이브 마이그레이션 — [계획].

---

## 2026-10-20 — v0.2 M5-T1: 테스트 플레이 — 편집 월드는 서버 메모리에 맡기고 복사본으로, 멈추면 되돌린다 (ADR-0053)

**무엇을**

- ServerHost: WorldOp `TestPlay` — 편집 월드를 임시 폴더에 저장하고 다시 열어 복사본을 만들고 (폴더는 바로 지움), 원본 runner 는
  `m_editRunner` 에 그대로 맡긴다 (replaceWorld 의 keepOld). 게임 프로젝트면 복사본의 세션을 Lobby 로 놓고 GameControl Start,
  일시정지였으면 ResumeSimulation 을 서버가 넣는다. `TestStop` — 복사본을 버리고 맡긴 원본으로 되돌린다.
  그 동안 불러오기 · 재생 · 재시작 · 기록 시작 거절, 저장은 된다, 자동 재시작 없음. ServerStats mode 3.
- 프로토콜 11 → 12: WorldOp op 8 · 9, WorldChanged reason 4 · 5, ServerStats mode 3.
- 클라이언트 "월드" 패널: "테스트 플레이" · "테스트 플레이 멈추기 (되돌리기)" 단추, 상태 줄. WorldOp 열거 값 TestPlay · TestStop.
- 시험: test_game_control +1 (서버 끝까지), test_world_panel +1 (로컬 서버) · 패널 모드 3, test_messages +1 (프로토콜 12).
- 문서: ADR-0053, 08 (머리말 · 5장 표 · 프로토콜 12), 10 9장, 13, 18 (M5-T1 ✅ — 다음 M5-T2), adr/README.

**왜**

- 편집 → 플레이 → 되돌리기를 저장 · 불러오기 없이. 원본 객체를 그대로 맡겨 "원본 오염 없음" 을 가장 확실하게 (세이브에 담지
  않는 상태까지 그대로).

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외). 멈춘 뒤 편집 월드의 틱 · WorldHash · 개체 수 · 세션 Lobby 가 시작
  전과 같다 (test_game_control). 골든 · 세이브 형식 · kSimVersion 그대로.

**남은 일**

- M5-T2 GamePackage. 메모리 직렬화 (임시 폴더 없이), 여러 사람이 각자 테스트 플레이 — [계획].

---

## 2026-10-20 — v0.2 M4-T3: 트리거 편집 — 오버레이 종류 Trigger (프로토콜 11) · "콘텐츠" 창의 트리거 폼 → M4 완료 (ADR-0052)

**무엇을**

- 오버레이 종류 `ContentKind::Trigger` (값 3, "trigger", 폴더 "triggers"): EditContent 로 트리거를 바꾸거나 더하거나 뺀다.
  로더 loadTriggerJson (팩 · 오버레이 공용) — 바꾼 것은 팩의 실행 순서 자리, 더한 것은 맨 뒤 (TriggerDef.overlay).
  와이어 kind 0~3 → 프로토콜 11. 세이브 overlay/triggers/<id>.json (worldVersion 그대로). 리플레이 {"kind": "trigger"}.
- 에디터 `editor/ui/TriggerForm`: 표(TriggerSchema) 기반 노드 양식 (종류별 위젯 · 선택 키 "사용" · 노드 더하기/지우기 · id 바꾸면
  연결도), 연결 표 (from → to · else), 이름 · 틱마다. 순수 함수 triggerTemplate · defaultTriggerNode · freeNodeId ·
  removeTriggerNode · describeTriggerNode · describeProgram.
- "콘텐츠" 창: 종류 "트리거", 폼 · JSON 탭 (같은 글), 요약에 실행 표현 줄. 검사 · 적용 · 되돌리기는 기존 경로.
- 시험: test_trigger_form (editor 4), test_trigger_run +1 (오버레이 순서 · 게임 중 EditContent · 세이브 왕복), test_messages ·
  test_roles_overlay (프로토콜 11 · trigger 왕복 · kind 4 거절).
- 문서: ADR-0052, 08 (프로토콜 11), 09 3장, 10 2 · 8장, 11 10장, 13, 17, 18 (M4 ✅ — 다음 M5-T1), adr/README.

**왜**

- 트리거를 에디터에서 만들고 고치게 (v0.2 "비개발자가 템플릿으로"). 오버레이를 그대로 쓰면 서버 검사 · 역할 · 되돌리기 · 세이브 ·
  복제 · 해시가 따라온다. 폼은 표에서 위젯을 만들어 노드 종류가 늘어도 코드가 늘지 않는다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외). 골든 그대로 (오버레이 없는 월드). 모든 노드 종류의 기본값이
  로더 검사를 통과 (시험). 세이브 왕복 후 같은 해시.
- 와이어: 프로토콜 10 → 11 (열거 값). 세이브 형식: 선택 폴더 하나 (worldVersion 1). kSimVersion 그대로.

**남은 일**

- M5-T1 Test Play. 노드 그래프 캔버스, 행동 · 규칙 폼, 오버레이를 팩으로 내보내기 — [계획].

---

## 2026-10-20 — v0.2 M4-T2: Trigger 실행 — 단계 17c, 틱 처음 스냅숏, 같은 틱 이어짐, 예산 (ADR-0051)

**무엇을**

- 단계 17c Triggers (`core/simulation/SimulationTriggers.cpp` — SimulationWorld 의 일부): 세션이 Running 일 때만. 단계 0 (명령 전)
  스냅숏 = 점수 칸 + (area_enter · death 트리거가 있으면) 개체 saveId · 위치 · 태그. 처음 목록 (game_start · tick · timer ·
  score 가장자리 · area_enter 밖→안 · 이번 틱 Died) 을 FIFO 로, 효과가 낸 이벤트 (destroy → death · spawn → area_enter ·
  score.add → score) 는 같은 틱에 목록 끝으로. 예산 perTick · 틱 1024 실행 · 65536 줄 (lastTriggerStats). game.end 로 끝.
  조건 count · score · time · field · tags · chance (Purpose::Trigger).
- 효과 실행기 `applyEffectNow` — PlayerAction 의 효과 코드를 옮겨 Trigger 와 공용 (같은 동작 · 같은 난수 키).
  game::scoreSlot · game::finishGame (GameRules).
- eco 팩 트리거 `eco.rabbit_lost` (토끼가 죽으면 점수 −1) — 데모 "토끼 지키기" 의 점수 = 덫 − 잃은 토끼.
- 시험: test_trigger_run (core 8). 문서: ADR-0051, 03 2장 (17b · 17c 표 · 글 · 그림), 04 · SimVersion 이력, 11 (0 · 10장),
  13, 17, 18 (M4-T2 ✅ — 다음 M4-T3), adr/README.

**왜**

- M4-T1 의 실행 표현을 서버 권위 · 결정적으로 돌린다. 앞 상태를 저장된 상태에서 다시 만드는 스냅숏으로 잡아 세이브/로드한
  월드도 같다. 같은 틱 이어짐이라 "덫 → 죽음 → 점수 → 보너스" 가 읽히는 대로 한 틱에 끝나고, 예산이 고리를 끊는다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외). 골든 · 리플레이 해시 그대로 (세션 없는 월드), PlayerAction
  시험 그대로 (공용 실행기). 세이브 왕복 후 같은 해시 (chance · 스냅숏). `sbx_sim_check --validate-content eco` 오류 · 경고 0.
- 와이어 · 세이브 형식 · kSimVersion 그대로. eco contentHash 는 바뀐다 (triggers/ 추가).

**남은 일**

- M4-T3 트리거 에디터 폼 · 오버레이로 고치기. 영역 나감 · 머묾, 세션 없는 월드의 트리거, 스냅숏 비용 측정 — [계획].

---

## 2026-10-20 — v0.2 M4-T1: Visual Trigger 스키마 — triggers/*.json · 노드 종류 표 · 실행 표현 · V10 (ADR-0050)

**무엇을**

- `core/trigger/TriggerSchema`: 노드 종류 표 21 종 (event 6 · cond 6 · effect 9) — 키 · 타입 · 범위 · 필수, 대상(subject) 규칙,
  권한 (모두 ServerGameplay). 로더와 에디터 폼(M4-T3)이 같은 표를 읽는다.
- `core/trigger/TriggerCompiler::compileGraph`: 원본 그래프의 모양 검사 (이벤트 하나 · 이벤트로 들어옴 없음 · 들어오는 연결 하나 ·
  순환 · 닿음 · else 는 조건에서만 · 노드 64 · 깊이 16 · 대상) 와 나무 → 명령열 (Cond · Effect · Jump) 펼치기.
- 콘텐츠: 팩의 `triggers/*.json` → `TriggerDef` (event · conditions · effects · program · perTick · raw 원본).
  ContentLoader::loadTrigger — 값은 표로 (V3 · V7 · V2 · 초는 V8), 모양은 V10. crossCheck: id 중복 V1, 256 개 V10, 팀 id V2 경고,
  재진입 고리 (score.add → event.score · destroy → event.death · spawn → event.area_enter) V10 경고. ContentDatabase::triggers ·
  findTrigger. 효과는 RuleEffect 를 그대로 (실행기 재사용 — M4-T2), game.end 만 새것.
- 시험: test_trigger (content 5).
- 문서: ADR-0050, 11 (1 · 8 · 10장), 03 6.4, 13, 17, 18 (M4-T1 ✅ — 다음 M4-T2), adr/README.

**왜**

- 월드 수준 규칙 (타이머 · 영역 · 점수 · 죽음 → 조건 → 효과) 을 콘텐츠로. Rule 은 개체 둘의 interact 에 묶여 있어 못 쓴다.
- 나무 + 펼친 명령열: 실행 순서가 분명하고 예산 · 깊이 검사가 쉽다. 원본(편집 배치)과 실행 표현을 나눠 에디터가 배치를 바꿔도
  실행은 같다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외, `chcp 65001` 뒤). 골든 · 리플레이 해시 그대로 — 시뮬레이션은
  아직 트리거를 읽지 않는다. 와이어 · 세이브 · kSimVersion 그대로. eco 팩에는 트리거를 넣지 않았다 (contentHash 그대로).

**남은 일**

- M4-T2 실행: Stage 위치, 이벤트 수집 (영역 진입 = 이전 틱 위치 비교, 점수 가장자리), cond.chance 난수 키, perTick 예산.
- M4-T3 에디터 폼 · 월드 오버레이로 고치기. 합류 · 변수 · 반복 · 대기 노드 — [계획].

---

## 2026-10-20 — v0.2 M3-T3: 게임 키 · 카메라 처음 자리 · 데모 모드 "토끼 지키기" → M3 완료 (ADR-0049)

**무엇을**

- 게임 키: 콘텐츠 팩의 `presentation/input.json` (sandbox.input 형식) — 로더는 원문만 `PackInfo.inputBindings` 로 (contentHash ·
  검사 밖). 클라이언트 `apps/client/PlayKeys` 가 팩 순서로 합치고 play.* 만 남기며 (다른 이름 경고) `--input` 의 play.* 가 마지막에
  이긴다, 엔진 키와 겹치면 경고. `play.action.<id>` 를 누르면 커서 자리에 PlayerAction (Application::handlePlayKeys — 엔진 키와
  따로 둔 ActionMap · ActionState). "행동" 패널이 키를 보인다. `ActionMap::withPrefix` 추가.
- 카메라 처음 자리: 컴포넌트 `game.camera_start {height}` (월드에 놓는다). NetworkSession::cameraStart() (netId 가 가장 작은 것 ·
  generation), Application 이 월드마다 한 번 · Home 으로 맞춘다 (없으면 월드 전체).
- 데모: `content/ecosystem` 에 modes/protect.json (eco.protect "토끼 지키기"), actions.json (eco.feed · eco.trap),
  presentation/input.json (Q · E). `createProject` 의 gameMode · `sbx new --mode` (없는 모드는 NotFound + 있는 모드 목록).
- 시험: test_play_keys (client 4), test_game_score +1 (저장소 eco 의 데모 진행), test_game_project +1, test_content_loader +1,
  test_action_map (withPrefix), 카탈로그 22, CTest project_demo_protect.
- 문서: ADR-0049, 02 · 07 · 10 · 11 · 13 · 15 · 17 · 18 (M3 ✅ — 다음 M4-T1), adr/README.

**왜**

- 게임이 자기 키 · 처음 화면을 주고, `sbx new` 한 줄로 바로 해 볼 수 있는 미니게임이 있어야 v0.2 완료 기준("템플릿만으로
  미니게임")에 닿는다. 키는 표현이라 팩의 클라 전용 폴더에 두어 원격 클라이언트도 받고 해시는 그대로다.

**검증**

- Windows MSVC Debug: ctest 59/59 통과 (render_tests_warp 제외 — M3-T2 항목 참고, `chcp 65001` 뒤). 골든 · 리플레이 해시 그대로
  (det_*), 프로토콜 10 · 세이브 형식 · kSimVersion 그대로.
- 런타임: `sbx new <d> --from ecosystem_small --mode eco.protect` → `SandboxClient --headless --world <d> --frames 90` (InWorld,
  Lobby tick 1 · 개체 301, 게임 키 경고 없음) · CTest 의 `SandboxServer --start-game --ticks 150`.
- eco 팩 contentHash 가 바뀌었다 (actions.json · modes/ 추가) — 예전 eco 세이브 · 리플레이는 P5 경고로 열린다.

**남은 일**

- M4-T1 Visual Trigger 스키마.
- 행동 말고 다른 play.* 키, 카메라 따라가기 · 이동 범위, 카메라 처음 자리 표시 아이콘 — [계획].
- (이전 항목의 알림 그대로) `--ticks N --exit` 이 게임이 일찍 끝나면 멈추는 문제는 따로 고친다.

---

## 2026-10-20 — v0.2 M3-T2: 점수 · 팀 · 스폰 지점 — game.score · 효과 score.add · 끝 조건 score · 팀 태그 · game.spawn_point (ADR-0048)

**무엇을**

- 컴포넌트 `game.score` (core/components/game/GameScore.hpp): points[9] — 0 공용, 1..8 GameMode.teams 순서. 세션 엔티티에
  붙고 엔진 관리 (명령 PermissionDenied · Prefab V3). resetSession · GameControl Start 가 0 으로 (없으면 붙인다).
- 효과 op `score.add {value 정수 ±1,000,000, team?, who 기본 target}` — Rule (Stage 11) · PlayerAction 이 `game::addScore` 를 쓴다.
  칸 = team → who 의 팀 태그 → 공용. 세션이 Running 일 때만, ±10억 으로 자른다. 어느 모드에도 없는 team 은 V2 경고.
- GameMode: `teams[].tag` (팀 소속 = 태그, V7 · 중복 V8), 끝 조건 `when.score {team?, op, value}` (time · count 와 AND).
- 컴포넌트 `game.spawn_point {prefab, count 1~64, radius 0~16}` (사용자 편집): GameControl Start 를 적용하면 saveId 순으로 지점마다
  prefab 을 ±radius 안에 만든다 (난수 Purpose::GameStart, 없는 prefab 건너뜀, 4096 개까지, 결과 created).
- 클라이언트 "게임" 패널 점수 줄 (GameInfo.scores · scoreRows · scoreLine).
- 시험: test_game_score (core 5), test_content_loader +3, test_game_panel +1 (+ 로컬 서버 복제 점수), 카탈로그 21.
- 문서: ADR-0048, 11 (0 · 8 · 9장), 03 (2 · 4 · 6.3장), 02, 04 (simVersion 이력), 10 9장, 13, 17, 18 (M3-T2 ✅ — 다음 M3-T3),
  adr/README, SimVersion.hpp 이력 주석.

**왜**

- "10 점 먼저" · 팀 대결 같은 미니게임을 콘텐츠만으로 (M3 Feature Bundle). 점수가 세션과 수명이 같아 재시작 = 0 점이고,
  세이브 · 리플레이 · 복제 · 인스펙터가 그대로 다룬다. 팀을 태그로 정하면 Prefab · 태그 식 · V7 을 그대로 쓴다.
- 스폰 지점: 시작 월드를 고치지 않고 판마다 같은 배치 (지점 saveId 로 결정적).

**검증**

- Windows MSVC Debug (windows-msvc 프리셋): ctest 58/58 통과 (render_tests_warp 제외). 골든 · 리플레이 해시 그대로
  (det_* 통과 — kSimVersion 4 그대로). 와이어 (프로토콜 10) · 세이브 형식 변경 없음.
- 런타임: `sbx new --from ecosystem_small` 프로젝트에 팀 태그 · score 끝 조건 모드와 늑대 Rule 의 score.add 를 넣고
  `SandboxServer --world <프로젝트> --start-game --ticks 600 --exit` 두 번 — 같은 해시 0x12019dabc96ea151.
- 알림 (이번 변경 전부터, 고치지 않음):
  - Windows 에서 CMake 가 도구의 UTF-8 출력을 콘솔 코드 페이지(cp949)로 읽어 project_new_* · project_game_start ·
    net_server_probe_* 가 실패한다 — `chcp 65001` 뒤 ctest 면 통과.
  - render_tests_warp: doctest 28/28 통과지만 D3D12 Debug Layer 배리어 메시지 (imgui target · world pass target 의
    COMMON → COPY_SOURCE) 로 "render tests OK" 를 내지 않는다.
  - 수동 재시작 모드에서 게임이 N 틱 전에 Finished 가 되면 `--ticks N --exit` 가 끝나지 않는다 (Finished 는 틱을 멈춘다).

**남은 일**

- M3-T3 카메라 · 입력 바인딩 기본값, 데모 모드 (생태계 "토끼 지키기").
- 다시 나오기 (respawn) · 주기적 스폰, 접속자별 점수, 가장 높은 팀 승리 (M4 트리거), 스폰 지점 표시 아이콘 — [계획].

---

## 2026-10-20 — v0.2 M3-T1: 플레이어 행동 — 콘텐츠 PlayerAction (actions.json) · 명령 PlayerAction · "행동" 패널 (ADR-0047)

**무엇을**

- 콘텐츠: 팩의 `actions.json` → `content::PlayerActionDef` (id · name · targets · range 0~32 · maxTargets 1~64 · conditions ·
  effects). 검사 V9 (who · 조건 경로는 target 뿐, 범위, 팩 전체 64 개) + V3 · V7 · V1. Rule 의 조건 · 효과 파서를
  `parseRuleConditions` · `parseRuleEffects` 로 떼어 Rule 과 함께 쓴다 (Rule 동작 그대로 — 기존 content 시험 통과).
- 명령 `PlayerAction{action, position}` (와이어 태그 30, 리플레이 JSON "action"): SimulationWorld 가 명령 단계에서 대상을
  고르고 (core.transform · 반경 · 태그 · 조건 · (거리², saveId) 순 maxTargets) 효과를 바로 적용 (field · destroy · spawn
  (난수 Purpose::PlayerAction) · tag · event). 대상이 없으면 거절 (원자적). 게임 세션이 있으면 Running 에서만.
  CommandClass::PlayerAction = 역할 player 부터. 프로토콜 10.
- 클라이언트 "행동" 패널 (apps/client/ui/ActionPanel): 행동을 쥐면 월드 왼쪽 클릭 = PlayerAction (에디터 · 선택은 클릭을 받지
  않는다), Esc = 놓기, 결과 · 거절 줄. NetworkSession::playerActions() · playerAction().
- 시험: test_player_action (core 5), test_content_loader +2, test_action_panel (client 2), test_messages (프로토콜 10 ·
  태그 30), test_multi_client 역할 표 +PlayerAction (player 부터).
- 문서: ADR-0047, 11 (머리말 · 0 · 6 · 8장 V9), 03 (0 · 4장), 08 (머리말 · 5 · 5.1장), 10 7 · 9장, 13, 17, 18 (M3-T1 ✅ —
  다음 M3-T2), adr/README.

**왜**

- D-11 (ADR-0045) 결정의 구현. 역할 player 가 처음으로 쓸 수 있는 명령이고, 콘텐츠만으로 플레이어 상호작용을 만든다.
- 대상을 시뮬레이션이 위치로 고른다: 복제 지연 · 관심 영역 밖 · 치트 검사가 필요 없고 리플레이에서 같다.

**검증**

- Linux clang 19 Debug -Werror: ctest 63/63. 골든 · 리플레이 해시 그대로 (명령이 없으면 아무것도 하지 않는다).
- 와이어: 프로토콜 9 → 10 (명령 태그 하나). 세이브 형식 · kSimVersion 그대로.

**남은 일**

- 행동 쿨다운 · 비용 (플레이어별 상태), 대상 직접 지정 · 인자, 행동 단축키, 월드 오버레이로 행동 고치기 — [계획].
- M3-T2 점수 · 팀 (game.score, 효과 op score.add, 끝 조건 score), 스폰 지점.

---

## 2026-10-20 — v0.2 M2-T3: 게임 시작 · 중단 · 재시작, "게임" 패널, 프로토콜 9 → M2 완료 (ADR-0046)

**무엇을**

- 명령 `GameControl{start | abort}` (core/command, 와이어 태그 24, 리플레이 JSON {"op": "game", "action": …}):
  start = Lobby → Starting, abort = Starting · Running → Finished (결과 없음). 권한 = 실행 제어 (admin 부터).
  서버는 역할 player 이상 접속자가 GameMode.players.min 에 모자라면 start 를 거절한다.
- `game::resetSession` — 세션을 Lobby 로 (없으면 만든다). openProject 가 gameMode 가 있으면 부른다 (시작 월드는 언제나 Lobby).
- WorldOp `Restart` (op 7): 서버가 시작 월드를 다시 열고 (openSave — contentHash 가 달라도 경고만) 세션을 Lobby 로 →
  WorldChanged{Restart}. GameMode.restart auto 면 Finished 뒤 delay 만큼 서버 단계가 지나면 스스로.
  `net::setGameProject` 로 서버 · 로컬 서버가 시작 월드 · 모드 · players.min 을 받는다. 프로토콜 9.
- SandboxServer `--start-game` (헤드리스 · CI 에서 Lobby 를 넘는다).
- 클라이언트 "게임" 패널 (apps/client/ui/GamePanel): 모드 · 상태 한 줄 (카운트다운 · 경과 · 결과와 팀) · 시작 · 중단 · 재시작 ·
  거절 줄. NetworkSession::game() 은 복제된 game.session 을 읽는다 (클라이언트에는 saveId 가 없어 세션 찾기에 대체 규칙).
- 시험: test_game_control (network 3), test_game_panel (client 2), test_game_rules +2, test_game_project (openProject = Lobby),
  test_messages (프로토콜 9 · 태그 24), test_multi_client 역할 표 +2 명령 · Restart, test_server_options, CTest project_game_start.
- 문서: ADR-0046, 08 (머리말 · 4 · 5 · 5.1장), 10 7 · 9장, 15 7장, 03 (2 · 4장), 13, 17, 18 (M2 ✅ — 다음 M3-T1), adr/README.

**왜**

- 시작 · 중단은 시뮬레이션 상태 전이라 명령이어야 리플레이 · 결과 · 역할 검사를 그대로 탄다. 재시작은 한 판의 모든 변경을
  되돌려야 해서 시작 월드를 다시 여는 것(12D 불러오기와 같은 경로)이 가장 확실하다.

**검증**

- Linux clang 19 Debug -Werror: ctest 63/63 (+ project_game_start). 골든 · 리플레이 해시 그대로 (세션 없는 월드).
- 와이어: 프로토콜 8 → 9 (명령 태그 · 열거 값만 늘었다). 세이브 형식 변경 없음.

**남은 일**

- players.max (접속 · 역할 제한), 준비(ready) · 팀 고르기, 여러 맵 순환 — [계획].
- M3-T1 PlayerAction (콘텐츠 actions.json, 역할 player).
- (알림) ADR-0045 에 M2-T2 에서 "구체화" 절을 더했다 — AGENTS.md 6장은 기존 ADR 을 고치지 않는다고 하므로, 이후 결정은
  ADR-0046 처럼 새 ADR 로 쓴다.

---

## 2026-10-20 — v0.2 M2-T2: game.session · 단계 17b GameRules (ADR-0045 구체화)

**무엇을**

- 컴포넌트 `game.session` (core/components/game/GameSession.hpp): mode · state(Lobby · Starting · Running · Finished) ·
  stateTick · result · team · rule. Persistent · Replicated · EditorVisible. 엔진 관리 — 사용자 명령(Create · Add ·
  Change · Remove)은 PermissionDenied, Prefab 의 components 는 V3.
- `core/game/GameRules`: 세션 찾기 (saveId 가 가장 작은 것), `gameHoldsSimulation` (Lobby · Finished), 끝 조건 평가
  (time · count(core.tags 태그 식) · AND), `stepGameRules` (Starting 경과 ≥ 카운트다운 → Running, Running 에서 end 를
  위에서부터 → Finished).
- SimulationWorld::tick: 세션이 Lobby · Finished 면 편집 단계 (clock.paused 와 별개, Step 을 쓰지 않는다), 17 (ECB ·
  Prefab 생성) 다음에 단계 17b GameRules.
- 시험: tests/unit/game/test_game_rules.cpp 8 케이스 (unit_core), unit_ecs 카탈로그 19, unit_content Prefab 의 game.session.
- 문서: ADR-0045 구체화 절, 03 (0 · 2장 17b · 그림), 04 6장 이력 (4), 02 (game.session), 11 9장, 17, 18 (T2 ✅ — 다음 T3),
  13, SimVersion.hpp 이력.

**왜**

- 게임의 진행 상태가 시뮬레이션 안에 있어야 세이브 · 리플레이 · WorldHash · 복제가 게임 진행을 안다 (ADR-0045 2).
- System(Stage 5~16)이 아니라 17 다음 단계로: ECB 적용 뒤라 그 틱의 파괴 · 생성이 끝 조건 수에 바로 보이고, 기존 System
  순서가 그대로다.

**검증**

- Linux clang 19 Debug -Werror: ctest 62/62. 골든 det_random_walk · det_world_save_load · det_ecosystem_small ·
  det_eco_lifecycle 과 리플레이 det_replay_* 그대로 — 세션이 없는 월드의 결과가 같다 (kSimVersion 4 그대로).
- 와이어 형식 변경 없음 (복제 표는 Welcome 이 stableId 로 싣는다 — 새 Replicated 컴포넌트는 같은 빌드끼리 자동).
  세이브: 새 컴포넌트만 더해졌다 (옛 세이브는 그대로 읽힌다).

**남은 일**

- M2-T3 시작 · 재시작 명령 (세션 만들기 · Lobby → Starting · Finished → 재시작), 역할 권한, 클라이언트 "게임" 패널,
  restart.auto 의 지연.
- M3-T1 PlayerAction.

---

## 2026-10-20 — v0.2 M2-T1: GameMode 콘텐츠 · 매니페스트 schemaVersion 2 gameMode — D-11 결정 (ADR-0045)

**무엇을**

- 콘텐츠 팩의 `modes/<이름>.json` → `content::GameMode` (id · name · description · players{min, max} · teams ≤ 8 ·
  start{countdownSeconds} · end[{when{time, count}, result, team?}] ≤ 32 · restart{manual|auto, delaySeconds}). 초는 로드할
  때 틱으로 (×30). `ContentDatabase::modes()` · `findMode(id)`, contentHash 에 든다. 검사 규칙 V8 (모르는 키 V3, 태그 V7,
  중복 id V1) — 문제가 있으면 그 모드는 넣지 않는다.
- 매니페스트 schemaVersion 2 (1 도 읽는다): 선택 키 `gameMode` (콘텐츠 id). v1 에서 쓰면 P1, 프로젝트 콘텐츠에 없으면 P5.
  `sbx check` 가 "게임 모드" 줄을 찍는다.
- ADR-0045: GameMode = 콘텐츠, 진행 상태 = 시뮬레이션 안의 game.session (M2-T2), 플레이어 행동(D-11) = 콘텐츠 PlayerAction
  부터 (M3-T1), 유닛 지시 · 직접 조작은 뒤로.
- 시험: unit_content 3 케이스 (로드 · 해시 · V8 오류 모음), unit_project 2 케이스 (v2 gameMode, P5).
- 문서: ADR-0045, 11 (1 · 8 · 9장 GameMode), 18 (2 · 5.2 · 6.1 D-11 · 7 · 8장, 다음 M2-T2), 13, adr/README.

**왜**

- 엔진에 "게임"(시작 · 끝 · 승패)이 없다. 승패 규칙은 게임 규칙이라 팩에 둔다 — V-규칙 검사 · contentHash 일치 · 오버레이 ·
  공유를 그대로 얻는다. 데이터를 먼저 고정하고 상태기계(T2) · 명령(T3)을 그 위에 올린다.

**검증**

- Linux clang 19 Debug -Werror: ctest 62/62. unit_content · unit_project 통과.
- 와이어 · 세이브 · 시뮬레이션 형식 변경 없음 (modes/ 가 없는 팩의 contentHash 는 그대로 — 골든 해시 그대로).
- 기존 v1 매니페스트 (tests/data/projects/minimal 등)는 그대로 읽힌다. 새로 쓰는 매니페스트는 schemaVersion 2.

**남은 일**

- M2-T2 game.session 컴포넌트 · GameSystem (Starting → Running → Finished, 끝 조건). game.session 이 없는 월드는 결과가
  같아야 한다.
- M2-T3 시작 · 재시작 명령 · 역할 · UI. GameMode 를 월드 오버레이로 고치기는 M4.

---

## 2026-10-20 — v0.2 M1-T5: 프로젝트의 세이브 폴더 = worlds/, Ctrl+S = 시작 월드 → M1 완료 (ADR-0044)

**무엇을**

- `project::projectSavesRoot(폴더)` = <프로젝트>/worlds, `project::entryWorldSaveName(매니페스트)` = entryWorld 가
  "worlds/<이름>" 이면 그 이름.
- SandboxServer: --world 가 프로젝트이고 --saves 가 없으면 세이브 · 리플레이 폴더 = <프로젝트>/worlds (로그 "세이브 폴더: …").
- SandboxClient: `worldSaveDefaults(ClientOptions)` — --saves 가 이기고, 프로젝트면 <프로젝트>/worlds 와 시작 월드 이름
  (세이브 이름 규칙에 맞을 때), 아니면 saves · quicksave. `AppConfig.saveName` → "월드" 창 · Ctrl+S 의 처음 이름.
- 시험: unit_client 2 케이스 (기본값 표, 프로젝트 싱글플레이에서 30 프레임 → Ctrl+S → worlds/main 이 바뀌고 프로젝트 검사
  오류 · 경고 0 · 시작 틱이 늘었다), unit_project (이름 규칙 표), CTest server_project_saves_root.
- 문서: ADR-0044, 18 (M1 ✅ — 다음 M2-T1), 10 9장, 15 7장 (서버 · 클라이언트 --saves), 13, adr/README.

**왜**

- 에디터에서 고친 맵이 작업 폴더의 saves/quicksave 로 가서 게임에 들어가지 않았다. 월드 = 세이브 폴더(ADR-0040)이므로 저장
  위치와 처음 이름만 맞추면 "고치고 저장하면 게임이 바뀐다". 새 메시지 · 명령은 없다.

**검증**

- Linux clang 19 Debug -Werror: ctest 62/62 (+1). unit_client · unit_project 통과.
- 와이어 · 세이브 · 시뮬레이션 형식 변경 없음.

**남은 일**

- M2 GameMode · 라이프사이클 (서버 권위 상태기계) — 먼저 D-11 (플레이어 행동 모델)을 정한다.
- M5-T1 Test Play: 실행 상태가 Ctrl+S 로 시작 월드가 되는 것 — 편집 원본과 분리.

---

## 2026-10-20 — v0.2 M1-T4: 새 프로젝트 만들기 — createProject · 도구 sbx (ADR-0043)

**무엇을**

- `project::createProject(폴더, {gameId, name?, fromScenario?, contentRoot, seed}, 엔진, 카탈로그)`:
  - 템플릿 = 빈 월드(내장 콘텐츠 · 16 × 16 청크 · 일시정지) 또는 시나리오(requiredPacks + requires 를 content/ 로 복사, 한 틱
    돌린 월드 = worlds/main). 매니페스트 version 0.1.0, entryWorld worlds/main, 엔진 범위 `defaultEngineRange`
    (1.0 전 ">=<엔진> <1.0.0", 이후 "^<major>").
  - 폴더는 없거나 비어 있어야 한다 (AlreadyExists). "<폴더>.sbx-new" 에 만들고 validateProject 오류 · 경고 0 이면 rename,
    실패하면 임시 폴더를 지운다. 끝의 '/' 는 정규화.
- 도구 `sbx` (tools/sbx, Core 만 — 경계 규칙 등록): `new <폴더> --id … [--name] [--from] [--content-root] [--seed]`,
  `check <폴더>`, `templates`, `--version`. 종료 코드 0 · 1 검사 오류 · 2 인자/실패.
- T1 의 `sbx_sim_check --validate-project` 를 지우고 `sbx check` 로 옮겼다 (sim_check 소스는 T1 전과 같아졌다).
  CTest project_minimal_validate · project_bad_manifest_* 는 이름 그대로 명령만 sbx check.
- 시험: unit_tools test_sbx_options (명령 · 인자 오류), unit_project 3 케이스 (빈 월드 · 시나리오 템플릿 — 서버 경로로 연다 ·
  덮어쓰기 거절 · 실패 시 아무것도 안 남김 · 알 수 없는 시나리오 · 팩 없는 루트), CTest project_new_empty ·
  project_new_from_scenario (tests/project/project_new.cmake — new → check → SandboxServer 30 틱 → 다시 new 거절).
- 문서: ADR-0043, 18 (T4 ✅, T5 = 에디터 저장 위치, 5.2 검사 명령, 7 · 8장), 15 (sbx · 타깃 · 옵션 표), 17, 13, adr/README.

**왜**

- v0.2 "프로젝트 생성 · 빌드 · 실행"의 생성: 손으로 매니페스트를 쓰고 세이브를 복사하지 않게. 시나리오가 이미 팩 · 경계 ·
  초기 배치를 가지므로 템플릿 형식을 따로 만들지 않았다. 만든 직후 같은 검사로 확인 — 서버가 열지 않는 프로젝트를 만들지 않는다.
- 결정론 하네스(sbx_sim_check)와 사용자용 프로젝트 도구를 나눴다 (v0.4 패키지 관리자가 sbx 에 붙는다).

**검증**

- Linux clang 19 Debug -Werror: ctest 61/61 (+2). unit_project · unit_tools 통과.
- 실행: `sbx new demo/Arena --id studio.arena --name 아레나 --from ecosystem_small` → 팩 eco, worlds/main tick 1 · 엔티티 300;
  `sbx check demo/Arena` → 오류 0 · 경고 0; 같은 폴더에 다시 new → AlreadyExists, 종료 코드 2.
- 세이브 · 와이어 · 시뮬레이션 형식 변경 없음.

**남은 일**

- M1-T5: 프로젝트 서버의 세이브 폴더 = <프로젝트>/worlds, Ctrl+S 기본 이름 = 시작 월드 (편집한 맵이 게임이 된다).

---

## 2026-10-19 — v0.2 M1-T3: 핸드셰이크가 게임 정체성을 싣는다 — 프로토콜 8 (ADR-0042)

**무엇을**

- `net::GameIdentity{gameId, name, version}` 을 Welcome 끝 · Reject 끝에 (kProtocolVersion 7 → 8, 상한 64 · 256 · 128 바이트,
  gameId 없이 name · version 만 있으면 형식 오류). `gameIdentityOf(매니페스트)`. ServerHostDesc.game — 서버가 상한으로 자르고
  모든 Welcome · Reject 에 싣는다. SandboxServer · LocalServerHost 가 WorldSource.game 에서 채운다.
- 클라이언트: WorldInfo.game → "월드" 패널 "게임 <이름> — <gameId> <버전>", --headless 끝 "game …" 줄, sbx_net_probe "게임 …" 줄.
  ContentMismatch 로 팩을 못 읽거나 해시가 다르면 실패 문구에 "--content <프로젝트 폴더>/content" 안내.
- 매니페스트 content.packs ≤ 16 (P2) — Reject 가 실을 수 있는 팩 수와 같게 (static_assert).
- 시험: test_messages (왕복 · 형식 오류 · 상한 · 프로토콜 8 고정), test_server_host (Welcome · Reject 의 게임, 이름 자르기),
  test_network_session (로컬 프로젝트 표시 · 시나리오는 빈 값 · 원격 팩 실패 안내), test_game_project (팩 17 개 P2),
  test_world_ops 의 "== 7" 고정을 ">= 7" 로 (지금 판 고정은 test_messages). CTest net_server_probe_project (실제 UDP —
  net_smoke.cmake 에 WORLD · WORLD_NAME · GAME, 게임이 아닌 서버는 게임 줄이 없어야 한다).
- 문서: ADR-0042, 08 (머리말 · 4장 게임 · 5장 표 · 프로토콜 8 이력), 18 (T3 ✅ · 다음 T4 · 5.2 팩 상한 · 8장), 15 7장, 13,
  adr/README.

**왜**

- 접속한 클라이언트가 어떤 게임인지 몰랐다 (월드 이름은 64 바이트로 잘림). 프로젝트 팩은 저장소 content/ 에 없어 원격 접속이
  "팩을 찾을 수 없다"로만 끝났다 — 무엇을 어디서 읽어야 하는지 알려 준다. 자동 탐색 · 다운로드는 무결성 문제로 뒤로 (v0.5 · v0.7).

**검증**

- Linux clang 19 Debug -Werror: ctest 59/59 (+1). unit_network · unit_client · unit_project 통과.
- 실행: `SandboxClient --headless --world tests/data/projects/minimal` → "game Minimal — sandbox.minimal 0.1.0".
  probe (실제 UDP) → 게임 sandbox.minimal 0.1.0 "Minimal".
- 프로토콜 7 클라이언트는 VersionMismatch 로 거절된다 (정확 일치 규칙 그대로). 세이브 · 시뮬레이션 형식 변경 없음.

**남은 일**

- M1-T4: 새 프로젝트 만들기 (템플릿 → 매니페스트 + 빈 월드 + 팩 뼈대), 에디터 저장을 프로젝트 worlds/ 로.

---

## 2026-10-19 — v0.2 M1-T2: 서버 · 클라이언트가 게임 프로젝트를 연다 (ADR-0041)

**무엇을**

- `project::openProject` — validateProject 와 같은 검사(공용 함수 checkProject 로 나눔)를 하고, 오류가 없으면 콘텐츠와 시작
  월드를 넘긴다 (검사 때 연 월드를 그대로 — 두 번 열지 않는다). 오류면 ValidationFailed (처음 5 개 문제). `isProjectFolder`.
- `scenario::openWorldSource`: 시나리오 → 게임 프로젝트(sandbox.project.json) → 세이브 폴더 순서. 프로젝트는 이 엔진 버전
  (BuildInfo)으로 검사, 팩 = <프로젝트>/content (--content-root 를 쓰지 않음), 월드 이름 = 매니페스트 name,
  `WorldSource.game` = 매니페스트. 그래서 `SandboxServer --world <프로젝트>` 와 `SandboxClient --world <프로젝트>`
  (LocalServerHost) 가 같은 길로 연다. 서버는 게임 정보를 로그에, 도움말 · NotFound 문구에 프로젝트.
- 시험: unit_project 5 케이스 더 (열기 · 직접 로드와 같은 해시 · 엔진 범위 · gameId 오류 문구 · 경고 · WorldSource 세 갈래),
  CTest server_project_exit · server_project_invalid · client_local_project_headless. tests/data/projects/minimal 의 월드는
  일시정지를 풀었다 (서버가 60 틱까지 돈다).
- 문서: ADR-0041, 18 (0 · 2 · 3 · 5.1 — T2 ✅, T3 = 핸드셰이크 게임 정체성, T4 = 새 프로젝트 · 저장 · 7 · 8장), 15 7장,
  08 (WorldSource), 17, 13, adr/README.

**왜**

- M1-T1 의 프로젝트를 실제로 돌릴 길. 서버와 싱글플레이가 한 함수로 월드를 여는 구조(ADR-0026)라 거기에 더했다.
- 검사 오류가 있으면 시작하지 않는다 — 접속자가 깨진 콘텐츠 · 월드를 받지 않게. 경고(다른 콘텐츠로 저장된 월드 등)는 열린다.

**검증**

- Linux clang 19 Debug -Werror: ctest 58/58 (+3). unit_project 20 케이스 · 225 단언.
- 실행: `SandboxServer --world tests/data/projects/minimal --port 0 --ticks 60 --exit` → "게임 프로젝트 sandbox.minimal 0.1.0
  Minimal", "listening udp … world Minimal", "tick 60 hash …". bad_manifest → P2 · P3 · P4 세 줄과 종료 코드 2.
  `SandboxClient --headless --world tests/data/projects/minimal --frames 60` → 스냅숏 받음, 다시 맞춤 0.
- 와이어 · 세이브 · 시뮬레이션 형식 변경 없음 (프로토콜 7 · kSimVersion 그대로, det_* 통과).

**남은 일**

- M1-T3: Welcome · Reject 에 게임 정체성 (프로토콜 8) — 원격 클라이언트는 지금 `--content <프로젝트>/content` 로 접속한다.
- M1-T4: 새 프로젝트 만들기, 에디터 저장을 프로젝트 worlds/ 로.

---

## 2026-10-19 — v0.2 M1-T1: GameProject 매니페스트 · SemVer · 프로젝트 검사 (ADR-0040)

**무엇을**

- `foundation/version/SemVer`: SemVer 2.0.0 엄격 파서 (세 숫자 · 앞자리 0 금지 · 프리릴리스 · 빌드 · u32), 11장 우선순위,
  `VersionRange` (>= > <= < = ^, 부분 버전은 npm 과 같은 뜻, 공백 = AND). `||` · `~` · `*` · 하이픈은 Unsupported (D-05).
- `core/project/GameProject`: `sandbox.project.json` schemaVersion 1 (gameId · name · version · engine · description? ·
  content.packs? · entryWorld) 읽기 · 정규형 쓰기 · 원자적 저장, 경로 검사 (상대 · '/' · '..' · Windows 예약 이름),
  `validateProject` — 매니페스트 → 엔진 범위 → `<프로젝트>/content` 팩 (ContentLoader V1~V7) → 시작 월드 (요약 · 팩 ·
  contentHash · persist::loadWorld 로 실제로 열기). 문제는 ContentIssue (P1~P5) 로 모두 모은다.
- `sbx_sim_check --validate-project <폴더>` (다른 실행 · 검사 옵션과 함께 쓰면 거절).
- 시험: unit_foundation (test_semver), 새 스위트 unit_project (test_game_project 15 케이스 — eco 팩을 복사하고
  ecosystem_small 3 틱을 저장한 프로젝트를 실제로 연다), unit_tools (옵션), CTest project_minimal_validate ·
  project_bad_manifest_rejected · project_bad_manifest_message, 데이터 tests/data/projects/{minimal,bad_manifest}.
- 문서: ADR-0040, 18 (현황 표 · 5.1 T1 ✅ · 5.2 규범 · 8장), 17, 13, 11 1장, 09 3장, adr/README.

**왜**

- v0.2 의 첫 조각: 게임 하나를 담는 단위가 없었다 (서버는 시나리오 이름 · 세이브 폴더 + 저장소 content/ 만 안다).
  새 맵 형식을 만들지 않고 세이브 폴더를 월드로, 기존 팩을 프로젝트 안에 — 저장 · 마이그레이션 · 해시 · 에디터 경로 재사용.
- 검사가 월드를 실제로 여는 이유: "형식은 맞는데 안 열린다"를 서버 시작 전에 잡는다.

**검증**

- Linux clang 19 Debug -Werror: ctest -LE "balance|bench" 55/55 (기준 51 + 4). unit_project 15 케이스 · 167 단언.
- 실행: `sbx_sim_check --validate-project tests/data/projects/minimal` → 오류 0 · 경고 0, 시작 월드 열림 (tick 3, 엔티티 3);
  bad_manifest → P2 · P3 · P4 세 줄, 종료 코드 1.
- 시뮬레이션 · 세이브 · 와이어 형식은 바뀌지 않았다 (kSimVersion · 골든 그대로, det_* 통과).

**남은 일**

- M1-T2: WorldSource 가 프로젝트를 연다 → SandboxServer · SandboxClient `--world <프로젝트>`, 핸드셰이크에 게임 정체성.
- M1-T3: 새 프로젝트 만들기 · 에디터 저장을 프로젝트 worlds/ 로.

---

## 2026-10-19 — 플랫폼 범위 · 버전 줄 (ADR-0039), 18-PLATFORM 통합 계획, 설계 초안 원본 (docs/platform/)

**무엇을**

- 사용자의 플랫폼 설계 초안(2026-10-13, 28 문서)을 `docs/platform/` 에 원본 그대로 (동결 · 비규범).
- ADR-0039: 제품 = Sandbox Platform (Engine + Game Creator + SandboxScript + Package Manager + Hub + Cloud + Marketplace).
  Phase 0~15 = v0.1 Engine Foundation, 이후 작업 단위는 "v0.N Mk-Tn". 스크립팅은 v0.3 TypeScript 부분집합 → 자체 VM (Lua · WASM
  후보에서 뺌), 공개 인터넷은 v0.7 전 보안 전송 ADR, 결제 · 호스팅은 v0.8. 바뀌지 않는 것(서버 권위 · 30 TPS · 결정론 · 경계 ·
  3 백엔드)을 명시.
- 18-PLATFORM (새 계획 문서): 버전 줄 · v0.1 의 열린 항목 · 초안 대비 현황 표(Implemented / Partial / Planned — 소스 · 시험
  근거) · 모듈 계획 · 버전 규칙 · v0.2 마일스톤 M1~M6 과 태스크 · 열린 결정 D-01~D-11 · 문서/ADR 계획 · 기준선.
- 00 (1장 플랫폼 문단, 7장 스크립팅 비목표), 03 6.4 (언어 후보), 16 (머리말 · 2장 버전 줄 · 6장 범위 밖 · R14), docs/README
  (지도 · 읽는 순서), adr/README, README.

**왜**

- 새 방향이 기존 규범과 6 군데 부딪혔다 (00 비목표 · 16 범위 밖 · 03 6.4 언어 후보 · ADR-0011 명시 등록 · R6 평문 ENet ·
  Phase/버전 번호). 한쪽을 조용히 무시하지 않고 ADR 로 처리 방법과 시점을 정했다.
- 초안의 "현행 구현"은 2026-10-13 기준이라 그 뒤의 13A~15 가 빠져 있다 — 현황 표는 2026-10-19 소스 · 시험으로 다시 썼다.

**검증**

- 문서만 바꿨다. 기준선: master f4a72c1 (= GitHub master, git ls-remote) Linux clang 19 Debug -Werror ctest 51/51 통과.
- 표 정렬 (열 폭 유지), 링크 대상 파일 존재 확인.

**남은 일**

- v0.2 M1-T1 (다음 커밋): GameProject 매니페스트 · SemVer · 프로젝트 검사.
- v0.1 열린 항목: 사용자 PC 실기 QA · 50k 판정, CI 결과, macOS.

---

## 2026-10-19 — Phase 15A · 15B: 50k 시나리오, 결과가 같은 최적화 — 태그 종류 색인 · 행동 병렬 · Prefab 템플릿 · 스냅숏 공유

**무엇을**

- 측정: `ecosystem_50k` (448 × 448, 50,000 개체, 1,000 틱 — 10k 와 같은 밀도). sbx_bench sim.ecosystem 이 10k · 50k 를 함께.
- 15A 시뮬레이션:
  - 공간 색인: 위치 SoA, u32 순열 정렬, 태그 집합 → 종류 번호(intern), 종류별 고운 색인 (4 × 4 타일, 행마다 원의 현 안 칸만 —
    순서 S1 아님, 순서와 무관한 질의만). 셀 계산 인라인 (floor 호출 없이).
  - 감지: 그래프 × 태그 종류 일치 표, 일치하는 종류의 색인만 (늑대가 풀을 건너뛴다).
  - 충돌: 후보의 콜라이더 · 위치를 항목 표(엔티티 index → 항목)에서, 후보 반경의 상대 반지름 = 이번 틱 최대 반지름(전에는
    상한 2), 지형 밀어내기 계산을 Worker 와. `Registry::slotCount`.
  - 행동: 평가를 Worker 와 나눈다 — 자기 Behavior · Movement · Path 만 쓰고 남의 Transform 만 읽는다 (04 7장에 Worker 쓰기
    조건 (a)~(e)).
  - 번식: 이웃의 Prefab 비교를 엔티티 index → Prefab 번호 표로. `WorldGrid::moveCostAt` 인라인 (청크 찾기 · floorDiv 없이).
  - Prefab 템플릿: 덮어쓰기 없는 생성은 첫 JSON 생성의 컴포넌트 값을 BinaryCodec 바이트로 기억해 다음부터 읽는다
    (contentRevision 이 바뀌면 버린다).
- 15B 스냅숏: (엔티티, 컴포넌트) 인코딩을 build 마다 한 번 · 클라이언트끼리 공유, 관심 엔티티는 청크별 목록에서, netId 기수
  정렬, 복제 mask 를 풀마다 훑어 index 표로.
- 문서: ADR-0038, 14(상태 · 6장 판정 · 7.12), 04(7장 Worker 쓰기 조건), 03(행동 병렬), 05(4.4 색인), 08(6.3 15B),
  13(시나리오 표), 16(Phase 15 — 15A · 15B ◐, 판정 대기), README.

**왜**

- Phase 15 완료 기준(50k 평균 < 10 ms · p99 < 25 ms)을 잴 시나리오가 없었다. 2코어 측정: 50k Worker 1 평균 46.6 ms — 감지 20 ·
  충돌 9.3 · 행동 6.6 ms. 비용은 이웃 수를 따른다 (14 7.5): 감지 후보의 84 % 가 풀인데 늑대는 풀을 보지 않는다.
- 결과가 비트 단위로 같은 최적화만 했다 — 감지 주기 같은 동작 변경은 밸런스와 함께 따로 (ADR-0038).

**검증**

- 해시: 바꾸기 전 sbx_sim_check 로 ecosystem_small(900) · eco_lifecycle(600) · ecosystem_10k(600) · ecosystem_50k(300) 를 50 틱마다
  기록해 두고 단계마다 비교 — 모두 같음, --threads 0,1,4 같음 (D5). TSan (ecosystem_10k 60 틱 · Worker 3) 경고 0.
  스냅숏: 바이트 · 관심 개체 수가 6 경우 모두 같음. ctest 51/51 (골든 · D2 · D3 · net 수렴 포함).
- 성능 (2코어 클라우드, master + 50k 시나리오만 넣은 빌드와 번갈아 두 번씩 — 14 7.12): 50k Worker 0 63.7 → 38.5 ms,
  Worker 1 46.6 → 29.6 ms (p99 81 → 53), 10k Worker 0 9.4 → 5.5 ms. 50k 첫 틱(배치) 650 → 455 ms. 스냅숏 클라 16 · 월드 전체
  219 → 127 ms, 클라 4 · 월드 전체 64 → 34 ms, 클라 16 · 화면 25 → 20 ms.
- 시도했지만 이득이 없던 것: 색인 SoA 만으로는 감지가 그대로였다 (범위 안 후보의 갱신이 비용) — 종류별 색인에서 줄었다.
  색인 재구성은 SoA · 종류 작업으로 0.2 ms 쯤 늘었다 (질의는 25 % 빨라짐).
- Linux clang (-Werror) · gcc Debug ctest 51/51, MinGW 경고 0 (커밋마다).

**남은 일**

- 사용자 PC 측정: `sbx_bench --only sim.ecosystem --threads <코어 수 − 1>` → 50k 평균 < 10 ms · p99 < 25 ms 면 Phase 15 ✅.
  넘으면 남은 직렬 몫(번식 · 재구성 · 이동 · 충돌 모으기)을 15C 로.
- [계획] 스냅숏 Net 스레드 · 메시지 아레나 (클라 16 < 3 ms), 색인 증분 갱신, 시스템 간 병렬, 감지 원 안 칸 합산.

---

## 2026-10-19 — Phase 14C: Metal 백엔드 — 공유 이벤트 타임라인, 그릴 때 묶는 번호 표, CAMetalLayer 스왑체인 (구현 완료 · macOS 검증 대기)

**무엇을**

- render/metal (Objective-C++ · ARC): `MetalDevice.hpp` (내부 헤더 — ObjC++ 밖에서 include 하면 #error), `MetalDevice.mm`
  (Metal 3 장치 · MTLCommandQueue · MTLSharedEvent 타임라인 · 명령 버퍼 오류 수집 · Private/Shared 버퍼 · Private 2D/2DArray
  텍스처와 뷰 · 업로드 링 · 지연 해제 · 프레임), `MetalPipeline.mm` (MSL newLibraryWithSource · 샘플러 · 레이아웃 서명 ·
  바인드 그룹 리소스 목록 · 정점 서술자 · 블렌드 · 리플렉션 번호 표), `MetalCommandList.mm` (렌더 · 블리트 인코더, 기억한
  상태를 draw 때 묶기, push = buffer 0 setBytes, 시저 자르기, 디버그 그룹), `MetalSwapChain.mm` (CAMetalLayer · nextDrawable ·
  별도 명령 버퍼 presentDrawable · @autoreleasepool).
- RHI: `Backends.hpp` 에 metal::createDevice, 팩토리 Auto = D3D12 → Vulkan → Metal, `backendCompiled(Metal)`.
  CMake: `SBX_ENABLE_METAL` (macOS 기본 ON), OBJCXX · -fobjc-arc · Metal · QuartzCore · Foundation, `SBX_HAS_METAL`.
  sbx_render_tests 는 Metal 빌드에도 (루트 CMakeLists 조건).
- 시험: CTest `render_tests_metal` (`--rhi metal --debug --skip-unsupported` — 새 옵션: 디바이스가 Unsupported 면 77 →
  건너뜀). 팩토리 시험 · `--rhi` 시험이 Metal 이 든 빌드를 다룬다. client: `--rhi metal`, `--version` 의 render 목록,
  client_version 정규식에 Metal. Vulkan 의 Cocoa 표면 메시지는 "--rhi metal" 을 가리킨다.
- build: macOS 배포 목표 13.0 → 13.4 (따로 커밋). Apple libc++ 의 std::format 은 std::to_chars(double) 을 쓰는데 그 시스템
  라이브러리가 13.3/13.4 부터라 13.0 이면 모든 std::format TU 가 "unavailable" 로 컴파일 오류 — 14B 를 macOS 에서 빌드해 보지
  않아 놓쳤다 (리뷰 에이전트가 Apple 포럼 사례로 찾음).
- 문서: ADR-0037, 06(상태 · 2 · 3.4 · 3.5 · 5.3 · 6.1 · 13 · 14장), 13, 15(1 · 3 · 4 · 6 · 9장), 16(Phase 14 = 구현 완료 ·
  macOS 검증 대기 — ◐ 의 뜻을 적었다), 17, MANUAL-QA 14C (14B 의 "render none" 을 고쳤다), README.

**왜**

- Phase 14 의 마지막 단계. 사용자: "완료는 Test 후이고 Mac 이 없으니 완료는 대기 상태로" — 코드 · 빌드 · 시험 · 문서를 끝내고
  상태를 "구현 완료 · macOS 검증 대기"로 둔다 (✅ 는 macOS 결과가 온 뒤).
- 펜스를 MTLSharedEvent 로 하면 프레임 슬롯 · 업로드 링 · 지연 해제가 다른 백엔드와 같은 코드다. Argument Buffer 대신 그릴 때
  setBuffer — 지금 규모에서 단순하고, MSL 쪽(ADR-0035 의 고정 번호)을 바꾸지 않는다.

**검증**

- macOS 가 없어 **빌드 · 실행하지 못했다**. render/metal 의 .mm 넷: GNUstep 헤더 + 검사용 Apple 선언(14B 의 머리 파일에 Metal
  프로토콜 · 서술자 · 열거를 더함, -fblocks)으로 `clang++-19 -fsyntax-only -fobjc-arc -Wall -Wextra -Wpedantic -Wshadow` —
  우리 코드의 오류 · 경고 0. Metal API 의 제약은 Apple 문서로 확인한 것만 검사에 넣었다 (copyBuffer 4 의 배수 · 인덱스 오프셋 =
  인덱스 크기의 배수).
- 독립 리뷰(서브 에이전트가 Apple 문서 JSON 으로 셀렉터 · 열거 · 가용 버전을 대조): 이름 · 시그니처 오류 없음. 고친 것 —
  패스 밖에서 연 디버그 그룹을 패스 안에서 닫으면 짝이 어긋남, 빠진 색 첨부가 번호를 당김 · 파이프라인 형식과 패스 첨부가
  다른 draw, 묶지 않은 정점 버퍼, 2D 복사의 bytesPerImage(0 이어야), 깊이 텍스처 복사, 빈 제출의 명령 버퍼가 nil 이면 펜스만
  오름, 블리트 인코더 안의 쓰기-쓰기 · 읽기-쓰기, 정수 형식 블렌딩, 버퍼 밖 오프셋의 크기 계산, METAL_DEVICE_WRAPPER_TYPE.
- Linux clang 19 (-Werror, MSL 켬) · gcc 13 Debug: ctest 51/51. MinGW 경고 0 (커밋마다).

**남은 일**

- CI 의 macOS 잡 첫 실행 (빌드 · platform_cocoa · render_tests_metal — 실행기의 GPU 가 Metal 3 가 아니면 건너뛴다, dxc 가 없으면
  그리기 케이스 없음) → 로그를 받아 고친다. Mac 이 있으면 MANUAL-QA 14B · 14C. 통과하면 Phase 14 ✅.
- [계획] Metal 타임스탬프 · 깊이 · metallib · Argument Buffer · --rhi-capture, Slang 재평가 스파이크(14.0).

---

## 2026-10-19 — Phase 14B: macOS Cocoa 창 — AppKit (Objective-C++ · ARC), 가상 키 코드 표, NSTextInputClient (미검증)

**무엇을**

- platform/macos: `CocoaWindow.mm` — NSApplication(Regular · 앱 메뉴 ⌘Q = 닫기 요청), NSWindow + flipped 내용 뷰
  (layer = CAMetalLayer → NativeWindowHandle{Cocoa}), 위임으로 닫기 요청 · 크기 · 배율 · 최소화 · 포커스, 키(keyCode →
  `MacKeyMap`, 반복 = isARepeat, flagsChanged 의 장치별 비트로 왼 · 오른 수정자, Caps Lock 누름 + 뗌, ⌘ keyUp 직접 전달),
  글자(켜져 있을 때만 interpretKeyEvents → insertText, 조합 중 글자는 버림), 마우스(other 버튼 2 · 3 · 4, clickCount, 휠 =
  scrollingDelta, 정밀 ÷ 10), 캡처(CGAssociate + 델타), NSCursor, NSPasteboard, poll/wait = nextEventMatchingMask.
  `MacKeyMap` (kVK 표 — AppKit 없음, 모든 OS 컴파일), `MacPlatform.cpp` ("Cocoa"), `MacWindows.hpp`.
- CMake: APPLE 이면 OBJCXX · CocoaWindow.mm 만 -fobjc-arc · AppKit · QuartzCore · CoreGraphics, SBX_HAS_COCOA. 배포 목표 13.0.
  stub 창은 이제 "X11 · Wayland 를 모두 끈 Linux" 뿐.
- client: macOS 한글 UI 글꼴 (Apple SD 산돌고딕 Neo → 나눔고딕 → AppleGothic). client_version 정규식에 Cocoa.
- 시험: unit_platform Mac 키 표 (모든 OS), 스위트 `cocoa` (test_cocoa_window.mm, macOS 만) · CTest `platform_cocoa`.
- 문서: ADR-0036, 07(상태 · 1 · 2 · 6.3 · 9장), 13, 15(1 · 4 · 9장), 16(14B ◐ · 14C), 17, MANUAL-QA 14B, README.

**왜**

- Phase 14 의 두 번째 단계. Metal 백엔드(14C)가 받을 CAMetalLayer 를 창이 만든다.
- 가상 키 코드는 물리 위치 — 세 OS 가 같은 Key 를 낸다. 엔진이 이벤트 루프를 쥐어 다른 OS 와 같은 poll/wait 계약.

**검증**

- macOS 가 없어 **빌드 · 실행하지 못했다**. 대신: GNUstep 헤더(libgnustep-gui-dev) + 없는 Apple API 를 적은 검사용 머리 파일로
  `clang++-19 -fsyntax-only -fobjc-arc -fobjc-runtime=gnustep-2.0` — CocoaWindow.mm · test_cocoa_window.mm 오류 · 경고 0
  (문법 · C++ 형 · ARC 만 — Apple API 이름 · 시그니처는 확인 못 함).
- Linux clang 19 · gcc 13 Debug: ctest 51/51 (Mac 키 표 시험 151 단언). MinGW 경고 0 (커밋마다).
- 사고 하나: .clang-format 이 Objective-C 를 지원하지 않아 clang-format 이 .mm 에 빈 출력을 냈고 그것으로 파일을 덮어썼다 —
  대화 기록에서 되살렸다. 이제 서식 도구는 실패하면 건너뛴다 (클라우드 스크립트), 15-BUILD 9장에 적었다.

**남은 일**

- 첫 확인: push 후 CI 의 macOS 잡 (빌드 · platform_cocoa), Mac 이 있으면 MANUAL-QA 14B. 오류 로그를 받아 고친다.
- [계획] 조합 중 글자 UI, GCMouse 가속 없는 캡처, 대각선 크기 커서, 전체 화면.
- 다음: 14C — Metal 백엔드 (내장 MSL 실행 중 컴파일 · 번호 표 · MTLSharedEvent 타임라인).

---

## 2026-10-14 — Phase 14A: 셰이더 MSL 경로 — 벤더링한 SPIRV-Cross 호스트 도구, Metal 번호 규칙, MSL 소스 내장

**무엇을**

- external: SPIRV-Cross 1.4.304.1 (Debian orig 묶음, 서명된 .dsc 의 SHA256 7c9ffa18…, 수정 없음)의 코어 · GLSL · MSL 소스와
  라이선스만 → `sbx_spirv_cross` (경고 끔).
- tools/shader: `sbx_msl` (SPIR-V 한 단계 → .metal, MSL 3.0 · macOS · 인자 버퍼 없음). 번호는 정하지 않고 `--bind` 로 받는다
  — 셰이더가 쓰는 리소스가 번호를 받지 못했으면 실패.
- `sbx_shader_gen.py`: 프로그램의 바인딩을 (group, binding) 순서로 종류마다 Metal 번호 (버퍼 1.. · 텍스처 0.. · 샘플러 0..,
  push constant = buffer 0, 정점 버퍼 = 30 - slot, 한계 26 · 31 · 16) → `ReflectedBinding::metalIndex` · reflect.json, `--msl-tool`
  이면 sbx_msl 을 불러 `ShaderBytecode::msl` · `mslEntry` 로 내장. 자체 시험에 번호 · 한계.
- `render/rhi/ShaderTypes.hpp`: `kMetal*` 상수 · `metalVertexBufferIndex`, 필드 셋. 옵션 `SBX_SHADER_MSL` (macOS + 셰이더면 기본
  ON, 교차 빌드 불가), 정의 `SBX_HAS_MSL`. macOS 에서도 PATH 의 dxc 로 셰이더를 켠다.
- 시험: unit_render `test_shader_msl` (상수, 내장 셰이더 7 개의 번호, MSL 의 진입점 · 한정자 · "<이름> [[texture(n)]]" ·
  push constant buffer 0).
- 덤 (버그): `UploadRing` 이 비면 처음으로 돌아가던 것을 "끝에 자리가 없을 때만" 으로. GPU 가 프레임마다 다 끝내면(빈 링)
  위치가 늘 0 이라 render_tests_lavapipe 의 "링 감기" 검사가 부하(ctest -j8)에 따라 실패했다 (13B 부터 가끔 보였다).
  unit 시험 하나 추가.
- 문서: ADR-0035, 06(상태 · 5.3 · 6.1 · 6.3), 13, 15(3 · 4 · 5 · 9장), 16(Phase 14 를 14A · 14B 로), 17, external/README, README.

**왜**

- 개발 · 검증 환경에 macOS 가 없다 — Metal 을 돌릴 수 없으니 여기서 시험할 수 있는 셰이더 경로부터 (사용자 선택).
- 배포판 spirv-cross 는 판이 제각각(Ubuntu 24.04 = 2021 판)이고 Windows 에 없다 → 소스를 고정해 세 OS 가 같은 MSL 을 만든다.
- 번호를 생성기 한 곳에서 정하면 백엔드(리플렉션)와 셰이더(MSL)가 어긋날 수 없다.
- metallib 는 Xcode 전체가 필요하고 Linux 에서 시험할 수 없다 → MSL 소스를 내장해 Metal 이 실행 중에 컴파일 (14B).

**검증**

- Linux clang 19 Debug, `-D SBX_SHADER_MSL=ON` (셰이더 = Wine 의 dxc): sbx_msl 빌드 · 셰이더 7 개 × 2 단계 MSL 생성, ctest 51/51
  세 번 연속 (-j8), unit_render 31 케이스. 생성 MSL 을 눈으로: sprite VS `[[stage_in]]` + `push [[buffer(0)]]` + `[[vertex_id]]`,
  PS `atlas [[texture(0)]]` · `atlasSampler [[sampler(0)]]`, terrain `tiles [[texture(0)]]` · `palette [[texture(1)]]`.
  sbx_msl 에 샘플러 번호를 빼면 "Metal 번호를 받지 못한 리소스: atlasSampler(2:1)" 로 실패.
- MinGW (MSL 끔) 경고 0, gcc 13 Debug (MSL 켬) — 커밋마다.

**남은 일**

- 14B: Cocoa 창 · Metal 백엔드 (MSL 실행 중 컴파일 · 번호 표), 내장 MSL 을 Metal 로 컴파일하는 macOS 시험, 기준 이미지.
- [계획] metallib, 인자 버퍼, 14.0 Slang 스파이크 (14B 이후), CI 에서 MSL 생성 (Linux CI 에 dxc 고정이 먼저).

---

## 2026-10-13 — Phase 13C: Linux Wayland 창 — libdecor · xkbcommon · text-input-v3, 실행 중 창 시스템 선택, Linux CI

**무엇을**

- platform/linux: `WaylandWindow` (libwayland + libdecor 틀 — 서버 장식이 있으면 서버 것, 없으면 플러그인. configure 크기 ·
  최대화를 풀면 떠 있던 크기 · SUSPENDED = 최소화 · close → CloseRequested, 렌더러 전 wl_shm 어두운 버퍼), 키 = 같은 evdev 표,
  `WaylandInput` (xkbcommon 키맵 · 수정자 · 글자 · compose 데드 키 · repeat_info 로 클라이언트 자동 반복 · 버튼 코드 · 더블클릭),
  text-input-v3 입력기, pointer-constraints lock + relative-pointer 캡처, wl_data_device 클립보드, wl_cursor 테마, 정수 배율.
  확장 프로토콜 C 는 빌드 때 wayland-scanner 로 (`cmake/SbxWayland.cmake`). 옵션 `SBX_ENABLE_WAYLAND` (Linux 기본 ON).
- `WindowDesc.system` (Auto · X11 · Wayland) · `IWindow::backendName()` · `LinuxPlatform` 의 선택 (Auto = WAYLAND_DISPLAY →
  Wayland, 안 되면 X11 · 강제한 쪽은 대신하지 않음 · 한쪽만 든 빌드는 그쪽만).
- render/vulkan: Wayland 표면 (vkCreateWaylandSurfaceKHR), Linux 인스턴스 확장 = surface + 있는 것.
- client: `--platform auto|x11|wayland`, 로그 "창 <실제 백엔드>" (--headless 는 "Headless").
- 시험: unit_platform 에 xkb 키맵 이름으로 Wayland 입력(us · fr · us intl, 반복 시각, 수정자 키 반복 없음, 키맵 없음, 버튼,
  더블클릭), 강제 Wayland, 창 시스템 이름 · unit_client --platform. 스위트 `wayland` (실제 컴포지터 — 핸들 · 크기 · 제목 ·
  클립보드 · 커서 · 캡처 · 두 연결). CTest `platform_wayland` · `client_wayland_world_smoke` — `tools/wayland/run_headless.sh`
  (weston headless, 자기 XDG_RUNTIME_DIR), ASan 은 `tools/wayland/lsan.supp` (libdecor cairo 플러그인의 fontconfig · pango 캐시).
- CI: `.github/workflows/ci.yml` (문서에는 있었지만 커밋 기록에 없던 파일을 이번에 넣는다) — Linux 잡에 X11 · Wayland 개발 파일 · Xvfb · weston,
  push 트리거를 main → master (이 저장소의 브랜치).
- 문서: ADR-0034, 07(상태 · 1 · 2 · 6.2 · 9장), 06(상태 · 5.2), 13(CTest · CI 표), 15(1 · 4 · 7 · 8 · 9장), 16(13C ✅), 17,
  MANUAL-QA 13C, README.

**왜**

- 지금 데스크톱의 기본 세션은 Wayland 다. Xwayland 위의 X11 창은 배율이 흐리고 입력기가 XIM 을 거친다.
- GNOME 은 서버 장식이 없다 — libdecor 가 GNOME 에선 틀을 그리고 다른 서버에선 서버 틀을 쓴다 (SDL · GLFW 와 같은 선택).
- Wayland 키 코드 = evdev → 13B 의 표 그대로, 두 창 시스템이 같은 Key 를 낸다. 반복은 Wayland 규약상 클라이언트 몫이다.
- 한 바이너리가 X11 · Wayland 세션 모두에서 돌아야 한다 (배포판 패키지 하나).

**검증**

- Linux clang 19 Debug (-Werror) · gcc 13 Debug: ctest 51/51 (platform_wayland · client_wayland_world_smoke 추가).
  ASan · UBSan: platform_x11 · platform_wayland · unit_platform · client 창 스모크 둘 — 보고 없음 (억제 파일 적용 뒤).
  Wayland 만 · X11 만 · 둘 다 끈 구성: SandboxClient · SandboxTests 경고 0, --version "window Wayland" · "window X11",
  Wayland 만 든 빌드는 WAYLAND_DISPLAY 가 없어도 기본 소켓을 해 본다, X11 만 든 빌드의 --platform wayland = 오류.
- Xvfb 위 weston(x11 백엔드) + xdotool QA: libdecor cairo 틀(한글 제목 · 단추)에 월드 · 패널 (lavapipe FIFO 4장), 로그
  "Wayland 창 … · libdecor · 상대 이동 있음". 최대화 단추 → Resized 1300×804 → 복원 → 900×560, 모서리 끌기 → 820×496
  까지 매 단계 Resized, 닫기 단추 → CloseRequested → Shutdown. --no-render --log-input: 클릭 · F2 글자 a Z 9 ! · 키 반복
  (1 초에 약 25 번) · Ctrl+C → wl-paste "aZ9!xxx…" · wl-copy "붙여넣기" → Ctrl+V → Ctrl+C → wl-paste "붙여넣기" ·
  F3 캡처 중 MouseMove 위치 (500, 368) 그대로 Δ(15, -5) 셋 · 더블클릭 ×2 · 휠 (0, -1) · 가로 (-1, 0) · Ctrl+Q.
- 처음 잡은 것: 버퍼가 없는 표면은 지도에 오르지 않아 --no-render 창이 보이지도 입력을 받지도 않았다 (wl_shm 버퍼),
  libdecor 제목 줄 · 단추 클릭이 우리 wl_pointer 로도 와 게임 클릭으로 샜다 (표면 비교), 최대화를 풀 때 configure 에
  크기가 없어 최대화 크기에 남았다 (떠 있던 크기 기억), weston x11 백엔드에서 잠금이 늦게 켜져 캡처 중 절대 위치가
  움직였다 (캡처 중 위치 고정), 입력이 없는 창의 set_selection 은 weston 이 거절한다 (규약 — 시험을 그에 맞춤).
- MinGW (-D SBX_ENABLE_VULKAN=ON) 경고 0 (커밋마다).

**남은 일**

- 사용자 Linux PC: MANUAL-QA Phase 13C (GNOME · KDE · 한글 입력기 text-input-v3 · 배율 2 · 실제 GPU). CI 첫 실행 확인.
- [계획] 분수 배율 (wp-fractional-scale), 조합 중 글자 UI, wp-cursor-shape, 전체 화면, 닫은 뒤 클립보드, DnD, Linux CI dxc 고정.
- 다음: Phase 14 — macOS (Cocoa · Metal).

---

## 2026-10-13 — Phase 13B: Linux X11 창 — Xlib · evdev 키 표 · XIM · XInput2 Raw · 클립보드, Xlib 표면으로 Linux 클라이언트

**무엇을**

- platform/linux: `X11Window` (Xlib — WM_DELETE_WINDOW · ConfigureNotify 크기 · Unmap/_NET_WM_STATE_HIDDEN 최소화 · 포커스(키보드
  잡기 때문의 가짜 포커스는 뺀다) · Xft.dpi 배율 · _NET_WM_NAME 제목 · cursorfont 커서 · 빈 커서), 키 = keycode(evdev + 8) →
  `X11KeyMap` (순수 표, 모든 OS 에서 컴파일 · 시험), XKB detectable auto-repeat (없으면 같은 시각 Release + Press 쌍), XIM + XIC 텍스트
  입력 (Xutf8LookupString, 끄면 XUnsetICFocus + 조합 버림), 더블클릭 직접 판정, 휠 4 ~ 7 · X1/X2 8 · 9, 캡처 = XGrabPointer +
  XInput2 RawMotion (없으면 가운데로 되돌리기), CLIPBOARD UTF8_STRING (SelectionRequest 응답 · XConvertSelection 읽기),
  poll 기다림. `LinuxPlatform` (백엔드 이름 "X11", 잠자기). 옵션 `SBX_ENABLE_X11` (Linux 기본 ON — libx11-dev, libxi-dev 는 있으면).
- render/vulkan: Xlib 표면 — VulkanSwapChain.cpp 만 Xlib.h · vulkan_xlib.h 를 보고 vkCreateXlibSurfaceKHR 은 vkGetInstanceProcAddr 로,
  인스턴스 확장 VK_KHR_xlib_surface.
- client: Linux 한글 UI 글꼴 (Noto Sans CJK · 나눔고딕을 찾는다), --version "window X11".
- 시험: unit_platform X11 키 표 (모든 Key 왕복 · 한/영 · 한자), SandboxTests 스위트 `x11` (실제 X 서버 + XSendEvent 합성 이벤트 —
  닫기 · 키 · 반복 · 수정자 · 글자 켬/끔 · 버튼 · 더블클릭 · 휠 · X1 · 이동 · 크기 · 포커스 · 클립보드 · UTF-8 제목 · 커서 · 캡처),
  CTest `platform_x11` · `client_x11_world_smoke` (xvfb-run -noreset 이 있으면 가상 화면, 클라이언트는 lavapipe 로 월드를 그린다).
- 문서: ADR-0033, 07(상태 · 1 · 6.2 · 9장), 06(상태 · 5.2), 13, 15(1 · 3 · 4 · 7장), 16(13B ✅), 17, MANUAL-QA 13B, README.

**왜**

- Xlib 은 XIM · 글자 변환 · 클립보드가 있고 Vulkan 의 VK_KHR_xlib_surface 가 그대로 받는다 (XCB 만으로는 IME 가 없다).
- evdev keycode 는 물리 위치 그대로라 ADR-0017 의 Key 와 뜻이 같다 — Wayland(13C)도 같은 표를 쓴다.
- XInput2 Raw 가 Win32 Raw Input 과 같은 "가속 없는 상대 이동" 이다.
- 합성 이벤트는 같은 처리 함수를 지나므로 표 · 상태 기계를 실제 X 서버에서 자동 시험할 수 있다.

**검증**

- Linux clang 19 · gcc 13 Debug: ctest 49/49. ASan · UBSan 빌드: x11 · platform · client 스위트 56 케이스, 클라이언트 창(lavapipe)
  120 프레임 — 보고 없음.
- Xvfb + xdotool(XTEST — 실제 입력 경로): 월드 창(FIFO 3장, 제목 줄 "Vulkan (소프트웨어) 88 fps"), 한글 패널 글자, 휠 확대(줌 8.5 →
  22.4 px/칸) · 가운데 끌기 · D 키 이동 · 박스 선택("선택 eco.rabbit #275 …"), ImGui 글자 칸 입력 · xclip 붙여넣기("붙여넣기") ·
  Ctrl+C → xclip -o, G 격자 · F1, 창 크기 1200×760 · 500×300 · unmap/map · 1000×640 뒤 창을 채운다. --log-input: F3 캡처 중
  mousemove_relative 15,-5 → MouseMove Δ(15, -5) (XInput2 Raw), F2 글자 'a' 'Z' '9' '!' · Backspace 는 글자 아님, 더블클릭 ×2,
  휠 아래 · 왼쪽, Ctrl+Q.
- 처음 잡은 것: X.h 의 `CursorShape` 매크로가 우리 enum 을 가렸다 (#undef), Xvfb 가 마지막 클라이언트가 나갈 때 리셋해 다음 창이
  2 초 늦거나 연결이 거절됐다 (CTest 에 -noreset).
- MinGW (-D SBX_ENABLE_VULKAN=ON) 경고 0.

**남은 일**

- 사용자 Linux PC (실제 창 관리자 · 한글 입력기 · GPU): MANUAL-QA Phase 13B. Windows 는 다시 빌드 → ctest (바뀐 것은 X11 키 표뿐).
- [계획] XSETTINGS 로 실행 중 배율, 클립보드 INCR · 닫은 뒤 남기기, on-the-spot 조합 글자 그리기, Xcursor 테마, 전체 화면.
- 다음: 13C — Wayland (libdecor · xkbcommon · text-input-v3) + --platform + Linux CI.

---

## 2026-10-13 — Phase 13A: Vulkan 백엔드 — volk + 벤더링한 헤더, 타임라인 세마포어, lavapipe 로 D3D12 와 같은 기준 이미지

**무엇을**

- external: Vulkan-Headers 1.4.309 (C 헤더만) · volk 1.4.304 — Debian orig 묶음에서 (서명된 .dsc 의 SHA256), 수정 없음.
  `sbx_vulkan_headers` (VK_NO_PROTOTYPES, Windows 는 VK_USE_PLATFORM_WIN32_KHR) · `sbx_volk`. 옵션 `SBX_ENABLE_VULKAN` (Linux 기본 ON,
  Windows · macOS 기본 OFF — 계획의 SBX_ENABLE_VULKAN_ON_WINDOWS 를 대신한다).
- render/vulkan: VulkanDevice (인스턴스 1.3 · Validation · debug utils, 장치 고르기 — --rhi-warp = CPU 장치, 타임라인 큐, 리소스마다
  vkAllocateMemory, 업로드 링 · 지연 해제 · 타임스탬프 = D3D12 와 같은 프로토콜), VulkanPipeline (셰이더 · 샘플러 · 세트/파이프라인
  레이아웃 캐시 · 바인드 그룹 = 디스크립터 세트 · dynamic rendering 파이프라인), VulkanCommandList (프레임 슬롯마다 커맨드 풀,
  텍스처 전이 + 보수적인 전역 메모리 배리어, 음수 높이 뷰포트), VulkanSwapChain (Win32 표면, acquire · present binary 세마포어를
  큐가 맡는다, OUT_OF_DATE · VSync 바꿈 → 다시 만들기). DeviceCaps 정렬은 D3D12 값 이상.
- render/rhi/RenderDeviceFactory: createRenderDevice · defaultBackend · backendCompiled · parseBackend, SBX_HAS_D3D12 ·
  SBX_HAS_VULKAN. render/stub 을 지웠다.
- 셰이더: Linux 는 PATH 의 dxc (또는 SBX_DXC) 가 있으면 내장 셰이더를 켠다. sbx_shader_gen 이 내장 SPIR-V 에서 -fspv-reflect 의
  SPV_GOOGLE_* 확장 · 장식을 뺀다 (리플렉션은 원본으로 — self-test 추가).
- 시험: sbx_render_tests `--rhi auto|dx12|vulkan`, Vulkan 빌드면 Linux 에서도. CTest `render_tests_lavapipe`. SandboxTests: 백엔드
  팩토리 (이름 · 빌드에 든 것 · 없는 백엔드 = Unsupported), 클라이언트 --rhi. 고친 시험: 16 바이트 변수에서 256 바이트를 읽던 상수
  버퍼 업로드 (ASan 이 잡았다), 대소문자를 가리는 파일 시스템의 에셋 이름 (Green.png → green.png).
- client: `--rhi auto|dx12|vulkan`, --version 의 render 에 빌드에 든 다른 백엔드 ("D3D12 (+Vulkan)"), 상태 줄 · 디버그 패널의 검증
  경고 문구를 백엔드 이름으로, 소프트웨어 장치 표시 "(소프트웨어)".
- 문서: ADR-0032, 06(상태 · 2 · 3.2 · 5.2 · 12 · 13 · 14장), 07 6.2, 13, 15(1 · 3 · 4 · 5 · 7 · 9장), 16(Phase 13 = 13A · 13B · 13C,
  13A ✅), 17, external/README, MANUAL-QA 13A, README.

**왜**

- 창(13B)을 기다리면 백엔드 버그와 창 버그가 섞인다. 헤드리스 렌더 시험이 백엔드의 거의 전부를 덮고, 스왑체인은 이미 있는
  Win32 창으로 먼저 시험한다 (로드맵의 "Vulkan 을 Windows 에서 먼저").
- volk + 벤더링한 헤더: SDK · import 라이브러리 없이 재현 가능한 빌드, 로더가 없는 PC 에서도 D3D12 클라이언트가 뜬다.
- 타임라인 세마포어는 D3D12 펜스와 같은 모양이라 FrameContext · 업로드 링 · 지연 해제가 그대로다. binary 세마포어는 큐 안에
  숨겨 ISwapChain · ICommandQueue 를 바꾸지 않았다 (ADR-0032 결정 3).
- 정렬 · NDC Y · 기준 이미지를 D3D12 에 맞춰 "상위 코드 diff 0 줄" 을 기준 이미지로 증명한다 — 백엔드별 기준을 두지 않았다.

**검증**

- Linux clang 19 Debug (커밋마다 -Werror — 셰이더 없이 7A 케이스) · gcc 13 Debug: ctest 47/47. 셰이더(tools/wine/dxc.sh)를 켠
  빌드의 sbx_render_tests --rhi vulkan --warp: 커밋된 기준 이미지로 28 케이스 · 누수 0 · Validation 경고 0 · 오류 = 예상 8,
  --gbv (동기화 검증) 도 0. ASan · UBSan 빌드의 sbx_render_tests 0.
- MinGW Release (-D SBX_ENABLE_VULKAN=ON) 경고 0 (커밋마다) + Wine 11: sbx_render_tests --rhi vulkan (winevulkan → lavapipe) ·
  기본 dx12 (vkd3d) 모두 28 케이스, SandboxTests 348 케이스. 창 ecosystem_small --rhi vulkan: 스왑체인 FIFO 3장, 제목 줄
  "Vulkan (소프트웨어) 88 fps", 지형 · 스프라이트 · 패널, SetWindowPos 로 1200×800 · 0×0 · 1100×760 (다시 만들기 · 최소화) 뒤에도
  창을 채운다, Ctrl+Q.
- 처음 돌렸을 때 잡은 것: 스왑체인 확장을 표면 없는 인스턴스에서 켜던 것 (VUID-…-01387), SPIR-V 의 GOOGLE 확장 (08742), 복사 전용
  텍스처의 이미지 뷰 (04441), 디바이스 detach 순서 (시험 끝 SIGSEGV).

**남은 일**

- 사용자 PC (MSVC): `render/stub/StubRenderDevice.cpp` 지우기 → 다시 빌드 → ctest (render_tests_warp), `-DSBX_ENABLE_VULKAN=ON` 으로
  실제 GPU 의 --rhi vulkan (MANUAL-QA Phase 13A).
- [계획] VMA · 서브 할당, 디스크립터 프레임 링 · bindless, VkPipelineCache, 세밀한 버퍼 배리어, 전송 큐, 깊이 첨부.
- 다음: 13B — X11 창 · 입력 · Xlib 표면 → Linux 클라이언트. 13C — Wayland · Linux CI.

---

## 2026-10-12 — Phase 12D: 세이브 · 로드 · 리플레이 UI + 완료 기준 — 서버가 틱 사이에 하고, 월드를 바꾸면 generation (프로토콜 7). Phase 12 완료

**무엇을**

- core: `replay/ReplayPlayer` — 시작 틱 · 해시 확인 뒤 step() 마다 call 하나 (그 call 의 명령 + tick(), 해시 레코드 · 끝 해시
  비교 — 불일치는 처음 것을 기억하고 끝까지). playReplay 는 이것으로. 시나리오 `edit_session` (두 사람의 편집 명령 로그 600틱 —
  이동 · 값 · 컴포넌트 더하기/빼기 · 지형 · Rule · Prefab · Behavior 오버레이, 일시정지 편집 · 한 틱 · 속도, 일부러 거절 하나).
  `WorldSource`: openSave (이미 읽은 콘텐츠로 세이브 열기 — contentHash 가 다르면 거절) · readSaveSummary.
- network: 프로토콜 7 — WorldOp(65) · WorldOpResult(66) · WorldList(67) · WorldChanged(68, Bulk), Welcome · Snapshot 의
  generation, ServerStats 의 기록 · 재생 상태. ServerHost: Net 절반이 권한으로 거르고(목록 = 모두, 나머지 = admin 부터) Sim
  절반이 틱 사이에 저장 · 불러오기 · 기록 시작/끝 · 재생 (<savesRoot>/<이름>, 이름 = [A-Za-z0-9_-]{1,48}). 월드 바꾸기 =
  generation++ · ReplicationWriter::resetWorld(새 epoch) · WorldChanged · 오버레이 전체 · 그 배치의 명령 거절. 재생 중 편집은
  거절, Pause · Resume · Step · Speed 는 재생 제어, 끝나면 모두에게 결과 · 일시정지. 멈출 때 기록 중인 리플레이를 쓴다.
  ClientSession: requestWorldOp · 결과 · 목록 · generation (새 generation 스냅숏은 WorldChanged 까지 쥐고, 옛 것은 버리고,
  Welcome 보다 먼저 온 WorldChanged 로 시작). SandboxServer `--saves`, LocalServerDesc::savesRoot.
- editor: IEditorHost::worldGeneration — 바뀌면 되돌리기 · 다시 하기 · 기다리던 편집 · EditPreview 를 비운다.
- client: NetworkSession — worldOps · worldOp (접속하면 목록, 받아들여진 결과마다 다시), 재생 중 Space · . · = / - 는 재생 제어,
  상태 줄에 기록 · 재생. `ui/WorldPanel` ("월드"), Ctrl+S (`editor.save`) = 그 이름으로 저장, SandboxClient `--saves`.
- 테스트 (Phase 12 완료 기준): network test_world_ops (메시지 · 저장 · 불러오기 · 같은 배치 거절 · 늦게 들어옴 · 콘텐츠 다름 ·
  기록 → 재생 · 재생 제어 · 멈출 때 쓰기 · 스냅숏 쥐기 · 이른 WorldChanged), test_multi_client (5 역할 × 명령 12 종 + 월드 요청 5 종,
  두 editor 동시 편집 수렴), core edit_session (+ CTest det_edit_session · det_replay_edit_session), editor
  test_editor_world_change, client test_world_panel, 옵션 시험.
- 문서: ADR-0031, 10-EDITOR(상태 · 2 · 7 · 9 · 10장), 08(상태 · 4 · 5 · 12 · 13장, 프로토콜 7), 09 4.1, 01 4장, 13, 15, 16(12D ✅ ·
  Phase 12 완료), 17, MANUAL-QA 12D, README.

**왜**

- 클라이언트는 월드를 갖지 않는다 (ADR-0026) — 저장 · 불러오기는 서버의 일이다. 원격 클라이언트가 서버 파일을 고르는 것은
  "저장 폴더 안의 이름" 으로만 (경로 · .. 금지).
- 월드를 통째로 바꾸는 것은 epoch 재동기화(ADR-0025)와 같은 길에 얹고, 경계가 바뀔 수 있어 복제 월드를 다시 만든다. 채널 사이
  순서가 보장되지 않아 "옛 스냅숏이 새 월드에 붙는" 경우를 generation 으로 막는다 (ADR-0031 결정 3).
- 재생을 서버 월드로 하면 모두가 같은 재생을 보고, 끝난 자리에서 그대로 이어 편집한다. call 단위라 일시정지 편집까지 같다 (D3).
- 되돌리기 기록은 netId 로 대상을 가리킨다 — 다른 월드에서는 엉뚱한 개체를 바꾸므로 비운다.

**검증**

- Linux clang Debug (커밋마다) · gcc Debug: ctest 46/46 (골든 그대로 — det_edit_session · det_replay_edit_session 추가).
  TSan network · net · client · editor · content 129 케이스 경고 0.
- MinGW Release 빌드 경고 0 (커밋마다) + Wine 230 → 238 → 243 케이스. Wine 창 ecosystem_small --saves (sbx_sim_check 로 만든
  리플레이 · 세이브): 접힌 "월드 — 보통" 제목 줄, 펼친 패널의 목록([리플레이] · tick · 개체) · Ctrl+S → 목록에 quicksave · 결과
  줄. 처음 둔 펼친 패널은 인스펙터 · 편집 패널과 겹쳐 접어 두고 제목 줄에 상태를 보이게 바꿨다. Ctrl+S 가 처음엔 에디터 없는
  선택 경로에만 있어 듣지 않았다 — 월드 입력 공통 자리로 옮기고 앱 시험(Ctrl+S → 세이브 폴더)을 더했다.
- Wine · xdotool 의 클릭이 ImGui 단추에 닿지 않아 불러오기 · 재생 단추는 단위 시험(로컬 서버 · 패널 그리기)으로 확인했다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드(프로토콜 7 — 서버 · 클라이언트 둘 다) → ctest, MANUAL-QA Phase 12D.
- [계획] 세이브 지우기 · 이름 바꾸기 · 썸네일 · 자동 저장, 클라이언트로 내려받기 · 올리기, 재생 되감기 · 시크, 저장을 Worker 로.
- 다음: Phase 13 (Linux — X11 → Vulkan → Wayland).

---

## 2026-10-12 — Phase 12C: 역할 · 접속자 · 콘텐츠 편집 — 역할은 Net 절반의 접속자 표로, 콘텐츠는 EditContent → 월드 오버레이 (프로토콜 6)

**무엇을**

- core: `content/ContentOverlay` (종류 · id → JSON, 지문). 로더가 읽은 파일 텍스트를 DB 에 남기고(ContentSources)
  `ContentLoader::applyOverlay` 가 팩 원본 + 오버레이로 다시 만든다 (팩 항목을 바꾸면 그 자리를 건너뛰고 오버레이 쪽을 읽는다,
  Rule 은 원래 정의 순서). 항목마다 raw JSON · overlay 표시, findRule. 명령 `EditContent` (CommandJson "content").
  SimulationWorld = 팩 DB + 오버레이 DB, content() 는 지금 것, contentRevision · setContentOverlay · 행동 상태를 상태 id 로 옮기기.
  WorldHash 끝에 오버레이 지문 (비면 없음), 세이브 overlay/<종류>/<id>.json · 로드(실패면 경고 + 팩 콘텐츠).
- network: 프로토콜 6 — 명령 태그 8, ContentOverlay(42) · PlayerList(61) · SetRole(63) · SetRoleResult(64), Auth.ownerKey.
  ServerHost: 접속자 표 알림 · 역할 바꾸기(net::roleCanAssign) · 토큰 표에 역할 · 주인 열쇠, Sim 절반이 오버레이 차이 · 들어온
  클라이언트 전체를 보낸다. ClientSession: players · role · requestRole · overlay. CommandValidator: EditContent = ContentEdit.
  SandboxServer `--owner-key`.
- editor: EditPermissions (IEditorHost::permissions) — 막힌 툴 숨김 · 선택 툴로, Delete · Inspector · 되돌리기 막기.
  `Editor::editContent` (되돌리기 한 칸 — 이전 오버레이 값). `ui/ContentPanel` ("콘텐츠" 창 — 목록 · JSON · 검사 · 적용 · 팩 값으로 ·
  새 항목 · 요약), 편집 패널의 "콘텐츠 창" 단추 · 막힌 툴 숨김, 인스펙터 "보기만".
- client: NetworkSession — 오버레이를 팩 콘텐츠에 얹어 content(), permissions, players · requestRole, `--owner-key`.
  `ui/PlayersPanel` ("접속자"), 시뮬레이션 패널의 실행 제어를 역할로 끈다.
- 테스트: content/core/persist test_content_overlay, network test_roles_overlay (+ test_messages · test_server_host), editor
  test_editor_roles (FakeEditorHost 에 perms · contentDb), client test_roles_content, 옵션 시험.
- 문서: ADR-0030, 10-EDITOR(상태 · 2 · 7 · 8 · 9 · 10장), 08(상태 · 4 · 5장, 프로토콜 6), 03 4장, 04 5장, 09 3 · 7장, 11 0 · 1.2장,
  13, 15, 16(12C ✅), 17, MANUAL-QA 12C, README.

**왜**

- 역할은 시뮬레이션 상태가 아니다 — 명령이 아니라 Net 절반의 메시지로 두면 Sim 스레드를 거치지 않고 판단 · 알린다. 표 전체를
  보내 접속자 패널이 그대로 쓴다 (ADR-0030 결정 1 · 2).
- 콘텐츠 DB 는 색인 · action 번호 · 감지 질의 · 교차 참조가 얽혀 있다 — 부분 패치 대신 로더 한 길로 다시 만들어 팩과 같은 검증 ·
  같은 결과를 보장한다. 불변 DB 를 갈아 끼우면 System 코드는 그대로다 (결정 3).

**검증**

- Linux clang · gcc Debug: ctest 44/44 (골든 그대로). TSan network · net · client · editor · content 116 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine 229 케이스. Wine 창: 원격 서버(--default-role player --owner-key) + 두 클라이언트 — 손님 창의
  접속자 표 · 선택 툴만 · 2 키 거절 문구 · 실행 제어 꺼짐. 로컬 "콘텐츠" 창(규칙 JSON · 요약). 처음 둔 접힌 콘텐츠 창이 목록
  패널 제목과 겹쳐 "편집" 패널의 단추로 여닫는 창으로 바꿨다 (열 때 앞으로). Wine · xdotool 의 클릭이 ImGui 단추에 닿지 않아
  창 안의 조작은 단위 시험(패널 그리기)과 기본값을 바꾼 임시 빌드의 화면으로 확인했다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드(프로토콜 6) → ctest, MANUAL-QA Phase 12C.
- [계획] 폼 · 노드 그래프 편집, 팩 항목 끄기 · 오버레이를 팩으로 내보내기, 태그 · 머티리얼 편집, 강퇴 · 차단, 소유 개체.
- 다음: 12D 세이브 · 로드 · 리플레이 UI + 완료 기준 (edit_session D3 · 두 클라이언트 동시 편집 · 역할 × 명령 매트릭스).

---

## 2026-10-12 — Phase 12B: 인스펙터 · 목록 · 되돌리기 — 서버 전용 값은 선택 상세로 (프로토콜 5), 받아들여진 편집 한 번 = 한 칸

**무엇을**

- network: 프로토콜 5 — InspectEntry.components[] {stableId, json ≤ 4 KB} × ≤ 16. buildInspect 가 복제되지 않는 EditorVisible
  컴포넌트(core.movement · life.age · ai.sensor …)의 리플렉션 JSON 을 담는다.
- editor: `Undo.{hpp,cpp}` (UndoEntry · IdRemap · paintInverse · recreateCommand · withoutEntityRefs). Editor 에 기록(편집 한 번 =
  한 칸, 결과가 다 오면 받아들여진 명령만) · undo · redo (기다리는 동안 막기, 거절이면 칸 빼기) · setField · addComponent ·
  removeComponent · 카메라 맞추기 요청. 툴이 보내는 명령마다 역명령 (이동 -delta, 배치 → 새 netId 지우기, 지우기 → 다시 만들기,
  지형 → 이전 머티리얼). `ui/InspectorPanel` ("인스펙터" — 리플렉션 필드 위젯, 이름 순, "(서버)", 더하기 · 빼기 / "목록"),
  편집 패널에 되돌리기 · 다시 단추. IEditorHost 에 catalog · components · listEntities · positionOf.
- client: NetworkSession 이 위 넷을 구현 (복제 월드 + 선택 상세). Application: Ctrl+Z · Ctrl+Y · F(editor.focus), 두 패널.
- 테스트: editor test_undo (FakeEditorHost.hpp 로 가짜 host 공유), network (선택 상세 컴포넌트), client (로컬 서버에서 고치기 →
  되돌리기 · 지운 개체 되살리기 · 앱 Ctrl+Z).
- 문서: ADR-0029, 10-EDITOR(상태 · 2 · 4 · 6.2 · 9 · 10장), 08(상태 · 5장, 프로토콜 5), 13, 15, 16(12B ✅), 17, MANUAL-QA 12B, README.

**왜**

- 복제 월드에는 복제 컴포넌트뿐이라 사용자가 고칠 값 대부분(maxSpeed · 감지 반경 · 수명)이 안 보였다. 전부 복제하면 모든 개체의
  대역폭이 늘어난다 — 고른 개체만 오가는 선택 상세 길에 얹었다 (ADR-0029).
- 되돌리기는 10-EDITOR 6.2 그대로 — 서버가 받아들인 것만, 역명령도 보통 명령. 지운 개체는 새 netId 로 돌아오므로 기록을 잇는 표가
  필요했다.

**검증**

- Linux clang · gcc Debug: ctest 44/44. TSan client · editor · network 88 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine: 187 케이스. Wine 창: 박스 선택 → 인스펙터(복제 · 서버 컴포넌트) · 목록, 배치 두 번 → Ctrl+Z
  (서버 수락 4 · 거절 0). 컴포넌트가 stableId 순이라 뒤섞여 보여 이름 순으로, 목록 패널을 네트워크 패널과 겹치지 않는 자리로 옮겼다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 12B.
- [계획] 덮어쓰기 경고 · 잠금, 되살린 개체의 saveId · 관계, enum 이름 · 태그 칩 · EntityRef 고르기 · "뷰에서 선택", batchId.
- 다음: 12C 역할 · Players · 콘텐츠 편집, 12D 세이브 · 리플레이 UI · 완료 기준 시험.

---

## 2026-10-09 — Phase 12A: 에디터 툴 — SandboxEditor · IEditorHost · 선택 · 이동 · 배치 · 지형 · 지우기 · EditPreview

**무엇을**

- editor/ = SandboxEditor (새 라이브러리, Core + Render + imgui, Network 금지): `EditorHost.hpp` (ICommandSink · IEditorHost),
  `Editor` (툴 다섯 · 설정 · EditPreview · 결과 처리 · 덧그림 · 상태 줄), `ui/EditorPanel` ("편집" 패널 — 툴 · Prefab 목록 ·
  거르기 · 끌기 간격 · 머티리얼 · 브러시 반지름 · 모양 · 격자 맞춤 · 명령 수 · 거절 사유). 루트 CMake 가 늘 만든다.
- client: NetworkSession 이 IEditorHost — submit(=명령 보내기) · 결과를 에디터 몫으로 · 콘텐츠 · 복제 지형 · 스냅숏 고르기 ·
  setSelection · setPreview(그릴 때 스냅숏 위치에 더한다) · 연결이 바뀌면 기다리던 결과를 거절로. IWorldSession::editorHost.
  Application: 세션이 host 를 주면 왼쪽 · 오른쪽 · Delete · Esc · 숫자 키(editor.tool.* = Digit1 ~ 5)를 에디터로, 덧그림 ·
  편집 패널 · 제목 줄 "툴 …". host 가 없으면(가짜 세션) 8B 선택 그대로.
- 테스트: editor 스위트 (test_editor — 가짜 host), client (test_editor_session — 로컬 서버까지, 앱 숫자 키 · 클릭). CTest unit_editor.
- 문서: ADR-0028, 10-EDITOR(상태 · 2 · 3 · 6.1 · 9 · 10장), 06 10장, 13, 15(타깃 · 실행), 16(Phase 12 를 12A ~ 12D 로, 12A ✅), 17,
  MANUAL-QA 12A, README.

**왜**

- Phase 12 는 1차 목표선이지만 일곱 묶음이라 넷으로 나눈다 (ADR-0028). 12A 로 "놓고 · 옮기고 · 칠하고 · 지운다" 가 서버 권한
  그대로(싱글플레이도) 돈다 — Inspector · Undo · 역할 · 콘텐츠 편집은 그 위에.
- 10-EDITOR E2: 에디터는 Network 를 모른다 — 복제 월드 · 선택 · 명령 결과를 가진 NetworkSession 이 host.
- 원안 단축키 Q W E B X 는 카메라 W A S D 와 겹친다 → 숫자 키.

**검증**

- Linux clang · gcc Debug: ctest 44/44 (unit_editor 7 케이스, client 28 케이스). TSan client · editor 35 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine: network · net · core · foundation · client · editor 179 케이스.
- Wine 창 (ecosystem_small, 일시정지): 3 배치 클릭 · 4 지형 끌기(바위 띠 · 브러시 원) · 2 이동(끄는 동안 preview 자리 = 놓은 뒤 서버
  자리, 깜빡임 없음), 서버 "수락한 명령 12 · 거절 0". 편집 패널이 오른쪽 아래에서 잘려 크기를 고정했다.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 12A (원격 편집 · player 역할 거절).
- 12B Inspector · Hierarchy · Undo/Redo, 12C 역할 · Players · 콘텐츠 편집, 12D 세이브 · 리플레이 UI · 완료 기준 시험.
- [계획] 배치 미리보기 그림, 툴 커서 모양, 패널을 SandboxEditor 로 옮기고 도킹 레이아웃.

---

## 2026-10-08 — 측정: 사용자 PC (MSVC Release) sbx_bench --only net.

**무엇을** 사용자가 보낸 결과를 14-PERFORMANCE 7.11 · 16-ROADMAP Phase 11 에 기록. 바이트 · 관심 개체 · late_join 스냅숏 수가
컨테이너(clang)와 정확히 같고, 만들기 시간은 클라 16 · 화면 13.5 ms (컨테이너 37 ms), 훑기 약 2 ms.

**남은 일** 14 1.4 의 3 ms 목표는 여전히 [계획 15]. MANUAL-QA 11 의 나머지 (확대 · 다시 접속).

---

## 2026-10-08 — 문서: sbx_bench 는 SBX_BUILD_BENCH=ON 으로 구성해야 생긴다

**무엇을** MANUAL-QA Phase 11 · 15-BUILD 에 sbx_bench 를 켜는 방법(CLion 프로필 CMake options 에 `-DSBX_BUILD_BENCH=ON`)을
적었다. 15-BUILD 4장의 sbx_bench 링크 목록을 실제대로 (Core Network Render).

**왜** 사용자 보고 "sbx_bench 빌드가 안 된다" — cmake-build-release 의 캐시가 `SBX_BUILD_BENCH=OFF`(기본값)라 타깃이 없었다.
MANUAL-QA 11 이 켜는 단계 없이 실행 명령만 적었다.

**검증** MinGW Release 에서 SBX_BUILD_BENCH=ON 으로 sbx_bench 빌드 경고 0, Wine 에서 `sbx_bench --quick` 끝까지 (net.* 포함).
MSVC 로는 아직 빌드한 적 없다 — 사용자 PC 에서 처음.

---

## 2026-10-08 — Phase 11: 관심 영역 — Subscribe 청크 사각형 · 지연 해제 · 예산 우선순위 · 관심 지형 · 세션 토큰 다시 접속

**무엇을**

- network: 프로토콜 4 — `Subscribe` (청크 사각형 또는 all). `ReplicationWriter`: 스냅숏마다 위치 → 청크를 한 번 훑고,
  클라이언트마다 관심 = 구독 청크 비트맵 ∪ 늘 보낼 것(`setAlwaysRelevant` — Inspect 로 선택한 개체 · 위치 없는 개체).
  빠진 청크는 30 스냅숏 지연 해제, 관심 밖 = despawn · 들어오면 spawn. 예산이 있으면 늘 보낼 것 → spawn → update, 같은
  단계는 중심 거리 − 미룬 틱 × 0.5 칸 (round-robin 삭제). 지형은 관심 청크만 · 가까운 것부터 · 클라이언트가 들고 있다.
  ServerHost: Subscribe → setInterest, Inspect 집합 → setAlwaysRelevant. 세션 토큰 표 (60 초, ClientQuit 면 지움, 살아 있는
  연결의 토큰이면 옛 연결 Kicked). ClientSession::subscribe · desc.token. LoopbackTransport::severAll (시험).
- client: `IWorldSession::setView` (Application 이 카메라 visibleRect), `NetworkSession::interestFor` (+ 1 청크 · 경계로 자름 ·
  전체면 all, 초당 2 번). --connect 가 시간 초과로 끊기면 같은 토큰으로 2 초마다 다시 (60 초), 그동안 옛 복제본. 네트워크
  패널 "관심 영역" · "다시 접속".
- bench: `net.snapshot` · `net.late_join` (bench/NetBench.cpp, sbx_bench 가 SandboxNetwork 링크).
- 테스트: test_interest (새 파일), test_server_host (토큰), test_network_session (interestFor · 원격 다시 접속), test_messages.
- 문서: ADR-0027, 08(상태 · 4 · 5 · 6.1 · 6.3 · 8 · 12 · 13장, 프로토콜 4), 13, 14(1.4 · 2 · 6 · 7.11), 15, 16(Phase 11 ✅), 17,
  MANUAL-QA 11, README.

**왜**

- Phase 10 은 모든 엔티티를 모든 클라이언트에 보냈다 — 50k 면 클라이언트당 스냅숏 1 MB (14 MB/s). 256 KB/s 예산에서는
  화면 안 개체도 늦게 왔다 (14 7.9). 완료 기준: 바이트 ∝ 가시 엔티티, Late Join < 2 초.
- 처음 구현(거리 순 우선순위만)은 50k 화면 새 접속이 77 스냅숏(5.1 초) — 가까운 개체의 매 틱 갱신이 예산을 먹었다 →
  spawn 을 update 앞에 두어 5 스냅숏(0.33 초).
- 다시 접속: 16-ROADMAP 11.4. 무선 · 잠깐의 끊김에 월드를 잃지 않게.

**검증**

- Linux clang · gcc Debug: ctest 43/43. TSan network · net · client 73 케이스 경고 0.
- MinGW Release 빌드 경고 0 + Wine: network · net · core · foundation · client 170 케이스.
- Wine 창 (ecosystem_survival, 서버 스레드): Home(월드 전체) 2,316 개체 → 휠 확대(22.6 px/칸) 뒤 관심 "청크 -1..2 × -2..1",
  클라이언트 개체 692 · 받은 KB/s 425 → 145.
- 측정 (14 7.11, Release `sbx_bench --only net.`): 바이트 ≈ 관심 개체 × 19.6 B (월드 전체 978 KB · 화면 49 KB), 새 접속 화면
  0.33 초 · 월드 전체 6.07 초, 클라 16 화면 만들기 37 ms.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 11 (확대 · 다시 접속 — resmon 으로 서버 일시 중단).
- [계획 15] 클라이언트 16 · 50k 스냅숏 만들기 < 3 ms (14 1.4) — 같은 개체 인코딩 바이트 재사용 · Net 스레드 인코딩.
  위치 양자화 · 필드 마스크 (예산 없이 화면 하나 600 ~ 720 KB/s). 소유 개체 alwaysRelevant · 역할 바꾸기 (Phase 12).
- 다음: Phase 12 — Multiplayer Editor.

---

## 2026-10-08 — Phase 10B: SandboxClient 가 서버의 복제본을 그린다 — LocalServerHost · NetworkSession · 보간 · Inspect · --direct-sim 삭제

**무엇을**

- core: `scenarios/WorldSource` (`openWorldSource` — 시나리오 이름 · 세이브 폴더 → 월드). SandboxServer --world 의 코드를 옮겨
  클라이언트 --world 와 같이 쓴다.
- network: 프로토콜 3 — `Inspect`(볼 netId ≤ 32) · `InspectResult`(행동 상태 · 감지 반경 · 남은 경로 · 목표 · 대상 netId, Snapshot
  채널). ServerHost: 클라이언트별 선택 상세 집합, 스냅숏 간격을 실제 시간으로 (×8 에서도 15 Hz). `LocalServerHost`
  (ServerHost + Loopback 둘, 역할 owner · 예산 없음, Threaded | Inline). `InterpolationClock` (서버 틱 추정 · renderTick,
  지연 0.1 초 · 앞으로만 · 일시정지 · 재개 · 다시 맞춤). ClientWorld: `sampleTransform` (선형 · 최단 회전 · 순간이동 · 같은 틱),
  직전 표본 틱 ≥ 직전 스냅숏 틱. ClientSession: `setInspect` · `inspect`.
- client: `NetworkSession` (IWorldSession — 로컬 · 원격 접속, 콘텐츠가 다르면 서버 팩을 읽어 다시, 첫 스냅숏까지 Connecting,
  거절 · 끊김이면 실패, 일시정지 · 한 틱 · 속도 = 서버 명령, 선택 netId + Inspect, 받은 KB/s · 적용 ms). SpriteExtraction 은
  ClientWorld + renderTick 에서 (render.sprite Opaque 캐시 netId 별, 지형은 바뀐 청크만). Application: Connecting 에서 update ·
  ready 대기 · failure 면 종료 코드 1. "네트워크" 패널. 옵션 `--world` · `--connect` · `--name` (--direct-sim · DirectSim 삭제).
- foundation: `console::utf8Arguments` — Windows 에서 명령줄을 UTF-16 에서 UTF-8 로 (Wine 원격 QA 에서 `--name 원격` 이 "??" 로
  보여 찾았다). SandboxClient · SandboxServer · sbx_net_probe · sbx_sim_check 에 적용, shell32 링크.
- 테스트: unit_network (Inspect 메시지 · 보간 시계 · 표본 · 선택 상세 · LocalServerHost ×8 15 Hz · 없는 월드), unit_client
  (test_network_session — test_direct_sim 대신), foundation (명령줄 UTF-8). CTest `client_local_world_headless` (tick 28 ·
  다시 맞춤 0) · `client_world_bad_name` (client_direct_sim_headless 대신).
- 문서: ADR-0026 (ADR-0021 Superseded, 0020 결정 5 입력 · 6 대체), 08(상태 · 4 · 5 · 6.3 · 6.4 · 9 · 12 · 13장, 프로토콜 3), 06 9장,
  01, 07 6.1, 13, 14 7.10, 15, 16(Phase 10 ✅), 17, MANUAL-QA 10B (8A ~ 8C 명령을 --world 로), README.

**왜**

- 16-ROADMAP 2장 · 10.5: "--direct-sim 은 Phase 10 완료 기준에서 삭제를 강제한다" — 싱글플레이도 명령 · 권한 · 복제를 같은 길로.
- 선택 상세(8B 의 감지 반경 · 경로 · 대상)는 서버 전용 컴포넌트라 복제되지 않는다 → 고른 개체만 Inspect 로.
- 10A 의 "Simulation 단계 2 번마다" 스냅숏은 ×8 에서 120 Hz 가 되어 대역폭 · 클라이언트 적용이 속도에 비례했다 → 실제 시간.

**검증**

- Linux clang Debug: ctest 43/43. 헤드리스 `--world ecosystem_small --frames 60` 이 두 번 모두 tick 28 (결정적).
- 네이티브 원격: Release SandboxServer ecosystem_survival · 10k + 헤드리스 SandboxClient --connect (15 Hz, 다시 맞춤 0).
- Wine (MinGW Release): SandboxClient --world ecosystem_small 창 (55 fps, 네트워크 패널, 패널 버튼 일시정지 = 서버 명령),
  SandboxServer + SandboxClient --connect --name 원격 (콘텐츠 다시 맞추기, 4.3k 개체, 한글 이름).
- 측정 (14 7.10): 10k Release 추출 1.9 ms · 스냅숏 적용 약 8 ms (15 Hz), Debug 적용 약 45 ms.

**남은 일**

- 사용자 PC (MSVC): 다시 빌드 → ctest, MANUAL-QA Phase 10B (8A ~ 8C 의 남은 항목도 이제 --world 로).
- [계획] 스냅숏 디코드 · 적용을 클라이언트 Net 스레드로 (10k 에서 66 ms 마다 튐), 메뉴 · 다시 접속 UI, 외삽, Phase 11 Interest ·
  재접속 토큰, Phase 12 인스펙터.

---

## 2026-10-08 — Phase 10A: 복제 — 변경 순번 · ReplicationWriter · ClientWorld · 프로토콜 2 · net_convergence

**무엇을**

- core: `SimulationWorld::changeStamp` — tick() 마다 `max(이전 + 1, 틱)` 로 오르는 변경 순번을 레지스트리 currentTick 으로 쓴다
  (일시정지 편집 단계도 다른 값). `core/serialization/BinaryCodec` (리플렉션 ↔ 바이트: LEB128 · zigzag · f32 비트 그대로 ·
  EntityRef 는 EntityRefCodec), `ComponentInfo::writeBinary/readBinary`.
- network: 프로토콜 2 (Welcome.replicated · Snapshot · SnapshotAck · TerrainChunk 길이 부호화, 스트림 메시지 16 MB).
  `ReplicationWriter` — 클라이언트별 스냅숏 기록 32 개, ack 된 기준 + inflight 합으로 spawn · update · despawn, 바이트 예산
  round-robin, 기준을 잃으면 epoch 으로 다시 맞추기, 바뀐 지형 청크 (스냅숏당 64). `ClientWorld` — 시뮬레이션 없는 복제 월드
  (Registry · WorldGrid · netId 표 · Opaque · transform 표본). ServerHost: 단계 2 번마다 스냅숏(일시정지 중에도), 클라이언트당
  예산, roster · ack 큐, replicationMs. ClientSession: Welcome 뒤 ClientWorld 를 만들고 적용 · ack, Welcome 보다 먼저 온 지형을
  모아 둔다.
- SandboxServer `--snapshot-kbps` (기본 256, 0 = 제한 없음), 끝 줄 `replication X ms`. sbx_net_probe 가 복제 개체 수를 보인다.
- 테스트: unit_network 에 복제 6 케이스, CTest `net_convergence` (서버 + 클라이언트, Loopback · 100 ms ± 20 · 5 % 손실 양방향 ·
  64 KB/s — 복제 컴포넌트 · Opaque · 지형이 바이트 단위로 같다). `unit_*` · `net_convergence` 는 케이스가 0 개면 실패.
- 문서: ADR-0025, 08(상태 · 4 · 5 · 6 · 7 · 9 · 12 · 13장), 02 8장, 03, 09 5장, 13, 14 7.9, 15, 16(Phase 10 → 10A ✅ · 10B),
  17, MANUAL-QA 10A, README.

**왜**

- 16-ROADMAP Phase 10 — LocalServerHost 와 --direct-sim 삭제(10B)는 클라이언트가 서버 월드를 받아 볼 수 있어야 시작할 수 있다.
  Phase 10 을 10A(복제 자체 · 수렴 시험)와 10B(SandboxClient 연결 · 보간)로 나눠 복제를 그리기 없이 바이트 비교로 먼저 확정한다.
- 틱 번호만으로는 일시정지 편집을 놓쳤고, 편집 순번을 기준에 섞으면 같은 틱에 움직인 엔티티를 모두 다시 보냈다 (29 개 대
  2 개) → 변경 순번 (ADR-0025).
- 사용자 PC 에서 다시 빌드하지 않은 ctest 가 unit_network 0 케이스로 "통과" 했다 → 0 케이스 실패 규칙.

**검증**

- Linux clang Debug · gcc Debug: ctest 42/42 (새: net_convergence — Loopback 249 개체, 나쁜 링크 330 개체 수렴). 결정론 골든
  그대로 (변경 순번은 해시와 무관).
- TSan: SandboxTests network · net 36 케이스, 경고 0.
- MinGW 전체 빌드 경고 0. Wine 11: SandboxTests network · net · core · foundation 132 케이스.
- Release, ecosystem_10k + probe (ENet 127.0.0.1): 스냅숏 만들기 제한 없음 5.4 ~ 5.7 ms · 256 KB/s 1.5 ms, 틱 7 ms 안팎 (14 7.9).

**남은 일**

- 사용자 PC (MSVC): **다시 빌드한 뒤** ctest (net_convergence 포함), MANUAL-QA Phase 10A.
- [계획 10B] NetworkSession · `SandboxClient --connect` · LocalServerHost, 그리기를 ClientWorld 에서, 보간(SnapshotBuffer),
  선택 상세(Inspect), --direct-sim 삭제, Network 패널. [계획] 양자화 · 필드 마스크 · Event 복제 · EntityRef 대기 목록 ·
  Opaque 갱신 · 클라이언트 간 바이트 재사용. [계획 11] Interest · 우선순위.

---

## 2026-10-08 — Phase 9: 네트워크 기초 — ENet · 핸드셰이크 · 명령 · ServerHost · SandboxServer --world · sbx_net_probe

**무엇을**

- `external/enet`: ENet v1.3.18 C 소스를 수정 없이 (Debian `enet_1.3.18+ds.orig.tar.xz`, SHA256 = 서명된 .dsc 값). `sbx_enet`
  정적 라이브러리 — 루트가 C 언어를 켠다, Linux 는 업스트림과 같은 configure 검사, Windows 는 ws2_32 · winmm.
- `network/` (SandboxNetwork, PUBLIC Core): Transport 계약(`INetworkTransport` — wait · drain · boundPort, ConnectionId 재사용 없음),
  LoopbackTransport(+ 허브, 스레드 사이) · SimulatedTransport(지연 · 지터 · 대역폭 · Snapshot 손실 · 옛것 버림 · 신뢰 채널 재전송
  지연, seed · 주입 시계) · EnetTransport(채널 3, disconnect_later, 15 초). BitStream(LSB 우선 · LEB128 · zigzag · 상한 · 엄격한
  UTF-8 · 오류 플래그). Messages(kProtocolVersion 1, Hello · Challenge · Auth · Welcome · Reject · Command · CommandResult ·
  ServerStats · Disconnect, 나머지 id 는 예약) · CommandCodec(명령 11 종, JSON 값은 텍스트).
- ServerHost: Net 절반(핸드셰이크 · 거절 6 가지 · 10 초 시간 초과 · ProtocolError 끊기 · CommandValidator) + Sim 절반(ScenarioRunner,
  executeTick = 다음 틱, 네트워크 명령의 결과만 돌려줌, 1 초마다 ServerStats), inbox · outbox · status 큐. Inline · Threaded.
  CommandValidator: 순번 → 속도 제한(초당 120) → 권한(10 7장 표 — 편집 Editor, 시뮬레이션 제어 Admin). ErrorCode::RateLimited.
- ClientSession: 핸드셰이크 · 명령 · 결과 · 통계 · 끊기 이유.
- SandboxServer `--world <시나리오|세이브 폴더> --port --bind --max-clients --default-role --threads --ticks N --exit`, Ctrl+C 로
  서버 종료 알림, 실제 포트 출력, SIGPIPE 무시. `sbx_net_probe`(tools/net_probe): 접속 · 명령 · 결과 · 통계, 콘텐츠가 다르면
  서버가 알려 준 팩을 읽어 다시 접속.
- 문서: ADR-0024, 08(상태 · 2 · 4 · 5 · 5.1 · 10 · 12 · 13장), 01 · 03 · 09 · 10 상태, 13, 15, 16(Phase 9 ✅), 17, MANUAL-QA 9,
  external/README, README.

**왜**

- 16-ROADMAP Phase 9 — 에디터(12)가 처음부터 명령을 서버로 보내는 구조로 태어나야 하고(2장), Phase 10 복제 · LocalServerHost 가
  이 위에 선다. --direct-sim 을 지우려면(10.5) 네트워크 경로가 먼저 있어야 한다.
- 검증을 Net 쪽에 두어 거절될 명령이 Simulation 스레드에 닿지 않게, Inline 모드로 핸드셰이크 · 시간 초과 · 속도 제한을 실제
  시간 없이 결정적으로 시험한다 (ADR-0024).

**검증**

- Linux clang Debug: ctest 41/41 (새: unit_network 28 케이스 · server_world_exit · server_world_bad_name · net_server_probe_smoke).
  `SandboxServer --world random_walk_1k --ticks 60 --exit` 의 해시가 헤드리스 `--scenario random_walk_1k --ticks 60` 과 같다.
- MinGW 전체 빌드 경고 0. Wine 11: SandboxTests network · server · tools · foundation 76 케이스 (ENet 이 winsock 위에서),
  SandboxServer.exe + sbx_net_probe.exe 두 프로세스 (콘텐츠 해시 다시 맞추기 · 명령 3 개 수락 · 나감 로그).
- 수동: 서버 + probe 로 MANUAL-QA 9 의 흐름 (Pause/Step/Resume/Speed/Create 수락, editor 의 Pause 거절 → 종료 코드 3, Ctrl+C →
  probe 에 "서버 종료", 서버 없음 → 10 초 뒤 접속 실패).

**남은 일**

- 사용자 PC (MSVC): ctest, MANUAL-QA Phase 9 (방화벽 · 다른 PC 접속).
- [계획 Phase 10] 복제(NetEntityId · Snapshot · Bulk · 보간), SandboxClient 의 네트워크 세션 · LocalServerHost, --direct-sim 삭제.
  [계획 11] Subscribe · Interest · 재접속 토큰. [계획 12] 역할 바꾸기 · Chat · ContentOverlay. 서버 --record-replay · --autosave.

---

## 2026-10-07 — Phase 8C: ImGui (1.92 docking) · 기본 패널 — Phase 8 완료

**무엇을**

- `external/imgui`: Dear ImGui v1.92.9b docking 을 수정 없이 벤더링 (crates.io dear-imgui-sys 0.18.0 의 cimgui 서브모듈 사본,
  crate SHA256 external/README 에). 정적 라이브러리 `sbx_imgui` (IMGUI_DISABLE_OBSOLETE_FUNCTIONS, 경고 끔).
- `render/imgui/ImGuiRenderer` + `shaders/imgui.hlsl`: 1.92 동적 텍스처 프로토콜(RendererHasTextures) — 만들기 · 바뀐 사각형
  갱신 · 지연 해제, ImTextureID = 칸 + 1, Alpha8 → 흰색 + 알파. 업로드 링 정점 · uint16 인덱스, 명령마다 scissor +
  drawIndexed(VtxOffset), ResetRenderState 는 platformIo 콜백. `Renderer::record` 에 UI 콜백 + 타임스탬프 kMarkUi →
  `GpuPassTimes.uiMs`.
- `apps/client/ui/ImGuiLayer`: PlatformEvent → ImGuiIO (수정자는 좌우 키 상태), WantCaptureMouse/Keyboard →
  `InputSystem::setCapture` (I1), 커서 모양 · 클립보드 · 글자 칸에서 IME 켜기/끄기, 키보드 내비게이션 끔, imgui.ini 안 씀.
  폰트: `--font` → 맑은 고딕 → 내장 벡터 폰트 (동적 아틀라스 — 쓴 글자만).
- `apps/client/ui/DebugPanels`: "시뮬레이션"(tick · TPS · 일시정지 · 한 틱 · 속도 · 격자 · 자세히 · 맞춤 · 선택 설명 · 해제),
  "통계"(fps 그래프 · CPU 구간 · GPU 패스 · Draw · UI · 디바이스 · ImGui 데모). 패널은 `PanelActions` 를 돌려주고
  Application 이 단축키와 같은 함수로 적용. F1 숨기기. `IFrameRenderer::info()` · `IWorldSession::info()` 가 수치를 준다.
- 옵션 `--font <파일>`, `--no-ui`. ADR-0023. 06 10 · 14장, 07 (I1), 13, 15, 16 (Phase 8 완료), 17, MANUAL-QA 8C.

**왜**

- 16-ROADMAP 8.4 — Phase 8 의 마지막 항목. 에디터(Phase 12)와 이후 디버깅이 화면 패널을 전제한다.
- 1.92 동적 텍스처: 한글 UI 에서 정적 아틀라스는 수 MB 를 굽는다. 요청 처리는 RHI 에 이미 있는 텍스처 생성 · 영역 복사뿐.
- 패널 → 액션: 동작이 키보드 단축키와 한 곳에서 정해지고, 단위 테스트가 그리지 않고 액션을 본다.

**검증**

- 단위 (SandboxTests, client 스위트): test_imgui_layer 5 케이스 — 키 · 커서 변환 전부, 이벤트 → io, 가로채기 · IME 켜기/끄기,
  Application + UI (I1: 패널 위 클릭이 선택을 만들지 않는다, F1), 패널 액션.
- GPU (sbx_render_tests, Wine + lavapipe): test_imgui 2 케이스 — 기준 이미지 imgui_basic (내장 비트맵 폰트, 허용 8 · 2%),
  창 밖은 지운 색 그대로, 두 번째 프레임에 텍스처를 다시 만들지 않음, 큰 폰트 → 갱신, 화면 밖 창 = Draw 0, 끝나면 텍스처 수 원래대로.
  render 28 케이스 통과, 끝 로그 살아 있는 리소스 0.
- 빌드 · ctest: Linux clang Debug · RelWithDebInfo, gcc Debug 37/37 (경고 0), MinGW 전체 빌드 경고 0. 헤드리스 클라이언트 tick 28 유지.
- Wine 창 QA: 한글 패널(Noto CJK 를 --font 로), 일시정지 버튼 클릭 → 멈춤.

**남은 일**

- 사용자 PC: `ctest -L render` (imgui_basic), MANUAL-QA 8C (맑은 고딕 · 패널 · F1 · 입력 가로채기 · 데모 창 한글 입력), UI GPU 시간.
- [계획] imgui.ini · 도킹 레이아웃 · 인스펙터는 Phase 12 에디터와 함께 (SandboxEditor 로 옮긴다).

---

## 2026-10-07 — Phase 8B 측정: 사용자 PC 의 실제 GPU

**무엇을**

- 사용자 PC (MSVC Release, `--direct-sim ecosystem_10k --vsync off`, 4.3 px/칸) 제목 줄 수치를 14-PERFORMANCE 7.8 · 16-ROADMAP 에 적었다:
  1,628 fps · 렌더 CPU 0.4 ms · 추출 0.1 ms · Draw 2 · GPU 0.05 ms (지형 0.04 · 스프라이트 0.01 · 격자 0.00 · 선 0.00) ·
  30.0/30 TPS · 틱 5.2 ms (개체 9,652). 그 전의 8A 빌드는 VSync 켬에서 144 fps (모니터 상한).

**왜**

- Wine + lavapipe 수치(지형 3 ~ 5 ms)는 소프트웨어 래스터라 실제 비용을 말하지 않는다 — 실제 GPU 로 확인했다.

**검증**

- 사용자 스크린숏의 제목 줄. Phase 8 완료 기준 "10k 스프라이트 60 FPS · Draw ≤ 16" 충족.

**남은 일**

- MANUAL-QA 8B 의 선택 · 격자 · --rhi-debug 항목, ctest -L render.

---

## 2026-10-07 — Phase 8B: 지형 · 격자 · 선택 · 디버그 선 · 패스별 GPU 시간

**무엇을**

- RHI: GPU 타임스탬프 — `ICommandList::writeTimestamp(i)` · `resolveTimestamps(n)`, `IRenderDevice::completedTimestamps()`
  (프레임 슬롯마다 32 칸, beginFrame 이 슬롯을 다시 쓰기 전에 옮긴다 — GPU 를 기다리지 않음). D3D12 쿼리 힙 + Readback 버퍼.
  `Format::R16Uint`.
- TerrainPass: 타일 머티리얼 번호 텍스처(R16 UINT) + 팔레트(RGBA8 1 행), 월드를 덮는 사각형 하나 · Draw 1개, revision 이
  바뀐 청크만 영역 업로드(프레임 4 MB), TerrainCache(순수 로직). 확대하면 타일마다 밝기 결, 없는 번호 = 마젠타.
- GridPass(타일 · 청크 선 · 월드 경계를 픽셀 셰이더로), LinePass(선마다 인스턴스, 화면 픽셀 두께) — SelectionPass · DebugPass.
  DebugDrawList(line · rect · box · circle · arrow · polyline). Renderer 순서: Clear → Terrain → Sprite → Grid → Selection →
  Debug, 각 뒤에 타임스탬프 → RendererStats.gpu (패스별 ms). 셰이더 terrain · grid · lines.
- 클라이언트: capture 가 지형 청크를 revision 이 바뀔 때만 복사(스냅숏끼리 공유) · 팔레트 = "terrain/<id>" 색, 선택한 개체
  (최대 32)의 프리팹 · 상태 · 에너지 · 체력 · 감지 반경 · 경로 · 목표 · 대상 · 속도. presentation/SelectionOverlay(고르기 ·
  외곽선 · 자세히 · 제목 줄). IWorldSession 에 선택 API. 조작: 왼쪽 클릭 · 끌기 박스 · Shift 더하기/빼기 · Esc · G 격자 · V 자세히,
  카메라 끌기는 가운데 버튼만. 일시정지 중 선택이 바뀌면 진행 없이 다시 capture. 배경 스프라이트는 지형이 대신한다.
- 제목 줄: "선택 eco.rabbit #… · flee · 에너지 … · 체력 … · 경로 …", "GPU … ms (지형 · 스프라이트 · 격자 · 선)".
- assets/ecosystem/materials.json: 흙 · 물 · core.* 지형 색.
- 문서: ADR-0022, 06(상태 · 8.1 · 8.3 · 8.4 · 8.5 · 8.6 · 9 · 14장), 11(지형 색), 13, 14(7.8), 15, 16(8.3 · 8.6 ✅), 17,
  MANUAL-QA 8B, README.

**왜**

- 8A 의 월드는 단색 사각형 위의 개체뿐이었다 — 호수 · 흙이 안 보이고, 무엇이 왜 움직이는지 볼 방법이 없었다.
- 타일 하나 = 텍셀 하나면 정점 메시 없이 청크 수와 무관하게 Draw 1개, 구현마다 같은 픽셀 (ADR-0022).
- 줌 범위(0.25 ~ 512 px/칸)에서 선이 보이려면 화면 픽셀 두께여야 한다.

**검증**

- Linux: clang Debug/RWD · gcc Debug — 경고 0, CTest 37/37. TSan client · render · foundation 2회 경고 0.
- Wine 11.19 + lavapipe: sbx_render_tests 26 케이스 통과 (terrain · overlay 기준 이미지를 확대해 검토, vkd3d 도 타임스탬프를
  준다), 누수 0. SandboxClient ecosystem_survival: 지형 · 격자 · 박스 선택 21개 · 감지 반경 · 속도 화살표 스크린숏.
- ecosystem_10k 헤드리스 RWD: 틱 13.9 ~ 14.3 ms (8B 전 14.4 — 지형 capture 비용이 보이지 않는다), emit 0.10 ms (14 7.8).

**남은 일**

- 사용자 PC: ctest -L render (terrain · overlay 를 WARP 로), MANUAL-QA 8B (실제 GPU 의 패스별 ms).
- `[계획]` 지형 타일 그림 · 경계 섞기 · 밉맵, 화면 글자 지표(8C ImGui), 스트리밍 월드의 지형 고리 텍스처.

---

## 2026-10-07 — Phase 8A 후속: --direct-sim 을 Simulation 스레드로 (Debug 3 ~ 6 fps)

**무엇을**

- 사용자 보고: `--direct-sim ecosystem_10k --vsync off` 가 3 ~ 6 fps (MSVC Debug), Release 로 바꾸니 올랐다.
- 원인: DirectSim 이 렌더 스레드에서 ScenarioRunner.step 을 부르고 밀리면 한 프레임에 4 틱까지 따라잡았다. Debug 의 틱
  (clang -O0 82.6 ms, MSVC Debug 는 더 느림)이 틱 간격 33 ms 보다 길어 늘 4 틱 → 프레임 ≈ 틱 × 4.
- DirectSim: 창 = **Threaded** (Simulation 스레드가 월드 소유, 30 TPS × 속도 시각표, 0.25 초 넘게 밀리면 버림, 일시정지 ·
  한 틱 · 속도는 mutex + condition_variable), 헤드리스 · 테스트 = **Inline** (그대로). 시뮬레이션 Worker 를 DirectSim 이 소유
  (`--threads n`, 기본 코어 수 - 2 를 1 ~ 4, 헤드리스 0).
- SpriteExtraction: `capture`(Simulation 스레드, 틱마다 → 불변 WorldSnapshot — 직전 · 지금 위치, 크기, 회전, 스프라이트, 색,
  레이어, 배경) + `emit`(Main 스레드, 프레임마다 보간). 스냅숏은 shared_ptr<const> 로 내놓고 이전 버퍼를 다시 쓴다.
- 제목 줄: 시뮬레이션 "29.8/30 TPS · 틱 12.3 ms", Application "144 fps · 월드 · 추출 · 렌더 ms" (0.5 초 평균).
  끝날 때 프레임 · 틱 평균을 출력.
- 문서: ADR-0021 (ADR-0020 결정 5 · 6 대체), 01(상태), 06 9장, 13, 14 7.7, 15, 16, 17, MANUAL-QA 8A(fps 는 Release 로).

**왜**

- 느린 틱이 화면 · 카메라까지 막았다. 01 5장의 스레드 모델(Simulation 스레드, T1 · T2)대로 나누면 틱 비용과 무관하게 그린다.
- 헤드리스는 Inline 으로 남겨 CTest client_direct_sim_headless 의 틱 수(28)가 시간과 무관하게 고정된다.

**검증**

- Wine 11.19 + lavapipe(소프트웨어, 2코어), ecosystem_10k --vsync off 40 초: MinGW Debug **4 → 52 fps**(평균 69.8, 시뮬레이션
  9.3/30 TPS · 틱 105 ms 로 표시), Release 60 → 57 fps(평균 68.9, 29.8/30 TPS) — 2코어 소프트웨어 렌더라 Release 차이는 작다.
- 단위 테스트: Threaded(Worker 2) 진행 · 일시정지 · 한 틱 정확히 하나 · 같은 틱에서 Inline(Worker 0) 과 위치가 같다 (D5).
  TSan(linux-clang-tsan, clang 19) client · foundation 3회 경고 0. Linux clang · gcc Debug/RWD CTest, MinGW 경고 0.
- 헤드리스 끝 요약으로 틱 · 추출을 Debug/RWD 비교 (14-PERFORMANCE 7.7).

**남은 일**

- 사용자 PC: Release ecosystem_10k 의 fps · TPS · 렌더 ms, Debug 에서 카메라가 부드러운지 (MANUAL-QA 8A).
- `[계획]` Debug 의 틱 자체(MSVC 반복자 검사)는 그대로 느리다. Phase 10 에서 --direct-sim 이 LocalServerHost 로 바뀐다.

---

## 2026-10-07 — Phase 8A: 스프라이트 렌더러 · AssetManager · --direct-sim 관찰

**무엇을**

- Phase 8 을 셋으로 나눴다 (ADR-0020): 8A 에셋 · 스프라이트 · Renderer · --direct-sim → 8B 지형 · 디버그 패스 · 지표 → 8C ImGui.
- `render/renderer`: Camera2D(월드 y 위, 줌 = px/칸, 커서 기준 줌 · 끌기 · 맞춤), RenderWorld · SpriteDraw, 정렬 키
  [pass|layer|pipeline|material|depth], SpriteBatcher(컬링 → 안정 LSD 기수 정렬 → 아틀라스 · 상한별 묶음), Renderer
  (AssetManager.update → 배치 → 인스턴스를 업로드 링에 → Clear + WorldSpritePass, 묶음마다 draw(4, n)).
- `shaders/sprite.hlsl`: 정점 버퍼 없이 SV_VertexID 사각형 + 48 바이트 인스턴스 정점(위치 · 크기 · 회전 · 층 · uv · 색),
  카메라 push constant, Texture2DArray 아틀라스.
- `render/asset`: AssetManager(경로 정규화 id, JobSystem Worker 디코드, 요청 순 선반 패킹 + 1 텍셀 테두리, 프레임 예산 8 MB 의
  영역 업로드, 흰색 자리 · 마젠타 실패), ShelfPacker, MaterialLibrary(`assets/<팩>/materials.json`, 대체 색).
- `assets/ecosystem`: 풀 · 토끼 · 늑대 32² 자리 표시 그림(새로 그린 단순 도형)과 머티리얼 표.
- SandboxClient: ClientRenderer(ClearRenderer 를 바꿈 — 월드면 Renderer, 메뉴면 Clear + 7B 삼각형, AssetManager 소유),
  IWorldSession · DirectSim(`--direct-sim <시나리오>` — ScenarioRunner 30 TPS × ¼ ~ 8, 일시정지 · 한 틱, 따라잡기 4 틱),
  presentation/SpriteExtraction(render.sprite Opaque → 머티리얼 · 크기 · 레이어, saveId 캐시, 틱 사이 보간, depth = -y,
  월드 배경). Application: 세션이 있으면 InWorld 로, WASD · 휠 · 끌기 · Home · Space · . · = · -. 옵션 --seed · --content · --assets.
  SandboxClient 가 SandboxCore 를 링크한다 (01 의 계획대로).
- 테스트: sbx_render_tests 6 케이스(sprite · camera · order · batch_1k · assets · materials) + 기준 이미지 2장, SandboxTests
  render(카메라 · 키 · 배치 · 기수 = 기준 stable_sort · 패커 · id) · client(DirectSim · Extraction · 앱 InWorld · 옵션),
  CTest client_direct_sim_headless. 벤치 render.sprite_batch.
- 문서: 06(상태 · 7 · 8 · 9 · 14장), 11(material 의미), 13, 14(7.6), 15, 16(Phase 8 표), 17, MANUAL-QA 8A, README, ADR-0020.

**왜**

- Phase 8 의 "화면에 보인다" 를 지형 · UI 보다 먼저 스프라이트로 — 시뮬레이션(Phase 5)을 처음으로 눈으로 본다.
- 인스턴스 정점 버퍼 · 고정 아틀라스 · push constant 카메라는 프레임마다 바인드 그룹을 다시 쓰지 않는 가장 단순한 조합 (ADR-0020).

**검증**

- Linux: clang · gcc Debug/RelWithDebInfo, clang ASan — 경고 0, CTest 전부 통과 (37~41개). include 경계 0.
- MinGW + Wine 11.19 + lavapipe: sbx_render_tests 22 케이스 · 313 단언 통과, 누수 0. 7A · 7B 기준 이미지 7장은 그대로.
  sprite · batch_1k 는 확대해 검토. SandboxClient --direct-sim ecosystem_small 스크린숏(풀 · 토끼 · 늑대, 휠 줌 ×2.3 이 커서 기준),
  ecosystem_10k 12,877 스프라이트 · Draw 1 · 54 fps (소프트웨어 렌더 — 하드웨어 수치 아님).
- 벤치(RWD, 2코어 컨테이너): 50k 배치 4.85 ms(비교 정렬) → 2.19 ms(기수 정렬), 10k 0.44 ms. 14-PERFORMANCE 7.6.
- 고친 것: batch_1k 는 처음에 회전한 3.6 px 스프라이트 + 날카로운 텍스처라 구현마다 다를 수 있어 픽셀 정렬 · 무회전 · 완만한
  텍스처로 바꿨다. DirectSim 따라잡기에서 fmod 나머지가 다음 프레임에 틱을 하나 더 넣던 것을 밀린 시간 통째 버리기로.

**남은 일**

- 사용자 PC: ctest -L render (sprite · batch_1k 를 WARP 로), MANUAL-QA 8A (실제 GPU 에서 ecosystem_10k fps).
- `[계획]` 8B TerrainPass(호수 · 지형) · Grid · Selection · DebugDraw · 타임스탬프, 8C ImGui, 아틀라스 해제 · 밉 · 핫 리로드.

---

## 2026-10-07 — 규칙: 수정 작업은 커밋 · push 까지

**무엇을**

- AGENTS.md 5장을 "커밋 · Push 규칙" 으로 바꿨다: 파일을 고친 작업은 커밋과 push 까지 해야 완료. 순서(빌드 · 테스트 → 범위 확인 →
  경로 지정 add → 작업 단위별 커밋 → push → 해시 · 결과 보고), 무관한 변경 제외, 강제 push 금지, git 을 직접 못 쓰는 환경의
  bundle 전달 절차. 2장 절차에 11번 Commit & Push. `.gitignore` 에 `*.bundle`.

**왜**

- 사용자 요청. Phase 2~7B 가 초기 커밋 뒤로 한 번도 커밋되지 않은 채 쌓여 있었다 (2026-10-07 에 항목별 8 커밋으로 정리).

**검증**

- 문서 변경만. 원격 세션은 `.git` 에 쓸 수 없어(기기 브리지 제한) 이 커밋도 bundle 로 전달했다.

---

## 2026-10-07 — Phase 7B: 셰이더 빌드 · 파이프라인 · BindGroup · Triangle · Texture

**무엇을**

- 셰이더 빌드 (ADR-0019): `cmake/SbxShaders.cmake` — DXC 를 NuGet `Microsoft.Direct3D.DXC` 1.9.2609.5 (SHA256 고정)로 처음 구성 때
  `.cache/dxc/` 에 내려받는다 (Windows). `SBX_BUILD_SHADERS`(기본 Windows ON) · `SBX_DXC` · `sbx_add_shader()`. HLSL → DXIL + SPIR-V →
  `tools/shader/sbx_shader_gen.py`(자체 SPIR-V 리플렉션, Python 표준 라이브러리만) → `<Pascal>Shader.{hpp,cpp}`(내장 바이트코드 ·
  리플렉션 표 · cbuffer/push 구조체 + offsetof/sizeof static_assert) + `.reflect.json`. 생성기가 DXIL 서명(dxil.dll)을 검사한다.
- 셰이더: `shaders/basic_color.hlsl`(정점 색 · Frame tint · push 변환), `basic_texture.hlsl`(Texture2D + Sampler, 그룹 2),
  `common/Common.hlsli`. 바인딩 규칙 register(xN, spaceG) — G = 그룹, N = 그룹 안 고유 binding, push = b0 space7.
- RHI: `ShaderTypes`(단계 · 바인딩 종류 · ShaderReflection · ShaderBytecode), Sampler · BindGroupLayout · BindGroup · GraphicsPipeline Desc,
  `layoutFromReflection`, `RangeAllocator`, `PipelineValidation`(레이아웃 · 파이프라인 ↔ 리플렉션 · 바인드 그룹 검사 — 그래픽 API 전에),
  ICommandList 의 setPipeline · setBindGroup · pushConstants · setVertexBuffer · setIndexBuffer · draw · drawIndexed, DeviceStats 의
  livePipelines · descriptorsUsed.
- D3D12 (`Dx12Pipeline.cpp`): shader-visible CBV/SRV/UAV 65,536 · Sampler 2,048 힙, 바인드 그룹 = 연속 구간(지연 반납), 루트 시그니처를
  레이아웃 모양 + push 크기로 만들어 캐시([루트 상수] → 슬롯마다 리소스 표 + 샘플러 표), PSO(CCW 앞면 · 블렌드 4종 · 깊이 끔).
  Garbage 를 일반화(객체 + 해제 콜백). `Com<T>` 변환 이동 생성자.
- 테스트: `tests/render/test_rhi_draw.cpp` 8케이스 — triangle · coord_convention(래스터 사분면 == 업로드 사분면, 비트 단위) ·
  culling_ccw · push/상수 버퍼 · drawIndexed · texture(nearest == 사분면, linear) · 검증 오류 5종 · 수명. 기준 이미지 4장 추가.
  끝의 누수 검사에 파이프라인 객체 · 디스크립터. 실패해도 이미 만든 것을 해제하도록 ColorRig 를 정리.
  SandboxTests `render`: `test_pipeline_validation.cpp`(RangeAllocator 참조 모델 · 검사 · 생성 셰이더). CTest `shader_gen_selftest`.
- SandboxClient: 셰이더가 내장된 빌드는 Clear 위에 도는 삼각형(가로세로비 보정 · 뒷면 컬링 — 규약 확인용).
- 시험 도구: `tools/wine/dxc.sh`(Windows dxc.exe 를 Wine 으로 — 교차 빌드), `run.sh` 에 `WINE` · Mono/Gecko 끔.
- 문서: 06(상태 · 3.2 · 3.4 · 5.1 · 6장 · 8.6 · 14장), 13, 15(옵션 · 함정), 16(7.4~7.6 ✅), 17, MANUAL-QA Phase 7B, README, ADR-0019.

**왜**

- ADR-0007 의 "도구 버전 고정 · SPIR-V 리플렉션 · 오프라인 컴파일" 을 사용자 PC 와 클라우드 세션 양쪽에서 같은 바이트로 하려면
  NuGet 이 맞았다 (GitHub 릴리스는 세션에서 막힘). 리플렉션에 필요한 정보는 SPIR-V 장식에 다 있어 SPIRV-Cross 빌드를 미뤘다.
- 바인딩 오류를 D3D12 런타임 오류(또는 조용한 잘못된 바인딩)가 아니라 원인 문장으로 받으려고 검사를 백엔드 앞에 뒀다.

**검증**

- Linux (셰이더 OFF): clang-19 · gcc-13 Debug/RelWithDebInfo, clang ASan — 빌드 경고 0, CTest 전부 통과 (36~40개).
- MinGW 교차 빌드 + `tools/wine/dxc.sh`(Wine 9 의 dxc.exe)로 셰이더까지 빌드 → **Wine 11.19 (WineHQ devel, dpkg -x)** d3d12(vkd3d) +
  lavapipe: `sbx_render_tests --fl11` 16케이스 · 222 단언 통과, 끝 "버퍼 0 · 텍스처 0 · 파이프라인 객체 0 · 디스크립터 0 · 경고 0 ·
  오류 6 (예상 6)". 7A 기준 이미지 3장은 바이트 단위로 그대로, 새 4장은 확대해 눈으로 검토. SandboxTests render 16케이스 통과.
  SandboxClient 실제 창: 삼각형이 돌고 가로세로비가 맞다 (스크린숏 2장).
- Ubuntu 의 Wine 9.0(vkd3d 1.10)은 PSO 생성에서 "Failed to compile shader, vkd3d result -4" — DXIL 미지원이라 시험 환경을 올렸다.
- GCC 13 -Warray-bounds 오진(RangeAllocator::allocate 의 vector::erase) → 반복자 루프로 바꿔 없앴다.

**남은 일**

- 사용자 PC: 첫 구성의 DXC 내려받기 · `ctest -L render`(WARP + Debug Layer 에서 기준 이미지 4장 재확인) · MANUAL-QA Phase 7B.
- `[계획]` Compute 파이프라인 · dispatch, 깊이 버퍼, PSO 캐시 (Phase 8). D3D12MA (Phase 8).

---

## 2026-10-06 — Phase 7A 후속: 사용자 PC 의 Debug Layer 성능 경고

**무엇을**

- 사용자 PC (MSVC, WARP, Debug Layer 켬): `render_tests_warp` 8 케이스는 모두 통과했지만 끝 검사에서 실패 — Debug Layer 경고 5건
  "ClearRenderTargetView: The application did not pass any clear value to resource creation". 정확성 문제가 아니라 성능 안내다.
- `Dx12Device` 의 InfoQueue 저장 필터에 `D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE` ·
  `CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE` 를 뺐다 (스왑체인 백버퍼는 최적 Clear 값을 가질 수 없어 매 프레임 났을 것).
- 사용 오류 테스트가 일부러 낸 `[error]` 로그 앞에 안내 MESSAGE.

**검증**

- 사용자 PC 관찰: FL 12_1 · Microsoft Basic Render Driver · Debug Layer 켬 — 기준 이미지 3장 · 수명 · 링 감기 모두 통과 (Debug Layer 메시지 외).
- Wine 재실행 통과 (Wine 에는 Debug Layer 가 없어 필터 자체는 사용자 PC 에서 다시 확인).

---

## 2026-10-06 — Phase 7A: RHI 골격 · D3D12 디바이스 · 스왑체인 Clear · 프레임 자원 · 기준 이미지 테스트

**무엇을**

- Phase 7 을 둘로 나눴다 (ADR-0018): 7A 디바이스·Clear·프레임 자원·기준 이미지 틀 → 7B 셰이더·파이프라인·Triangle·Texture.
- `SandboxRender` (Platform 만 의존, 항상 빌드):
  - `render/rhi`: `RhiTypes`(핸들·Format·MemoryType·Usage·ResourceState·Desc·Caps·DeviceStats), `RenderDevice`(IRenderDevice ·
    ICommandList · ICommandQueue · ISwapChain · createRenderDevice), `HandlePool`, `DeferredDestructionQueue`, `UploadRing`
  - `render/dx12`: `Dx12Device`(어댑터 선택 — WARP · 고성능 하드웨어 · 소프트웨어, FL 12_0 기본, committed 리소스, RTV 힙,
    업로드 링, 프레임 슬롯, 지연 해제, InfoQueue 경고·오류 집계, DRED, 디바이스 제거 원인), `Dx12CommandList`(배리어 · Clear ·
    MRT · 버퍼/텍스처 복사 + 배치 검증 · PIX 마커), `Dx12SwapChain`(FLIP_DISCARD · tearing · resize · 최소화 skip), `Com<T>`
  - `render/asset/Image`(RGBA8 · PNG 읽기/쓰기 · 비교 · 차이 그림), stb 벤더링(`external/stb`, f58f558)
  - `render/stub` — 백엔드가 없는 OS 의 `createRenderDevice` = Unsupported
- RHI 추가 (06 3.2 초안 대비): beginFrame/endFrame/frameIndex/waitIdle, map, allocateUpload, copyTextureToBuffer, destroy 오버로드,
  alive · textureDesc · stats, lastSubmittedValue, DeviceDesc.allowFeatureLevel11 (시험 전용)
- `sbx_render_tests` (Windows, `tests/render`): clear · 두 색 첨부 · upload_quadrants · region_copy · 버퍼 왕복 + 링 감기 · 수명 ·
  사용 오류. 기준 이미지 3장. 끝에 누수 0 · Debug Layer 경고 0 · 예상한 오류 수. CTest `render_tests_warp`
- SandboxClient: `IFrameRenderer` 경계 + `ClearRenderer`(실제 시간으로 천천히 색이 바뀌는 Clear, 제목 줄에 fps), 옵션
  `--no-render --rhi-debug --rhi-gbv --rhi-warp --rhi-fl11 --vsync on|off --frames-in-flight 2|3`. 렌더러가 있으면 페이싱은 VSync
- `tools/wine/`: Wine(vkd3d) + lavapipe 실행 래퍼와 시험용 Vulkan 레이어
- 단위 테스트 스위트 `render` (모든 OS): 핸들 풀 · 지연 해제 · 업로드 링(참조 모델 2,000프레임) · 포맷 · PNG 왕복(한글 경로) · 비교.
  `client`: 가짜 렌더러 · 새 옵션

**왜**

- Phase 7 (16): 렌더러(8)가 올라설 RHI 와 첫 백엔드. 06 8.6 순서 "Window → Clear → Triangle → Texture" 의 Clear 까지.

**도중에 바꾼 것 / 발견한 것**

- MinGW 의 d3d12.h 는 d3d12sdklayers.h 를 포함하지 않는다 → 직접 include. D3D_FEATURE_LEVEL_12_2 도 없다 → 값으로.
- Wine 의 d3d12(vkd3d)가 lavapipe 에서 FL 11_0 도 거절: viewportSubPixelBits 가 0 → 시험용 Vulkan 레이어로 8. 그래도 타일드
  리소스가 없어 FL 12_0 은 안 된다 → allowFeatureLevel11 (sbx_render_tests --fl11, SandboxClient --rhi-fl11).
- 지연 해제를 "마지막 제출 값" 으로 기록하면 기록 중 파괴에서 사용 후 해제가 된다 → "다음 제출 값".
- waitIdle 이 업로드 링의 이번 프레임 구간까지 반납하면, 아직 제출 전인 업로드를 덮어쓸 수 있다 → 끝난 프레임만 반납.
- VSync 가 없는 Xvfb 에서 클라이언트가 1,500 fps 로 돌아 프레임 기반 시간의 색 변화가 깜빡였다 → 렌더러에 실제 시간을 넘긴다.
- stb 의 stbi_write_png_to_mem 은 공개 선언이 없다 → stbi_write_png_to_func. 파일 IO 는 foundation/io (Windows 한글 경로).

**검증**

```text
linux-clang(19.1.1): Debug 36/36 (bench_quick 포함), RelWithDebInfo 39/39 (balance 포함). linux-gcc(13.3): 35/35 · 38/38. 경고 0
linux-clang-asan Debug 35/35 (bench·balance 제외). doctest Linux 203 케이스 · Windows(Wine) 202 케이스
MinGW GCC 13 교차 빌드 → Wine 9 (d3d12 = vkd3d → lavapipe, Xvfb):
  sbx_render_tests --fl11: 8 케이스 통과, 누수 0, 해제 13, 경고 0 (Wine 에는 Debug Layer 가 없다 — 안내 후 계속 확인)
  SandboxClient --rhi-fl11: 스왑체인 640×400 BGRA8 · 버퍼 3, Clear 색이 시간에 따라 바뀜(스크린샷), 900×500 으로 크기 변경,
  제목 줄 "D3D12 … fps VSync 켬", Ctrl+Q 종료
  SandboxTests.exe 202 케이스 통과 (Wine 은 LANG=C.UTF-8 이어야 한글 경로를 만든다 — tools/wine/run.sh)
기준 이미지 3장의 픽셀 값을 스크립트로 확인 (clear (51,102,204) · 사분면 빨강/초록/파랑/흰색 · 노란 패치 위치)
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- 사용자 PC: MSVC 빌드, `ctest -L render` (WARP + Debug Layer — "그래픽 도구" 설치 시), qa/MANUAL-QA Phase 7A
  (실제 VSync · DPI · 최소화 · --rhi-debug 경고 0). Wine 은 Debug Layer · Microsoft 런타임 동작을 대신하지 못한다.
- Windows 골든 4개 재기록(5C) · Phase 6 수동 QA 는 그대로 남아 있다.
- [계획] 7B: DXC 셰이더 빌드(sbx_add_shader · 리플렉션 JSON · 헤더 생성), 루트 시그니처 · PSO · BindGroup, Triangle · Texture ·
  좌표 규약 기준 이미지. Phase 8: D3D12MA, Copy 큐, 업로드 예산, 타임스탬프.

---

## 2026-10-06 — Phase 6: Windows 플랫폼 (창 · 입력 · ActionMap · Null 오디오 · SandboxClient 빈 창)

**무엇을**

- `SandboxPlatform` (Foundation 만 의존, ActionMap 파싱에 nlohmann/json PRIVATE). 항상 빌드한다 — 입력·오디오 단위 테스트가 쓴다.
  - `platform/common`: `Key`(물리 위치 · 이름 표), `PlatformEvent`/`PlatformEventQueue`, `IWindow`/`WindowDesc`/`NativeWindowHandle`,
    `HeadlessWindow`(OS 창 없는 IWindow — inject), `InputSystem`(I1 setCapture/downstream · I3 · I4 · 눌린 순간 수정자),
    `ActionMap`(settings/input.json · 정확 수정자 · 충돌 · 병합 · toJson) + `ActionState`(가장자리), `Gamepad` 인터페이스(I5),
    `IAudioBackend`, `FramePacer`/`PreciseSleeper`, `attachConsole`
  - `platform/windows`: `Win32Window`(Per-Monitor DPI v2 · WM_DPICHANGED · Raw Input 캡처 · IME 컨텍스트 분리 · WM_CHAR 서로게이트 ·
    SetCapture · 더블클릭 · 휠 · 클립보드 · 커서 · Alt 메뉴/경고음 차단 · AltGr·Shift·PrintScreen 보정), `Win32Platform`
    (고해상도 대기 타이머 · 콘솔), `Win32KeyMap`(windows.h 없는 순수 표 — 모든 OS 에서 테스트)
  - `platform/audio/NullAudioBackend`, `platform/stub`(창이 없는 OS: createWindow = Unsupported)
- `SandboxClient` (`apps/client`): 앱 상태기계(Boot → MainMenu → Connecting → InWorld(Play|Edit) → Shutdown, 전이 표, 프레임 끝 적용),
  프레임 루프(이벤트 → 입력 → 액션 → 디버그 동작 → 오디오 → 제목 줄), 제목 줄 입력 모니터(수동 QA), 기본 바인딩 JSON 내장 +
  `--input`, `--headless --frames --fps --width --height --console --log-input`. Windows: GUI 서브시스템 + main, DPI 매니페스트
- `foundation/text/Utf8.hpp`. 기본 로그 싱크가 줄마다 stderr flush
- 옵션 `SBX_BUILD_CLIENT`(ON). CTest `unit_platform` · `unit_client` · `client_version` · `client_rejects_unknown_option` ·
  `client_headless_smoke`
- ADR-0017 (물리 키 · 텍스트 입력 기본 끔 · 정확 수정자 + 눌린 순간 수정자 · HeadlessWindow · 오디오 update(dt))

**왜**

- Phase 6 (16): 렌더러(7·8)가 그릴 창과, 에디터(12)가 쓸 입력 경로를 먼저 확정한다. 실제 창 없이도 앱 루프를 시험할 수 있어야
  CI 와 Linux 에서 클라이언트 코드가 회귀하지 않는다.

**도중에 바꾼 것 / 발견한 것**

- 07 초안의 "scancode 기준 바인딩" 은 OS 마다 값이 달라 설정 파일에 적을 수 없다 → Key 자체를 물리 위치로 정의 (ADR-0017).
- 같은 프레임에 연속 두 번 탭하면 ActionState 가 두 번째를 놓쳤다 (지난 프레임도 켜짐) → 바인딩 키가 새로 눌린 프레임은 pressed.
- Wine + xdotool 시험에서 `Ctrl+Q` 가 안 먹었다: 한 프레임 안에 Ctrl↓ Q↓ Ctrl↑ Q↑ 가 모두 와서 프레임 끝 수정자가 비어 있었다
  → 키가 눌린 순간의 수정자를 InputState 에 기록 (keyPressMods).
- 두 Shift 보정이 누르지 않은 쪽 KeyUp 까지 냈다 → 알린 Shift 만 뗀다.
- Wine 에서 강제 종료하면 로그가 사라졌다 (Windows CRT 가 파이프로 넘긴 stderr 를 버퍼링) → 로그 싱크 flush.
- 07 의 `IAudioBackend::update()` → `update(dtSeconds)` (Null 이 목소리 수명을 정하려면 시간이 필요).

**검증**

```text
linux-clang(19.1.1): Debug 35/35 (bench_quick 포함), RelWithDebInfo 38/38 (balance 포함). linux-gcc(13.3): 34/34 · 37/37. 경고 0
linux-clang-asan Debug 34/34 (bench·balance 제외), doctest 193 케이스
MinGW GCC 13 (posix) 교차 빌드 → Wine 9: SandboxTests.exe 193 케이스 전부 통과, SandboxClient --version · --headless 통과
Wine 9 + Xvfb + xdotool 로 실제 Win32Window: 키(스캔 코드 → Key)·Shift·F2 글자 "hi"·클릭·더블클릭 ×2·휠·가운데 버튼·
  F3 캡처 중 Raw Input Δ(30, 10)·Esc 해제·창 크기 변경 → Resized 1000×700·제목 줄(한국어)·Ctrl+Q 종료
include lint 0건 (195 파일), clang-format 적용
```

**미검증 / 남은 일**

- MSVC 빌드와 qa/MANUAL-QA Phase 6 체크리스트 (사용자 PC): DPI 모니터 이동, 한글 IME 조합, Alt+Tab, 최소화 CPU, 클립보드.
  Wine 은 Per-Monitor DPI 를 지원하지 않는다 ("DPI 를 켜지 못했습니다" 경고는 Wine 에서만 나와야 한다).
- Windows 골든 4개 재기록(Phase 5C 에서 남은 일)은 그대로 남아 있다.
- [계획] 사용자 설정 경로(platformPaths) 자동 로드, IME 캐럿 위치·현지 배열 키 이름(Phase 8), 게임패드 구현, miniaudio.
- 다음: Phase 7 — DirectX 12 RHI (Device · SwapChain Clear → Triangle → Texture, 셰이더 빌드, WARP 기준 이미지).

---

## 2026-10-06 — Phase 5C: 리플레이(D3) · 생태계 시나리오 · 밸런스 · 10k 성능 (Phase 5 완료)

**무엇을**

- 리플레이 (D3, ADR-0016): `core/command/CommandJson`(명령 ↔ JSON, 엔티티 참조 변환), `SimulationWorld::setCommandObserver`
  (수락된 명령을 적용 전에 netId → saveId 로 바꿔 넘김), `core/replay/Replay`(ReplayRecorder · JSON Lines 파일 · playReplay).
  명령의 위치 = tick() 호출 번호(call). `sbx_sim_check --replay-roundtrip [--replay-dir]` · `--replay <file>`
- 시나리오 `ecosystem_small`(64×64, 300) · `ecosystem_survival`(256×256, 1,500, 18,000틱) · `ecosystem_10k`(192×192, 10,000) —
  풀·토끼·늑대 70/25/5 %, 호수·흙, **보충 없음**
- 밸런스: `life.reproduce` 밀도 제한(crowdRadius · crowdMax), 초식 행동(배고프고 풀이 보일 때만 찾기, 대상을 잃으면 배회),
  육식(energy < 80 일 때 사냥, 15초 휴식), 수치 조정 (11 7장 — 초안 대비 표)
- 성능: 색인 항목에 core.tags 사본, flee 가 통행 가능한 목표를 고름, 감지·충돌 읽기 패스를 Worker 와 나눔(`parallelFor`,
  `SystemContext.jobs`)
- `sbx_bench --only <접두사> --threads n` + `sim.ecosystem` (평균·p95·p99·최대·System 별·종별 수)
- `sbx_sim_check --require-prefabs` (끝에 0 이면 실패) + 종별 개체 수 출력
- kSimVersion 3 → 4, 골든 4개 (ecosystem_small 신규)
- 테스트: 명령 JSON 왕복, 리플레이 왕복(일시정지 편집·Step 포함)·변조 검출·시작 월드 불일치·파일 오류(잘림·magic·CRLF),
  옵션. CTest `det_replay_*` 4개, `det_ecosystem_small(_golden)`, `balance_ecosystem_survival_{1,2,3}`(최적화 빌드만)

**왜**

- Phase 5 완료 조건(16): 시드 3개 18,000틱 공존, D1~D5, 10k 평균 < 10 ms · p99 < 25 ms. 리플레이는 Phase 9 오토세이브·
  Phase 12 에디터 명령 로그의 바탕이다.

**도중에 바꾼 것 / 발견한 것**

- 리플레이 위치를 (tick, editSequence) 대신 tick() 호출 번호로 — 미리 큐에 넣으면 실행 틱이 편집 단계 몫까지 가져간다.
  대상은 saveId 로 — 시작 세이브를 로드한 월드는 netId 를 다시 매긴다 (ADR-0016).
- 첫 survival 실행이 10분 넘게 걸렸다: 풀이 지도를 다 덮을 때까지 지수적으로 늘었다 → 밀도 제한. 초안 수치에서는 토끼가
  "풀이 안 보이면 멈춘다" 행동 때문에 굶었고, 늑대가 토끼를 다 잡은 뒤 굶었다. 풀 crowdMax 6 은 시드 3 에서 붕괴, 8 에서 공존.
- 512×512 의 "10k" 는 3,000틱 뒤 27,000 개체가 된다 → 평형 밀도에서 10,000 근처를 유지하는 192×192 로. 비용은 밀도를 따른다.
- 물 위 flee 목표가 A* 를 확장 상한까지 헤매게 해 경로 비용 4.5 ms/틱 → 통행 가능한 점을 고르게 해 0.08 ms.
- survival 맵을 문서 초안 512×512 에서 256×256 으로 (시드 3개를 수 분 안에 — 13 6장).
- Windows 골든 항목(simVersion 3)은 더 이상 맞지 않아 지웠다 (남겨 두면 SKIP 이 아니라 "갱신 필요" 실패) → 다시 기록 필요.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3): Debug 30/30, RelWithDebInfo 33/33 (balance 3개 포함), -Werror
linux-clang-asan Debug 29/29 (bench 제외), doctest 168 케이스
D1·D2·D5 ecosystem_small --repeat 2 --save-at 450 --threads 0,1,8 일치
D3  det_replay_{random_walk_1k, world_save_load, eco_lifecycle, ecosystem_small} 일치. 리플레이 한 줄을 고치면
    다음 해시 레코드에서 "불일치 (D3) … 버그 (같은 simVersion)" (수동 + 단위 테스트)
D4  골든 4개 kSimVersion 4 — Clang 19 · GCC 13 같은 해시
공존 ecosystem_survival 18,000틱 시드 1·2·3: 풀 7,218/7,868/7,535 · 토끼 1,005/981/987 · 늑대 262/251/255
성능 sim.ecosystem (ecosystem_10k → 14,710 개체): Worker 1 평균 7.83 · p99 13.56 ms, Worker 0 평균 10.48 · p99 15.95 ms
MinGW GCC 13 (posix) 교차 빌드 성공, include lint 0건 (161 파일), clang-format 적용
```

**미검증 / 남은 일**

- Windows: 골든 4개 다시 기록 (`--record-golden`, ecosystem_small 포함), ctest 전체 (balance 는 Release/RelWithDebInfo 구성에서).
- Worker 0 에서 ecosystem_10k 평균 10.5 ms (기준 10 ms 근소 초과) — 감지 TagMask 색인(S5)·반경 조정 후보 (14 7.5).
- 경로 캐시·HPA*, 바이너리 리플레이(Phase 9), 에디터 명령 로그 시나리오 edit_session(Phase 12).
- 다음: Phase 6 — Windows 플랫폼 (IWindow · Win32Window · InputSystem · SandboxClient 빈 창).

---

## 2026-10-05 — Phase 5B: 감지 · 행동 · 상호작용 · 경로 · 충돌 · JobSystem

**무엇을**

- 컴포넌트 5개 (카탈로그 18개): `core.movement`(maxSpeed·accel·arriveRadius·goal·hasGoal), `core.collider`(radius·layer·mask),
  `ai.sensor`(radius), `ai.behavior`(graph·state·enteredTick·target(saveId)·blackboard[8]),
  `ai.path`(start·goal·state·submittedTick·partial·waypoints ≤16·cursor). 틱 캐시 필드는 reflect 하지 않는다 (ADR-0015)
- 콘텐츠 link 단계: action 번호·action 별 Rule 색인(priority 내림, 정의 순)·Behavior 감지 질의(sensed·seek·flee 의 TagExpr,
  그래프당 ≤ 8, 넘으면 V7). `ContentDatabase::actions/findAction/rulesForAction`
- System 7개 + Movement 조향 (`core/systems/`, 등록 `DefaultSystems.cpp`):
  Stage 5 PathCollect · 6 Sensor · 7 Behavior(FSM, 노드 어휘 전부) · 8 PathRequest · 9 Movement(조향) · 10 Interaction ·
  11 ResolveIntents(Effect op 전부: field.add/set · destroy · spawn · tag.add/remove · event) · 16 Collision
- 경로 (`core/path/`): `PathGridSnapshot`(불변, 바뀐 청크만 다시 복사), `tileLineClear`(Amanatides–Woo, 코너 컷 금지),
  A*(8방향, 정수 비용, (f, h, 순번) 전순서, 확장 상한·부분 경로, 직선 검사로 경유점 줄이기), `PathfindingService`
  (Job 계약 T→T+1, 로드 후 Submitted 재제출)
- `foundation/job/JobSystem`(Worker 풀, 0 = 그 자리 실행, `JobGroup::wait`). `SimulationWorld::setJobSystem`
- `SimulationWorld`: `SaveIndex`(saveId → EntityId), `IntentBuffer`, `PathfindingService`, `resolveSave`,
  `SystemContext` 에 catalog · saves · intents · paths. `RandomService::Purpose::Interaction`
- `sbx_sim_check --threads n[,n..]` (D5) — 실행마다 자기 JobSystem, 나란히 비교. CTest `det_eco_lifecycle` 에 `--threads 0,1,8`
- eco 팩: rabbit·wolf 에 이동·충돌·감지·행동·경로. 늑대 targetInRange 0.9 · Rule range 1.0 (충돌 반지름 합 0.7 보다 넉넉히)
- kSimVersion 2 → 3, 골든 3개 재기록
- 테스트: JobSystem, 경로(스냅샷 불변·직선 검사·우회·부분 경로·서비스 D5), 콘텐츠 link·V7, AI(사냥→먹기·이벤트·파괴,
  배타 Rule, "*" 자기 전이·stateTime, 경로 T→T+1 우회·Worker 0/4 동일, **진행 중 Job 저장→로드 D2**, 조향, 충돌)

**왜**

- 생태계가 "먹고 먹히는" 상태가 되어야 5C 의 밸런스·리플레이·성능 측정이 의미가 있다. 경로 Job 은 첫 Worker 사용처라
  D5 검사를 여기서 세운다 (Phase 3 에서 이월).

**도중에 바꾼 것 / 발견한 것**

- 문서의 ai.path_request + ai.path_follow → ai.path 하나 (구조 변경 없이 상태 전이). ai.sensor 의 mask → 그래프 질의.
  대상 참조는 saveId. 진행 중 Job 은 저장하지 않고 로드 때 같은 입력으로 다시 제출 (ADR-0015).
- 처음엔 Behavior 그래프 캐시를 포인터로 두었다가 02 C2(컴포넌트에 포인터 금지)에 맞춰 인덱스 + id 확인으로 바꿨다.
- "*" → 지금 상태 자신으로의 전이는 건너뛴다 — 그렇지 않으면 eco.herbivore 의 flee 가 매 틱 다시 들어가 stateTime 이 0.
- 중심 거리 사거리와 충돌 분리: 분리가 반지름 합까지 밀어내므로 targetInRange·range 를 그보다 크게 둬야 먹을 수 있다.
- 클라우드 컨테이너가 2코어·8 GB 인데 -j16 으로 빌드하다 cc1plus 가 OOM 으로 죽었다 → -j2. 빌드 도중 헤더를 고쳐
  서로 다른 레이아웃으로 컴파일된 목적 파일이 섞인 바이너리가 D2 테스트를 깨뜨린 일이 있었다 (다시 빌드하니 통과) —
  검증 결과는 소스를 고정한 뒤 처음부터 다시 돌린 것만 적는다.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 24/24 (소스를 고정한 뒤 처음부터 다시 돌린 결과)
linux-clang-asan Debug → 23/23 (bench 제외)
doctest 162 케이스
D1·D2·D5 eco_lifecycle --repeat 2 --save-at 900 --threads 0,1,8 일치. 저장 시점 101~110·211·333·517·700 에서도 D2 일치
D2  단위 테스트: 경로 Job 이 진행 중(Submitted)일 때 저장 → 로드(해시 검증 일치) → 400틱 매 틱 같은 해시
D4  골든 3개 kSimVersion 3 으로 재기록 — Clang 19 · GCC 13 같은 해시
D5  sbx_sim_check --threads 0,1,8 · 경로 서비스 단위 테스트(Worker 0·1·4) 같은 결과
실행 SandboxServer --scenario eco_lifecycle --ticks 300 정상 종료 (overruns 0)
성능 (RelWithDebInfo) eco_lifecycle 평균 tick 0.11 ms (~500 개체), random_walk_10k 2.0 ms (Phase 4: 1.9) — 14-PERFORMANCE 7.4
MinGW GCC 13 (posix) 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건 (152 파일), clang-format 적용 (사용자가 고친 test_error.cpp 는 그대로 둠)
```

**후속 (2026-10-06, 사용자 PC 피드백)**

- 사용자가 `cmake-build-debug\bin` 에서 `--record-golden tests/golden/...` 을 실행 → 상대 경로라 파일이 없었고, 도구는 골든 파일이
  없으면 기본 시나리오(random_walk_1k)를 돌린 뒤 쓰기 단계에서야 실패했다 (세 명령 모두 random_walk_1k 로 돌았다).
  → 골든 파일이 없으면 시뮬레이션 전에 절대 경로·현재 폴더와 함께 거절, 새 골든은 `--scenario` 필수, 쓸 폴더가 없으면 거절.
- Windows 콘솔(cp949)에서 한국어 출력이 깨졌다 → `foundation/io/Console`(SetConsoleOutputCP 65001, windows.h 없이 선언 하나)을
  sbx_sim_check · SandboxServer 시작 때 부른다.
- MSVC 19.44 에서 Phase 5B 코드가 빌드·실행됐다 (사용자 출력: random_walk_1k simVersion 3 최종 해시 0x3c9524ea60fc4ef7 —
  Linux 골든과 같다). 검증: linux-clang Debug 24/24, MinGW 교차 빌드, include lint 0건.
- 사용자가 저장소 루트에서 골든 3개에 Windows-x86_64-MSVC-19.44 항목을 기록했다 (simVersion 3). 세 파일 모두 Clang 19 · GCC 13 ·
  MSVC 19.44 의 **모든 체크포인트 해시가 같다** — 생태계(감지·FSM·A*·충돌, sqrt 포함)까지 세 컴파일러 일치. 콘솔 한국어 정상 출력.

**미검증 / 남은 일**

- Windows ctest 전체 (Phase 5A·5B 코드). 사용자 PC: 골든 3개에 `--record-golden` (simVersion 3 — 지금 Windows 항목 없음).
- 밸런스: 1800틱 eco_lifecycle 에서 토끼가 늘고 풀이 줄어드는 쪽 — 7장 수치 조정·18,000틱 3시드 공존은 5C.
- 경로 캐시·HPA*·그룹 경로, 감지 TagMask(S5), 10k 에서 경로 예산·감지 비용 측정 (5C 의 sbx_bench sim.ecosystem).
- 다음: 5C — Replay 기록·재생(D3), ecosystem_small/ecosystem_survival, sbx_bench sim.ecosystem 10k, 밸런스.

---

## 2026-10-05 — Phase 5A: 콘텐츠 · 생명 주기

**무엇을**

- Phase 5 를 셋으로 나눴다: **5A** 콘텐츠·생명 주기 → 5B 행동·상호작용·경로 → 5C 리플레이·밸런스·성능 (16-ROADMAP).
- 필드 타입 `FixedString<N>` / `ContentId`(47 바이트) — JSON 문자열, 해시는 길이+바이트. `Hint::PrefabRef`·`BehaviorRef`.
  `FieldAccess` 방문자로 `ComponentInfo::fields`·`findField`·`getNumber`·`applyNumber`(FieldMeta 범위로 자르기) — Rule 조건·효과용
- 콘텐츠 (`core/content/`): `TagSet`(256 비트)·`TagExpr{all, any, none}`, `ContentModel`(Prefab·Rule·BehaviorGraph·PackInfo),
  `ContentLoader` — pack.json 탐색·requires 위상 정렬·태그·머티리얼·Prefab·Rule·Behavior 파싱, 검증기 V1~V7(모든 문제를 모아
  "<파일>:<JSON 포인터>" 로 보고), contentHash(CRLF 정규화, presentation/ 제외). 내장 "core" 팩
- 컴포넌트: `core.tags`, `core.prefab`(출처), `life.energy`·`life.health`·`life.growth`·`life.reproduce` (카탈로그 13개)
- 생성: `CreateEntity{prefab}` 명령과 System 의 `SpawnQueue` 가 같은 `SimulationWorld::instantiate` 를 쓴다
  (Prefab + 덮어쓰기 키 단위 병합, 위치 우선순위, 검증, 경계). SpawnQueue 는 (parent saveId, seq) 정렬, uniqueTile
- `LifecycleSystem` 재작성: 에너지 소모·체력·나이·수명 → 사망(saveId 순, `Died` 이벤트 + `DeathCause`), 성장, 번식
  (cooldown·minEnergy·minStage·chance, `stream(Spawn, saveId)`, AdjacentEmptyTile/Nearby)
- 팩 `content/ecosystem` ("eco"): 태그 7, 머티리얼 3, Prefab grass·rabbit·wolf(11 7장 수치 + Opaque render.sprite),
  Rule 3(먹기), Behavior 2(초식·육식) — Rule·Behavior 실행은 5B
- 세이브: world.json 에 content{packs, contentHash}·tags[]·opaqueComponents[], 로드 때 태그 비트 재매핑·콘텐츠 차이 경고,
  로드 해시 검증 조건을 Opaque 이름 집합 비교로 (ADR-0014)
- 시나리오 `eco_lifecycle` (128×128, 풀 400·토끼 60·늑대 10, 300틱마다 토끼 +10, 1800틱), `IScenario::requiredPacks`
- 도구: `sbx_sim_check --validate-content <팩…>`·`--content-root`, `SandboxServer --content-root`, 기본 콘텐츠 경로
  `SBX_DEFAULT_CONTENT_DIR`
- 테스트: 콘텐츠 로더(정상 팩 + 규칙별 틀린 팩), 필드 접근, Prefab 생성·생명 주기·번식·SpawnQueue 순서, 세이브 태그 재매핑.
  CTest `det_eco_lifecycle`, `det_eco_lifecycle_golden`, `content_eco_validate`, `unit_content`. 골든 `eco_lifecycle`

**왜**

- 5B 의 Behavior·Rule 실행은 콘텐츠 모델과 Prefab 생성이 먼저 있어야 시험할 수 있다. 9개 티켓을 한 번에 검증하면
  결정론 위반의 원인을 좁히기 어렵다 — 행동 없이 생명 주기만 도는 시나리오로 먼저 D1·D2·D4 를 고정한다.

**도중에 바꾼 것 / 발견한 것**

- Prefab 생성은 ECB 가 아닌 SpawnQueue (ADR-0014 1번) — 5B 에서 System 을 Job 으로 나눠도 saveId 할당 순서가 그대로다.
- Phase 4 의 opaqueEntities("Opaque 가 있으면 해시 비교 건너뜀")로는 render.sprite 를 가진 생태계 세이브가 검증되지 않는다
  → 저장·로드 프로세스의 Opaque 이름 집합이 같으면 비교 (ADR-0014 3번, ADR-0013 2번 일부 대체).
- nlohmann::json `*j.items().begin()` 을 구조적 바인딩으로 잡으면 임시 프록시에 매달린다 — Debug 는 통과, RelWithDebInfo 에서
  invalid_iterator 예외. `j.begin()` 반복자로 고쳤다. 최적화 빌드 테스트가 잡았다.
- `requires` 는 C++20 키워드라 `PackInfo::dependencies` 로. 11 5장 예시의 flags "water" → 구현은 "Water"(대문자) — 11 0장에 기록.
- 기존 골든(random_walk_1k, world_save_load)이 변하지 않았다 → kSimVersion 은 2 그대로.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 24/24
linux-clang-asan Debug → 23/23 (bench 제외)
doctest 144 케이스
콘텐츠 content/ecosystem 오류·경고 0 (content_eco_validate)
D1  eco_lifecycle --repeat 2 일치
D2  eco_lifecycle 900틱 저장 → 1800틱까지 동일, 로드 해시 검증 일치 (태그 표 · Opaque render.sprite 를 지나는 왕복)
D4  eco_lifecycle 골든 신규 — Clang 19 · GCC 13 기록, 두 컴파일러 같은 해시. 기존 골든 2개 변화 없음
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건, clang-format 적용
```

**사용자 PC 결과 반영 (Windows · MSVC 19.44 · CLion Debug, Phase 4 코드 — 2026-10-05 19:30 의 CTest 로그)**

- 19개 중 17개 통과. D1·D2(폴더 교체 rename 포함) 통과 — Windows 실행 첫 확인. `random_walk_1k`·`world_save_load` 의
  최종 해시(600틱)가 Linux 골든과 같다 (Clang·GCC·MSVC 세 컴파일러 일치). 골든 2개는 Windows 항목이 없어 SKIP.
- 실패 1 `arch_include_lint_selftest`: 린터가 위반 메시지의 '—' 를 cp949 콘솔에 쓰다 UnicodeEncodeError.
  → 스크립트가 stdout/stderr 를 UTF-8 로 고정. 사용자가 `-X utf8` 을 붙인 수정은 유지하되, 그 수정이 들어간 쪽이
  `arch_include_lint`(저장소 전체 검사)여서 이 테스트가 픽스처를 검사하게 바뀌어 있었다 → `--root` 를 저장소로 되돌리고
  `-X utf8` 은 두 테스트 모두에 적용.
- 실패 2 `arch_link_boundary_selftest`: 중첩 CMake 구성의 링크가 LNK1104(kernel32.lib) — IDE 가 vcvars 환경을 CTest 에
  주지 않는다. → MSVC 면 구성 시점의 LIB·INCLUDE·LIBPATH 를 테스트 환경으로 넘기고, 컴파일러 자체를 못 쓰면 SKIP.
- 사용자가 `test_error.cpp` 에 `<ostream>`·`<string_view>` 를 넣은 MSVC 컴파일 수정은 유지. 같은 문제가 다른 테스트에서
  나지 않게 `sbx_doctest` 에 `DOCTEST_CONFIG_USE_STD_HEADERS` (doctest 가 std 를 앞선언하지 않고 헤더를 포함).
- IDE 가 표를 정렬한 ADR 0003~0011·external/README 는 기기 쪽을 그대로 가져왔다.

**미검증 / 남은 일**

- Phase 5A 코드의 MSVC 빌드 (MinGW 크로스만 확인). 사용자 PC: 골든 3개(`random_walk_1k`·`world_save_load`·`eco_lifecycle`)에
  `--record-golden` 으로 Windows 항목 기록 — 그 전까지 `det_*_golden` 은 SKIP 이 정상.
- Prefab 상속(`inherits`), enum 필드의 이름 표기, actions.json(Phase 12) — 11 0장 `[계획]`.
- SpawnQueue 상한 (5C 성능 측정에서), EntityRef 2-pass (5B 의 ai.* 대상 참조와 함께).
- 다음: 5B — SensorSystem·BehaviorSystem(FSM)·InteractionSystem·Rule 효과, 조향 이동·충돌, PathfindingService + JobSystem(D5).

---

## 2026-10-05 — Phase 4: World · Save/Load

**무엇을**

- 월드 (`core/world/`): `ChunkCoord`(floorDiv, y 우선 비교), 지형 레이어(SoA: material·flags·moveCost·height),
  `Chunk`(terrainRevision, revision 키 해시 캐시), `WorldGrid`(고정 경계, 기본 16×16 청크, paint, 점 포함·자르기)
- 콘텐츠 (`core/content/`): 최소 `ContentDatabase` — `TerrainMaterial` (id 정렬 인덱스, 검증, 내장 4종, contentHash)
- 명령: `PaintTerrain{materialId, cells | center·shape·radius}` — id 로 지정, 경계·상한 검증, 바뀐 청크만 revision +1
- 경계: Create/Move/AddComponent·ChangeComponent(core.transform)가 월드 밖이면 OutOfRange, Movement 는 경계로 자른다
- 공간 색인: 월드 경계 안 격자 + 카운팅 정렬, `forEachInChunk`·`countInChunk`
- WorldHash: 지형 추가 (머티리얼은 id 의 stableId). `kSimVersion` 1 → 2, 골든 재기록
- 세이브 (`core/persist/`): `saveWorld`/`loadWorld` — world.json · entities.jsonl · chunks/*.chunk, 머티리얼 재매핑,
  Opaque 보존, 컴포넌트 `MigrationRegistry`, 로드 시 worldHash 검증 (ADR-0013). `foundation/io/FileIo`
- 시나리오 `world_save_load`(128×128, 300 개체, 10틱마다 지형 칠하기). `ScenarioRunner` 가 로드한 월드로 이어 돌리기
- `sbx_sim_check --save-at T [--save-dir]` (D2: 저장→로드한 월드를 나란히 끝까지 비교)
- 테스트: 월드 격자·지형·콘텐츠·PaintTerrain·경계, 청크 파일, 마이그레이션, D2 왕복, 재매핑, Opaque, 손상 세이브, CRLF,
  커밋된 샘플 세이브 `tests/data/saves/v1_sample`. CTest `det_world_save_load`, `det_random_walk_save_paused`,
  `det_world_save_load_golden`, `unit_persist`
- 벤치: `save.world` 50k

**왜**

- 세이브 왕복(D2)이 되어야 리플레이(Phase 5)의 시작점과 에디터의 저장(Phase 12)이 성립한다. 청크는 저장·전송·경로·렌더의
  공통 단위라 (05 2.1) 먼저 고정 경계로 단순하게 세운다.

**도중에 바꾼 것 / 발견한 것**

- 원자성: 파일별 rename(초안) 대신 폴더 교체 — 세트 불일치와 지난 세이브의 청크 파일 잔재를 함께 막는다 (ADR-0013).
- 로드가 world.json 의 worldHash 와 스스로 비교한다. 첫 구현에서 "Opaque 를 가진 프로세스가 저장 → 그 컴포넌트를 아는
  프로세스가 로드" 경로가 해시 불일치로 실패하는 것을 테스트가 잡았다 → world.json 에 opaqueEntities 를 남기고 그때는 비교를 건너뛴다.
- 지형 해시를 머티리얼 인덱스가 아닌 id 로 — 콘텐츠 순서가 바뀐 재매핑 로드에서도 검증이 성립한다.
- Chunk 에 entityCount·state 를 두지 않았다 (공간 색인이 O(16) 으로 계산, state 는 스트리밍과 함께) — 05 0장.
- 공간 색인 카운팅 정렬로 50k 재구성 6.8 → 1.1 ms, 1k tick 0.29 → 0.18 ms, 10k 3.8 → 1.9 ms.
- 사용자 PC 의 IDE 가 docs 의 마크다운 표를 열 맞춤으로 바꿔 둔 것을 확인 (내용 변화 없음). 이번 문서 수정도 같은 형식으로 맞췄다.
  00·06·10·design 문서는 표 정렬 외의 차이도 있어 기기 쪽을 기준으로 가져왔다 (Phase 3 커밋 때).

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 20/20
linux-clang-asan Debug → 19/19 (bench 제외)
doctest 126 케이스
D1  det_random_walk_repeat · det_world_save_load(--repeat 2) 일치
D2  world_save_load 250틱 저장 → 600틱까지 동일, random_walk_1k 일시정지 300틱 저장 → 420틱까지 동일, 로드 해시 검증 일치
D4  random_walk_1k(simVersion 2), world_save_load — Clang 19 · GCC 13 기록, 두 컴파일러 같은 해시
성능 (RelWithDebInfo, 클라우드 컨테이너) 50k 저장 188 ms / 로드 504 ms (기준 저장 < 1 s)
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror) 성공 (실행은 못 함)
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- Windows 실행·MSVC. Windows 에서 폴더 교체 rename 이 실제로 동작하는지 (탐색기·백신이 폴더를 잡는 경우) — 사용자 PC 확인 필요.
- 사용자 PC: `det_*_golden` 2개는 SKIP 이 정상 → 두 골든 파일에 `--record-golden` (simVersion 2 로 바뀌었으므로 Phase 3 에서
  기록한 Windows 항목이 있다면 그것도 다시).
- 증분 저장·zstd·Worker 직렬화 (Phase 9 오토세이브), EntityRef 2-pass (Phase 5), 스키마 마이그레이션 체인.
- 다음: Phase 5 Ecosystem (ContentDatabase 완성, Sensor·Behavior·Interaction·Rule, Pathfinding Job, Replay, D3·D5).

---

## 2026-10-05 — Phase 3: 헤드리스 시뮬레이션 · 결정론 하네스

**무엇을**

- 시뮬레이션 (`core/simulation/`): `SimulationClock`(일시정지·Step·속도·editSequence), `ISystem`/`SystemContext`/`Stage`,
  `SystemScheduler`(Stage → 등록 순서, System 별 ECB + 구조 잠금, `ISystemProfiler` 훅), `SimulationWorld`
  (Stage 0·2·3·4·5~16·17, 일시정지 편집 단계), `EventStream`, `kSimVersion = 1`
- 명령 (`core/command/`): `SimCommand` variant (Create/Delete/Move/Add/Remove/ChangeComponent, Pause/Resume/Step/Speed),
  `CommandQueue`((executeTick, issuer, sequence) 정렬), `CommandResult`. 적용기는 명령마다 원자적
- 정체성: `persist.persistence{saveId}`, `net.identity{netId}` — SimulationWorld 가 생성 직후 부여, 명령으로 변경 불가
- 난수 (`core/random/`): `CounterRng`(SplitMix64 카운터, 동결), `RandomService.stream(Purpose, saveId)`
- 공간 색인 (`core/world/SpatialIndex`): 정렬 배열 격자, radius/AABB/nearest, 결과 순서 (셀 y, 셀 x, saveId)
- System (`core/systems/`): RandomWalk(Stage 7, 진단용), Movement(9), Lifecycle(15: `core.lifetime`, `life.age`)
- WorldHash (`core/replay/`): saveId 순서, Hashed 컴포넌트 stableId 순서, 구조 오류는 Error. 엔티티별 해시·JSON 진단
- 시나리오 (`core/scenarios/`): `IScenario`, `ScenarioRunner`, `random_walk_1k`/`random_walk_10k` (명령만으로 월드를 바꾼다)
- 도구: `sbx_sim_check` (`tools/sim_check/`, `SBX_BUILD_TOOLS`) — `--repeat`(D1), `--golden`/`--record-golden`(D4),
  불일치 시 엔티티→컴포넌트→필드 진단, `--inject-divergence`(하네스 자체 시험)
- 서버: `SandboxServer --scenario <name> [--ticks] [--seed] [--realtime]` 헤드리스 실행 (최종 해시·시간 출력)
- 골든: `tests/golden/random_walk_1k.json` (Linux-x86_64-Clang-19, Linux-x86_64-GNU-13) — 툴체인별 (ADR-0012)
- 빌드 정보: `kToolchainKey` 추가
- 벤치: `sim.random_walk` 1k/10k, `sim.spatial` 50k
- 테스트: RNG(동결 값), 명령 큐, 공간 색인 단위 + 속성(전수 탐색 대조), SimulationWorld(명령 경로·원자성·일시정지·
  Lifecycle·ECB 정체성·스케줄 순서·해시 성질), 시나리오(D1 축소판), sim_check·서버 옵션.
  CTest `det_random_walk_repeat`, `det_random_walk_golden`(항목 없으면 SKIP), `det_harness_selftest`, `server_scenario_smoke`

**왜**

- 렌더러보다 먼저 헤드리스로 "정답"을 고정한다 (16-ROADMAP 순서의 이유). 이후 모든 Phase 의 회귀는 골든 해시와
  `--repeat` 로 잡는다. 시나리오가 사람(에디터)과 같은 명령 경로를 쓰므로 Phase 12 의 편집 경로가 지금부터 시험된다.

**도중에 바꾼 것 / 발견한 것**

- 사용자 Windows 실행 결과 (CLion, MinGW GCC 15.2, `cmake-build-debug`): 8개 중 7개 통과 (property 3.9 s 포함).
  `arch_link_boundary_selftest` 가 "unable to find a build program corresponding to Ninja" 로 실패 — CLion 은 Ninja 를
  PATH 가 아니라 CMAKE_MAKE_PROGRAM 으로 넘기는데 중첩 cmake 호출이 그것을 받지 못했다. → `CMAKE_MAKE_PROGRAM`·
  `CMAKE_TOOLCHAIN_FILE` 을 전달하도록 수정, PATH 에 ninja 가 없는 환경을 만들어 재현·검증. Python3 은 여전히 못 찾음
  (arch_include_lint 미등록 — 15-BUILD 9장에 안내 추가).
- WorldHash 엔티티 순서를 문서 초안의 `EntityId.index` 에서 **saveId** 로 바꿨다 (로드 후 index 가 달라져도 같은 해시 — D2).
- 골든을 툴체인별로 기록 (ADR-0012). 시나리오(입력) 변경은 simVersion 대상이 아니라는 예외를 04 6장에 명시.
- JSON 읽기 버그 (Phase 2 코드): 코드에서 만든 `Json(100)` 은 signed 정수로 저장되는데 부호 없는 필드가 이를 거절했다.
  파일에서 읽은 JSON 은 양수를 unsigned 로 만들기 때문에 Phase 2 테스트가 못 잡았다. 음수만 거절하도록 수정.
- SpatialIndex: AABB 가 int32 전 범위를 덮으면 폭×높이(2^32 × 2^32)가 u64 를 넘쳐 0 이 되고, 격자 순회가 사실상 무한 루프.
  단위 테스트("huge query")가 잡았다 → 폭·높이를 먼저 셀 수와 비교.
- `editSequence` 가 실행 중 적용된 PauseSimulation 에도 오르던 것 → "이번 tick() 이 편집 단계인가" 로 판정.
- System 시간 측정은 Core 밖에서 주입 (`ISystemProfiler`) — Core 에 `<chrono>` 를 들이지 않는다 (04 4.5).
- `--threads`(D5)는 JobSystem 이 없어 Unsupported 로 거절하고 Phase 5 로 옮겼다 (16-ROADMAP 5.9). 단일 스레드에서 D5 를
  "통과"로 표시하지 않기 위해서다.
- 측정: SpatialIndex 50k 재구성 6.8 ms — 05-WORLD 4.1 의 1 ms 조건 초과. Phase 4 카운팅 정렬로 대응 (05-WORLD 4.4).

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) × Debug · RelWithDebInfo, -Werror → 각 ctest 16/16
linux-clang-asan Debug → 15/15 (bench 제외)
doctest 111 케이스 / 단언 약 152만 (대부분 속성 테스트)
D1  sbx_sim_check --repeat 2 --ticks 600: 일치.  하네스 자체 시험: tick 60 에서 saveId 1 의 core.transform.position 까지 좁힘
D4  Clang 19 · GCC 13 항목 기록, 두 컴파일러 × 두 빌드 설정 모두 같은 해시 (0x0a5ae056a0fb401b @600)
성능 (RelWithDebInfo, 클라우드 컨테이너) random_walk 1k 평균 0.29 ms (기준 < 2 ms), 10k 3.8 ms
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror): Server·Tests·sim_check·bench 빌드 성공 (실행은 못 함)
arch_link_boundary_selftest: PATH 에 ninja 없이 CMAKE_MAKE_PROGRAM 만 준 구성에서 통과
include lint 0건, clang-format 적용
```

**미검증 / 남은 일**

- Windows 실행 (MSVC·MinGW): 사용자 PC 에서 `det_random_walk_golden` 은 SKIP 이 정상 →
  `sbx_sim_check --record-golden tests/golden/random_walk_1k.json` 으로 기록해 커밋. MSVC 빌드 자체도 아직 미검증.
- D5(`--threads`), p99 tick 측정, 공간 색인 카운팅 정렬, 명령 권한·속도 제한 — Phase 4·5·9~12.
- 다음: Phase 4 World (Chunk · Terrain · Save/Load, D2).

---

## 2026-10-05 — Phase 2: Core ECS

**무엇을**

- Foundation: `SmallVector<T,N>`, `Vec2`/`Vec2i`
- ECS (`core/ecs/`): `EntityId`, `EntityManager`(LIFO·퇴역 슬롯), `ComponentPool`(4096 페이지 sparse set, P1~P3 `validate()`),
  `SBX_COMPONENT`/`ComponentTraits`(이름 규칙 컴파일 타임 검사, Persistent→Hashed 자동, `NotHashed`),
  `Registry`(구조 잠금, emplaceOrReplace, 리소스, `forEachEntityByIndex`, `poolsByStableId`, `adoptPool`),
  `View<Read/Write/Exclude>`(최소 풀 드라이버, Write 시 changed 틱), `EntityCommandBuffer`(PendingEntity, 상쇄),
  `ComponentCatalog`(명시 등록 — ADR-0011)
- 직렬화: 리플렉션 계약(`reflect(V&, T&)`), `JsonWriter/Reader`(기본값 유지·타입/범위/모르는 키 오류), `HashVisitor`(H1)
- 컴포넌트: `core.transform`, `core.velocity` + `registerCoreComponents`
- 외부: nlohmann/json v3.12.0 vendored (`external/nlohmann_json`), `external/CMakeLists.txt` 로 타깃화
- 테스트: 단위(ecs 스위트 신설), 속성 테스트(Registry vs `std::map` 참조 모델, 시드 3개), 결정성(같은 시퀀스 → 같은 id·순서)
- 벤치: `sbx_bench`(`SBX_BUILD_BENCH`), `ecs.iterate`·`ecs.churn`, 기준선 `bench/baselines/cloud-sandbox-container.json`
- CMake: MinGW 로 구성하면 경고 (사용자 CLion 이 기본 MinGW 프로필로 구성한 것을 확인했기 때문)

**왜**

- Phase 3(시뮬레이션 루프)이 올라설 저장소·순회·구조 변경·해시의 기반. 해시·세이브가 리플렉션 하나를 공유해
  "해시에 빠진 필드"(RTS H3) 문제를 구조로 막는다.

**도중에 바꾼 것 / 발견한 것**

- 구조 잠금 중 emplace 는 Release 에서도 중단 (참조 반환이라 무시 불가) — 02-ECS 0장에 반영.
- ECB emplace = 있으면 교체. Registry 에 emplaceOrReplace 추가.
- 벤치 첫 실행에서 4-컴포넌트 시나리오가 빈 view 를 재고 있었다(0.13 ns) → 방문 수 검사를 넣어 무의미한 측정이면 중단하게 함.
- GCC 13 -O2 `-Warray-bounds` 오진 (SmallVector relocate 의 memmove) → 명시적 루프로 우회.
- doctest `REQUIRE_MESSAGE` 에 삼항식을 넣으면 컴파일 실패 → `INFO` + `REQUIRE` 로.

**검증**

```text
linux-clang(19.1.1) · linux-gcc(13.3) · linux-clang-asan  ×  Debug · RelWithDebInfo, -Werror
→ 6개 조합 각 ctest 11/11 (doctest 76 케이스, 단언 약 147만 — 대부분 속성 테스트)
MinGW GCC 13 크로스 컴파일 (Windows 대상, -Werror): Server·Tests·bench 빌드 성공 (실행은 못 함)
include lint 58 파일 0건, 링크 경계 3 타깃 통과
벤치 (RelWithDebInfo, 클라우드 컨테이너): 10k×4 컴포넌트 8.2 ns/entity, 50k×4 10.9 ns/entity
사용자 PC(D:\Game\Sandbox\cmake-build-debug) 확인: CLion 기본 MinGW GCC 15.2 로 Phase 1 SandboxServer 빌드 성공,
  테스트는 빌드·실행되지 않았고 Python 을 찾지 못함 (arch 테스트 미등록)
```

**미검증 / 남은 일**

- MSVC 빌드 (특히 `SBX_COMPONENT` 의 전역 네임스페이스 특수화, View 의 템플릿 람다) — 사용자 PC 또는 CI.
- EnTT 기준선 비교, Opaque 컴포넌트, Binary/Bit/ImGui Visitor, `removedLog<T>` — 해당 Phase.
- 다음: Phase 3 헤드리스 시뮬레이션.

---

## 2026-10-05 — Phase 1: 저장소 골격

**무엇을**

- 빌드: 루트 `CMakeLists.txt`, `CMakePresets.json`(7 configure 프리셋, Ninja Multi-Config), `cmake/Sbx*.cmake`
  - `sbx_warnings`(MSVC `/utf-8` 포함), `sbx_strict_conversions`, `sbx_simulation_flags`(Core 가 PUBLIC 전파)
  - 표준 라이브러리 기능 확인(`std::expected`/`format`/`source_location`) — 안 되면 원인 한 줄로 중단
  - `CMAKE_CXX_SCAN_FOR_MODULES OFF`, 실행 파일은 `build/<preset>/bin/<Config>/`
  - `BuildInfo.hpp` 생성(버전·커밋·컴파일러)
- 경계 강제: `sbx_check_link_boundaries()`(전이적 링크 그래프), `tools/check_includes.py`(모듈 방향·금지 헤더·SFML/OpenGL·`#import`),
  각각 자체 시험(`arch_link_boundary_selftest`, `arch_include_lint_selftest` + `tests/arch/fixtures`)
- Foundation: `Types`, `Error`/`Expected`, `SBX_ASSERT`/`SBX_VERIFY`(처리기 교체 가능), `log`, `Fnv1a64`(동결 상수 테스트), `Handle<Tag>`
- Core: `core/simulation/SimConstants`(30 TPS, 고정 dt, 스냅샷 주기 약수 규칙)
- SandboxServer: `--help`/`--version`/`--log-level`, 종료 코드 0/1/2
- SandboxTests: doctest v2.5.0 vendored, 29 케이스
- 저장소 설정: `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`(적용 완료), `.clang-tidy`
- CI: `.github/workflows/ci.yml` (Windows MSVC, Linux clang-19/gcc-13/asan, macOS 15)

**왜**

- Phase 0 에서 정한 경계(ADR-0005)를 문서가 아니라 빌드·테스트 실패로 강제하기 위해. RTS 에서 "core 는 SFML 을 모른다"가
  문서에만 있고 코드에서 깨져 있던 것을 반복하지 않는다.

**도중에 바꾼 것**

- Clang 18 + libstdc++ 13 은 `std::expected`를 비활성화한다 → Linux 최소 요구를 Clang 19 로 올리고 구성 단계 검사 추가.
- CMake 3.28 + C++23 은 모듈 스캔을 켜서 clang-scan-deps 가 없으면 전부 실패한다 → 모듈 스캔 끔.
- `toString(enum)` 자유 함수가 doctest 와 ADL 충돌 → `errorCodeName`/`levelName`으로 개명, 규칙을 12-CODING-STANDARDS 에 추가.
- `SmallVector`는 첫 사용처인 Phase 2 로 이동.

**검증** (Linux 클라우드 환경)

```text
linux-clang (clang 19.1.1) · linux-gcc (gcc 13.3) · linux-clang-asan   ×   Debug · RelWithDebInfo, -Werror
→ 6개 조합 모두 ctest 8/8 통과 (doctest 29 케이스 / 68 단언)
SandboxServer --version → "SandboxServer 0.1.0 (commit unknown, Linux, Clang 19.1.1) / tick 30 Hz, dt 0.0333333 s"
인자 없음 → 종료 코드 1, 잘못된 인자 → 2
boundary selftest: 중간 INTERFACE 타깃을 거친 Server→Platform 링크를 구성 단계에서 FATAL_ERROR
include lint: 실제 트리 22 파일 0건, 픽스처 8건 정확히 검출
clang-18 지정 시 구성 단계에서 원인 메시지와 함께 중단됨을 확인
```

**미검증 / 남은 일**

- Windows MSVC, macOS 빌드는 이 세션에서 실행할 수 없었다 → 사용자 PC 에서 `cmake --preset windows-msvc` 1회, CI 첫 실행.
- `.github/workflows/ci.yml` 은 원격 쓰기가 금지된 경로라 채팅으로 전달했다 → 사용자가 직접 배치.
- `git init`·원격 저장소 생성은 사용자 작업 (16-ROADMAP 1.1b). 커밋 해시가 생기면 BuildInfo 에 반영된다(재구성 시).
- LICENSE (열린 질문 Q1).
- 다음: Phase 2 Core ECS.

---

## 2026-10-05 — Phase 0: 문서 세트 작성

**무엇을**

- 저장소 루트: `README.md`, `AGENTS.md`, `DEVELOPMENT_LOG.md`
- `docs/00~17` 규범·설계·절차 문서, `docs/adr/0001~0010`
- `docs/design/SANDBOX_ARCHITECTURE.md`: RTS 저장소 분석과 전체 설계의 원본 (Phase 0 입력, 이후 수정하지 않음)

**왜**

- 새 프로젝트를 코드 없이 시작하면서, 구현 전에 경계·불변식·순서를 먼저 고정하기 위해.
- 원본 설계서는 한 파일 2,000줄이라 작업 중 참조하기 어렵다. 주제별 규범 문서로 나누고
  구현에 필요한 세부(API 모양, 와이어 포맷, 스키마, 체크리스트)를 보강했다.

**검증**

- 문서 간 상대 링크 전수 확인, 코드 블록 짝 확인.
- 원본 설계서의 30개 장과 결정 항목이 분할 문서 어디로 갔는지 `docs/README.md` 4장에 대응표로 남김.

**남은 일**

- Phase 1: 저장소 골격(CMake 프리셋, Foundation/Core/Tests 타깃, CI, include 린트). [16-ROADMAP](docs/16-ROADMAP.md)
- 부록의 열린 질문(저장소 이름·라이선스 등)은 결정되는 대로 ADR로.
