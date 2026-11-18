# ADR-0162. 편집기 그룹 (세이브 폴더 editor.json · saveId) · Hierarchy 검색 낱말 · persist.persistence 복제

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [25-STUDIO-ARCHITECTURE](../25-STUDIO-ARCHITECTURE.md) 7장 (그룹 제안), [10-EDITOR](../10-EDITOR.md) (목록 패널),
  [02-ECS](../02-ECS.md) 14장 (컴포넌트 플래그), [08-NETWORK](../08-NETWORK.md) 6장 (복제), [14-PERFORMANCE](../14-PERFORMANCE.md),
  task-registry ST-06-6

## 맥락

ST-06-6 의 완료 기준은 "10k 측정". 목록 (Hierarchy) 패널은 prefab · 번호 글자 거르기 하나뿐이고, 개체를 묶어 접을 길이 없다. 25 의
제안은 편집기 전용 그룹을 worlds/<w>/editor.json 에 saveId 로 두는 것 — 그런데 클라이언트 (Studio) 는 netId 만 안다: netId 는 세이브를
넘어 같지 않고 (불러오기가 새로 매긴다), saveId (persist.persistence) 는 서버에만 있다. 로컬 서버는 스레드에서 돌아 클라이언트가 서버
월드를 읽을 수 없다.

## 결정

```text
1. 검색        낱말 (공백으로 나눔 — 모두 맞아야, 대소문자 무시): 글자 = Prefab id 에 있다 ("개체" = Prefab 없음), #<번호> = netId,
               prefab:<앞> , tag:<이름> (콘텐츠 태그 — 없으면 아무것도), group:<이름 글자>. editor/services/HierarchyService
2. 그룹        편집기 전용 — 시뮬레이션 · 해시 · 복제 밖. 이름 (64 바이트, 겹침 없음) · 멤버 (saveId — 개체 하나는 그룹 하나) · 접힘.
               256 그룹 · 그룹마다 65,536. 목록 = 그룹 머리 (만든 차례, 검색에 맞은 수) → 펼친 그룹의 맞은 개체 → 그룹 없는 개체.
               검색에 맞은 것이 없는 그룹은 뺀다
3. 저장        세이브 폴더의 editor.json {"format": "sandbox.editor", "version": 1, "groups": [{name, members, collapsed}]} — Studio 가
               로컬 서버의 저장이 받아들여지면 쓰고, 불러오기가 받아들여지면 읽는다 (없으면 빈 그룹, 틀리면 경고 · 빈 그룹). 지워진 개체는
               목록을 만들 때 빠진다 (파일에는 다음 저장까지 남는다)
4. saveId      persist.persistence 에 Replicated 를 더한다 — 클라이언트의 복제 월드가 saveId 를 안다 (생성 때 한 번 — 바뀌지 않는다).
               해시 · 세이브 · 시뮬레이션 그대로 (Replicated 는 해시에 닿지 않는다). 엔진 관리라 명령으로 바꿀 수 없다 (지금처럼)
5. 측정        개체 10,000 · 그룹 50 (×100) 의 목록 만들기 · 검색 — 시험이 시간을 찍고 Debug 50 ms 상한 (Release 는 14 에 기록)
```

## 결과

- Studio 에서 개체를 이름 붙은 묶음으로 접고, 태그 · Prefab · 그룹으로 찾는다. 그룹은 세이브와 같이 다닌다.
- 원격 서버에 붙은 Studio 도 saveId 를 알지만 저장 파일은 로컬 서버만 (원격 세이브 폴더를 쓸 수 없다) — 원격은 세션 동안만.
- 멀티 편집의 그룹 공유 · 부모-자식 변환은 [계획] (25 — v0.7 · v1.x).

## 고른 것 · 버린 것

- 그룹을 netId 로 저장: 불러오기가 netId 를 새로 매겨 다른 개체를 가리킨다.
- 서버가 editor.json 을 쓴다 (저장 명령에 그룹을 실어): 편집기 전용 데이터가 와이어 · 서버를 지난다 — saveId 를 보이는 편이 작다.
- 로컬 서버 월드를 클라이언트 스레드에서 읽기: 스레드 모드에서 경합 — 복제로.
