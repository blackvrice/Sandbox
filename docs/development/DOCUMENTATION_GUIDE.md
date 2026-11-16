# 문서 가이드 — 어디에 무엇을 쓰고, 언제 고치나

> **규범 문서.** 문서의 층 · 상태 표시 · 작성 규칙 · 기능 개발과 버전 완료 때의 갱신 절차를 정한다.
> [AGENTS](../../AGENTS.md) 6장 · [docs/README](../README.md) 3장의 규칙을 바꾸지 않는다 — 그 위에 "보는 문서" 의 관리 방법을 더한다.

| 항목 | 내용 |
|---|---|
| 문서 목적 | 문서 체계를 계속 맞게 유지하는 방법 |
| 문서 상태 | 확정 (2026-11-12) |
| 관련 도구 | `tools/check_docs.py` (CTest `docs_consistency`) · `tools/gen_roadmap.py` (CTest `docs_roadmap_generated`) |

## 목차

1. [문서의 층 — 정본과 보는 문서](#1-문서의-층--정본과-보는-문서)
2. [상태 표시](#2-상태-표시)
3. [기능을 개발할 때](#3-기능을-개발할-때)
4. [생성 문서 (gen_roadmap)](#4-생성-문서-gen_roadmap)
5. [버전을 끝낼 때](#5-버전을-끝낼-때)
6. [작성 규칙](#6-작성-규칙)
7. [모듈 문서의 틀](#7-모듈-문서의-틀)
8. [자동 검사](#8-자동-검사)

## 1. 문서의 층 — 정본과 보는 문서

같은 사실을 두 곳에서 손으로 관리하지 않는다. 사실마다 정본이 하나 있고, 나머지는 링크하거나 생성한다.

| 층 | 문서 | 성격 | 누가 고치나 |
|---|---|---|---|
| 입구 | [README](../../README.md) · [docs/README](../README.md) | 안내 · 목차 | 구조가 바뀔 때 손으로 |
| 보는 문서 | [overview/CURRENT_STATUS](../overview/CURRENT_STATUS.md) · [FEATURE_MATRIX](../overview/FEATURE_MATRIX.md) · [MODULES](../overview/MODULES.md) · [roadmap/MASTER_ROADMAP](../roadmap/MASTER_ROADMAP.md) · [roadmap/versions/](../roadmap/versions/) | 정본을 사람이 읽기 좋게 묶은 것 | 숫자 · 상태 표는 `gen_roadmap.py`, 나머지는 손으로 (3 · 5장의 시점에) |
| 계획의 정본 | [roadmap/task-registry](../roadmap/task-registry.md) (태스크 · 상태 · 목표 버전) · [releases](../roadmap/releases.md) (버전 목표 · 완료 기준) · [release-gates](../roadmap/release-gates.md) · [versioning](../roadmap/versioning.md) | 계획 · 규범 | 손으로 — 상태를 바꾸는 유일한 곳 |
| 설계의 정본 | `docs/00` ~ `29` (번호 문서) | 규범 · 설계 | 구조를 바꾸는 커밋에서 |
| 결정 | [adr/](../adr/README.md) | 이력 — 고치지 않는다 (Superseded 표시만) | 새 결정은 새 ADR |
| 이력 | [DEVELOPMENT_LOG](../../DEVELOPMENT_LOG.md) · [releases/](../releases/) · [audit/](../audit/) · `design/` · `platform/` | 그때의 기록 — 고치지 않는다 | 새 항목만 더한다 |
| 절차 | [qa/MANUAL-QA](../qa/MANUAL-QA.md) · [QA-EVIDENCE](../qa/QA-EVIDENCE.md) | 실기 확인과 그 증거 | 창으로 확인한 사람이 |

정본이 어디인지 헷갈릴 때:

| 알고 싶은 것 | 정본 |
|---|---|
| 태스크가 끝났나 · 어느 버전인가 | task-registry |
| 버전의 목표 · 완료 기준 | releases |
| 기능이 어떻게 동작해야 하나 | 번호 문서 (규범) |
| 왜 그렇게 정했나 | ADR |
| 화면에서 실제로 확인했나 | MANUAL-QA · QA-EVIDENCE |
| 무엇을 했고 어떻게 검증했나 | DEVELOPMENT_LOG |

낡은 문서를 지우지 않는다. 시점이 지난 분석 (audit · 21 의 1장 등) 은 머리에 "언제의 것이고 지금은 어디를 보라" 를 적고 남긴다.
더 쓰지 않는 문서는 `docs/archive/` 로 옮기고 (지금은 비어 있어 폴더가 없다) 옮긴 링크를 같은 커밋에서 고친다.

## 2. 상태 표시

보는 문서는 다섯 표시만 쓴다. 등록부의 상태 낱말 (Done · Planned …) 은 그대로 두고 아래 표로 옮긴다.

| 표시 | 뜻 | 등록부 상태 |
|---|---|---|
| ✅ 완료 | 완료 기준을 채웠고 자동 시험 또는 기록된 수동 QA 가 있다 | Done |
| 🟡 진행 중 | 일부 구현 · 완료 기준 미충족 | Planned · Proposed 인데 버전 문서의 세부 체크리스트에 `[x]` 가 하나 이상 |
| ⬜ 미착수 | 구현이 확인되지 않음 | Planned · Proposed |
| 🔴 차단 | 선행 작업 · 문제 · 정의 없음으로 진행 불가 | Missing, 또는 끝나지 않은 선행 (버전 문서의 "차단 여부" 칸) |
| ⚠️ 확인 필요 | 실제 상태를 충분히 검증하지 못함 | Review |

완료 판정:

- **코드가 있다는 것만으로 ✅ 를 붙이지 않는다.** 등록부의 "완료 기준" 칸에 적힌 시험이 등록돼 통과해야 Done 이다.
- 창 · GPU · 입력으로만 확인되는 것은 MANUAL-QA 줄을 만들고, 확인하면 `[x]` + QA-EVIDENCE 한 줄. 헤드리스 통과를 실기 통과로 적지 않는다.
- 실행하지 않은 시험을 통과로 적지 않는다. 모르면 ⚠️ 확인 필요로 둔다.
- 버전은 태스크가 모두 Done 이고 릴리스 노트가 있을 때만 ✅ 다.

기능 매트릭스는 여기에 분류를 더 쓴다 (🧪 시험 전용 · 📐 설계만 · ❓ 계획 불명확 · 범위 외 — [FEATURE_MATRIX](../overview/FEATURE_MATRIX.md) 1장).

## 3. 기능을 개발할 때

1. **작업 ID 를 확인한다.** [task-registry](../roadmap/task-registry.md) 에서 태스크를 찾는다. 없으면 등록부에 행을 먼저 더하고
   [releases](../roadmap/releases.md) 의 그 버전 "태스크" 줄에 넣는다 (ID 는 다시 쓰지 않는다).
2. **세부 칸을 확인한다.** 그 버전 문서 (`roadmap/versions/vX.Y.md`) 의 "세부 체크리스트" 에 그 태스크의 칸이 있으면 그것을 따른다.
   없으면 태스크 카드에서 옮겨 적는다 (카드에 없는 일을 만들지 않는다).
3. **구현 · 시험 · 실행 확인** — AGENTS 2장의 절차.
4. **같은 커밋에서 문서를 고친다.**
   - 끝낸 세부 칸을 `[x]` 로.
   - 설계 · API 가 바뀌었으면 해당 번호 문서, 결정이 있으면 새 ADR, 폴더 · 타깃이 바뀌었으면 17-SOURCE-MAP, 호환 번호가 바뀌었으면 versioning 5장.
   - 창 확인이 필요하면 MANUAL-QA 에 줄을 더한다.
   - DEVELOPMENT_LOG 에 "무엇을 · 왜 · 검증 · 남은 일".
5. **실제로 검증한 뒤 완료를 판단한다.** 완료 기준의 시험이 통과하면 등록부의 상태를 Done 으로, 완료 기준 칸에 시험 이름 · ADR 을 적는다.
6. **보는 문서를 다시 만든다.** `python tools/gen_roadmap.py --root .` — 버전 문서 · MASTER_ROADMAP · CURRENT_STATUS 의 숫자가 따라온다.
7. **기능의 분류가 바뀌었으면** [FEATURE_MATRIX](../overview/FEATURE_MATRIX.md) 의 그 줄과, 모듈 상태가 바뀌었으면
   [CURRENT_STATUS](../overview/CURRENT_STATUS.md) 3장 · [MODULES](../overview/MODULES.md) 의 그 절을 고친다 ("마지막 검증일" 도).

## 4. 생성 문서 (gen_roadmap)

`tools/gen_roadmap.py` 는 정본을 읽어 보는 문서의 **생성 구간** (`<!-- GENERATED:BEGIN … -->` ~ `<!-- GENERATED:END -->`) 만 다시 쓴다.
구간 밖의 손으로 쓴 글은 건드리지 않는다.

| 읽는 것 | 쓰는 것 |
|---|---|
| task-registry (태스크 · 상태 · 목표 · 선행 · 완료 기준) | `roadmap/versions/vX.Y.md` — 작업 요약 표 · 체크리스트 · 미완료 표 · 버전 완료 조건 |
| releases (버전 제목 · 목표 · 완료 기준 원문) | `roadmap/MASTER_ROADMAP.md` — 버전 표 · 알파 · GA 까지 남은 작업 |
| MANUAL-QA (`[ ] (v0.N …)` · `[x] (v0.N …)` 줄 수) | `overview/CURRENT_STATUS.md` — 요약 표 |
| `docs/releases/vX.Y.0.md` 의 존재 · CMake `project VERSION` | 버전의 ✅ 여부 · 제품 버전 |

```bash
python tools/gen_roadmap.py --root .
```

```bash
python tools/gen_roadmap.py --root . --check
```

- `--check` 는 다시 쓸 것이 있으면 실패한다 — CTest `docs_roadmap_generated` 가 돌린다. 등록부를 고치고 생성을 잊으면 여기서 잡힌다.
- 생성 구간을 손으로 고치지 않는다 (다음 생성 때 사라진다). 틀렸으면 정본을 고친다.
- **세부 체크리스트** (버전 문서의 생성 구간 아래, `### <태스크 ID> …` 절의 `- [ ]` · `- [x]`) 는 손으로 관리한다. 도구가 등록부와 맞춰 본다:
  한 칸이라도 `[x]` 면 그 태스크가 🟡, 등록부가 Done 인데 `[ ]` 가 남았거나 모두 `[x]` 인데 등록부가 Done 이 아니면 `--check` 실패.
- 진행률의 뜻 · 집계 단위는 [MASTER_ROADMAP](../roadmap/MASTER_ROADMAP.md) 6장.

## 5. 버전을 끝낼 때

1. **완료 조건을 검사한다.** 버전 문서의 "버전 완료 조건" 과 [release-gates](../roadmap/release-gates.md) 2장의 그 버전 행 — 여섯 칸
   (기능 · 데이터 · 안정성 · 보안 · 성능 · 호환) 을 실행한 명령 · 결과로 판정한다.
2. **미완료 작업을 확인한다.** 버전 문서의 "미완료 작업" 표가 비어야 한다. MANUAL-QA 의 그 버전 줄도 본다.
3. **이관할 작업을 적는다.** 못 끝낸 태스크는 등록부의 목표 칸을 다음 버전으로 고치고 (원래 칸은 그대로), releases 의 "태스크" 줄을 옮기고,
   이유를 ADR 또는 DEVELOPMENT_LOG 에 남긴다. 미완료 태스크를 남긴 채 버전이 끝났다고 하지 않는다.
4. **완료 기록을 남긴다.** `docs/releases/vX.Y.0.md` (들어간 것 · 기준 판정 · 호환 번호 · 미룬 것), CMake `project VERSION`, versioning 5장의 제품 버전.
5. **전체 로드맵을 갱신한다.** `gen_roadmap.py` 를 돌리고, [roadmap/README](../roadmap/README.md) 1장 · [18-PLATFORM](../18-PLATFORM.md) 0장의
   "지금 · 다음", 루트 README 의 상태 표, CURRENT_STATUS 의 손으로 쓴 줄 (요약 · 차단 요인 · 검증 기록) 을 고친다.

## 6. 작성 규칙

| 규칙 | 내용 |
|---|---|
| 제목 | 문서 맨 위에 `#` 제목 하나. 긴 문서 (대략 150 줄 이상) 는 목차를 둔다 |
| 요약 먼저 | 머리에 문서의 성격 · 상태 · 정본을 적고, 세부는 뒤에 |
| 메타데이터 | 중요한 문서는 표로 — 문서 목적 · 대상 버전 · 문서 상태 (초안 / 검토 중 / 확정 / 보관) · 마지막 검증일 · 관련 모듈. 필요한 것만 |
| 표 | 비교할 수 있는 정보는 표로. 한 칸에 문단을 넣지 않는다 |
| 체크리스트 | 진행 여부는 `- [ ]` · `- [x]`. 표의 상태와 어긋나지 않게 — 그래서 태스크 표는 생성한다 |
| 번호 목록 | 절차 · 실행 순서 |
| 코드 블록 | 언어를 적는다 (`cpp` · `ts` · `bash` · `powershell` · `json` · `text` · `mermaid`) |
| 링크 | 실제 파일을 가리키는 상대 경로. 파일을 옮기면 가리키던 링크를 같은 커밋에서 고친다 (`docs_consistency` 가 검사) |
| 그림 | 구조 · 흐름이 복잡하면 Mermaid. 그림 하나에 모든 시스템을 넣지 않는다. 코드 (CMake · 소스) 와 대조해 그린다 |
| 언어 | 한국어. 코드 식별자 · API · 클래스 · 파일 이름은 원문 그대로 |
| 버전 표기 | `v0.10` (식별자 — 소수가 아니다. v0.10 은 v0.9 다음). 제품 버전은 `0.10.0` |
| 이름 | 한 기능을 한 이름으로 — 처음 쓰는 이름은 [00-OVERVIEW](../00-OVERVIEW.md) 의 용어집에 |
| 수치 | 출처 (측정 · 추정 · 데이터 파일) 를 함께 |
| 미구현 | 규범 · 설계 문서 안에서는 `[계획]` 또는 `[계획 vX.Y]`. 구현하면 그 표시를 지운다 |
| ID | 태스크 `M1-T1` · `RT-06-1` · `ST-06-1` · `PS-12-1` · `RH-22-1` · `NG-01` · `UI-1` (등록부), 기능 `F-101` (FEATURE_MATRIX), 감사 발견 `G-01` (audit), ADR `ADR-0001`. 다시 쓰지 않는다 |
| 길이 | 한 문서가 한 주제를 넘으면 나눈다 |

## 7. 모듈 문서의 틀

새 모듈 문서를 쓰거나 기존 문서를 크게 고칠 때 이 순서를 따른다. 지금의 번호 문서를 이 틀로 한꺼번에 다시 쓰지는 않는다 —
내용이 규범이라 옮기다 뜻이 바뀌는 위험이 크다. 모듈의 한 화면 요약은 [overview/MODULES](../overview/MODULES.md) 가 이 틀로 준다.

1. 개요 — 역할과 목적
2. 현재 상태 — 구현된 것 · 안 된 것 (머리의 "상태" 줄)
3. 주요 기능
4. 아키텍처 — 구성 요소와 책임
5. 데이터 흐름
6. API — 주요 인터페이스
7. 제약 사항 — 알려진 문제 · 설계 제한
8. 향후 계획 — 태스크 ID 로 (버전을 직접 적지 않고 등록부를 가리킨다)
9. 관련 문서 — 설계 · 로드맵 · 시험 · ADR

## 8. 자동 검사

지금 도는 것:

| 검사 | 도구 · CTest | 잡는 것 |
|---|---|---|
| 링크 | `check_docs.py` L — `docs_consistency` | 깨진 상대 링크 (docs/ · README · AGENTS) |
| ADR 참조 | `check_docs.py` A | 없는 ADR 번호 |
| 태스크 ID | `check_docs.py` T · R | 계획 문서의 ID 가 등록부에 없음, 끝나지 않은 태스크가 releases 의 한 버전에만 없음 |
| 목표 버전 · 선행 | `check_docs.py` V · D | 버전 목록에 없는 목표, 선행이 더 뒤 버전 |
| 상태 | `check_docs.py` S | Done 인 태스크가 계획 문서에 `[계획]` 으로 남음 |
| 보는 문서 = 정본 | `gen_roadmap.py --check` — `docs_roadmap_generated` | 생성 구간이 등록부와 다름, 세부 체크리스트가 등록부와 어긋남 |
| 태스크 ID 중복 | (등록부가 표 하나라 눈으로) | — |

더하면 좋은 것 (제안 — 아직 없다. 필요해질 때 만든다):

| 제안 | 이유 | 방법 |
|---|---|---|
| 호환 번호 대조 | versioning 5장의 "지금" 값이 소스와 어긋난 적이 있다 ([감사](../audit/DOCS-AUDIT-2026-11-12.md) G-01) | `check_docs.py` 에 소스의 `kProtocolVersion` 등과 표의 숫자를 비교하는 검사 하나 |
| 등록부의 시험 이름 대조 | 완료 기준 칸의 `test_*` · CTest 이름이 실제로 있는지 | `tests/` 파일 이름 · `ctest -N` 과 비교 |
| Markdown 형식 | 표 열 수 · 닫지 않은 코드 블록 | markdownlint 를 CI 에 — 기존 문서가 넓은 표를 많이 써 규칙을 골라야 한다 |
| 문서 안 앵커 (`#절`) | 지금은 파일만 검사하고 절 이름은 보지 않는다 | `check_docs.py` L 확장 |
