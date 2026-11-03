# ADR-0097. 패키지 · 데이터 무결성 — 빌드 스냅숏, Asset Group, 에셋 id 충돌 = 오류, 경로 정책, 시뮬레이션 해시 · 표시 digest 분리, Export Profile

- 상태: **Accepted** (설계 — 구현은 v0.6 태스크) · 날짜: 2026-11-03
- 관련: [audit/SECOND-PASS-GAP-ANALYSIS](../audit/SECOND-PASS-GAP-ANALYSIS.md) 4.1 · 4.2 · 4.8 · 4.11 ~ 4.13, [26-ASSET-PIPELINE](../26-ASSET-PIPELINE.md) 8장,
  [ADR-0090](0090-asset-database-stable-id-sidecar-meta.md) (.meta 안정 id), [09-SERIALIZATION](../09-SERIALIZATION.md) 9장 (.sbpkg), [08-NETWORK](../08-NETWORK.md) (핸드셰이크)

## 맥락

ADR-0090 은 에셋에 .meta 안정 id 를 주고 패키지에 에셋을 싣기로 했다 (M6-T9). 2차 감사는 그 위에서 패키지가 틀릴 수 있는 길을 더 찾았다:
빌드 중 파일 변경 (옛 · 새 파일 섞임), 스크립트가 이름으로 만드는 참조 (정적 분석에 안 보임), .meta 복사로 생긴 같은 id, 대소문자 · Unicode
정규화만 다른 경로 (Windows · macOS 에서 같은 파일), 그리고 표시 에셋이 다른 클라이언트가 조용히 접속하는 것 (contentHash 는 presentation 을 뺀다).

## 결정

```text
1. 빌드 스냅숏 (NG-01): 시작 때 입력 목록 (경로 · 크기 · SHA-256), 읽은 내용이 목록과 다르면 실패 — 조용히 새 내용을 쓰지 않는다. 출력은 임시 → 원자적 이름 바꾸기
2. Asset Group (NG-02): 동적 참조는 assets/groups.json 의 그룹 (glob · .meta 라벨) 으로 선언 — 그룹째 포함. 그룹 밖 이름 = 진단. 빌드 보고 (개수 · 크기 · 미참조)
3. 에셋 id 충돌 (NG-12): 같은 id · 같은 내용 = 하나로, 같은 id · 다른 내용 = 오류 (어느 쪽도 고르지 않는다), 복제된 .meta = 복제본에 새 id,
   .meta 없음 = 새 id + 경고 (경로 해시로 돌아가지 않는다), 의존 패키지 에셋은 pkg:<name>/ 이름공간
4. 경로 정책 (NG-13): "/" · UTF-8 · NFC 로 기록, 대소문자 · NFC 충돌 = 오류, Windows 예약 이름 · 금지 문자 · 끝 공백 · 점 · .. · 절대 · 링크 거절,
   경로 240 · 구성 요소 120 바이트. 한 검사 함수 (어느 OS 에서도 같은 결과)
5. 해시 분리 (NG-14): contentHash (시뮬레이션 — 지금 그대로) 와 presentationDigest (presentation/** + 프로젝트 에셋). 핸드셰이크에 둘 다 (프로토콜 16):
   contentHash 다름 = 거절, presentationDigest 만 다름 = 접속 + 경고. 리플레이 · 세이브 결속은 contentHash
6. Export Profile (NG-08): export/<이름>.json (플랫폼 · 아키텍처 · 렌더 백엔드 · 그룹) 검증, 미검증 플랫폼 = 경고. v0.6 은 windows-x64
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| 충돌 A (채택) | 같은 id · 다른 내용 = 오류 | 조용한 선택은 "어떤 PNG 가 나오나" 를 빌드 순서에 맡긴다 — 재현 불가 |
| 충돌 B | 나중 것이 이김 (덮어쓰기) | 의존 패키지 업데이트가 프로젝트 에셋을 몰래 바꾼다 |
| 경로 A (채택) | 가장 엄격한 OS 규칙을 모든 OS 에 | Linux 에서 만든 프로젝트가 Windows 에서 깨지는 것을 만들 때 잡는다 |
| 경로 B | OS 마다 그 OS 규칙 | 같은 프로젝트가 OS 에 따라 열리고 안 열린다 |
| 동적 참조 A (채택) | 명시 그룹 | 예측 가능 · 크기 보고 가능 |
| 동적 참조 B | 프로젝트 에셋 전부 포함 | 크기 폭주 · 미사용 에셋을 못 찾음 |

## 결과

- .sbpkg 형식 3 (M6-T9) 에 스냅숏 해시 · 그룹 · 이름공간을 같이 넣는다 (형식을 두 번 올리지 않게). 프로토콜 16 은 NG-14 커밋에서.
- 시험: 26 8장의 픽스처 (충돌 4 경우 · 경로 규칙마다 · 빌드 중 변경 · 같은 입력 = 같은 digest · 다른 표시 = 경고 접속).
- 24 P8 (패키지 에셋 바꿔치기) 의 대응에 1 · 3 · 4 를 더한다 — release-gates v0.6.
