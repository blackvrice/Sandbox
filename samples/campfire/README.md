# 모닥불 지키기 (campfire) — 생존 템플릿

`sbx new <폴더> --template survival --id <작성자>.<게임>` 의 틀 (v0.10 M6-T2, [ADR-0144](../../docs/adr/0144-project-templates-marked-examples.md)).
모닥불의 장작 (life.energy) 은 초마다 1.5 줄고, 20 초마다 늑대가 판 가장자리에서 와서 불을 문다 (life.health −20). 장작 넣기 (Q) ·
덫 놓기 (E) 로 90 초를 버티면 승리, 불이 꺼지면 (장작 0 · 체력 0) 패배.

```text
content/camp/
  scripts/fire.ts       컴포넌트 camp.marker (모닥불 표시 — 복제, HUD 가 찾는다) · 월드 상태 camp.waves {count}
  scripts/waves.ts      서버 시스템 camp.waves — onStart: 모닥불을 가운데에 · ctx.every(600, "wave"),
                        onEvent timer: 가장자리 (결정적 난수) 에 늑대, onEvent death (태그 camp): 꺼짐 표시 camp.lost
  scripts/hud.ts        client 시스템 camp.hud — 모닥불의 Energy · Health → UI 변수 camp.fuel · camp.health
  actions.json          camp.fuel (불 둘레 4 — 장작 +30, 90 미만일 때) · camp.trap (늑대 둘레 2 — 없애고 점수 +1)
  behaviors/hunter.json 늑대 — prey 를 찾아가 문다 (bite) · 배부르면 10 초 쉰다
  rules/bite.json       camp.wolf_bites_fire — 불의 체력 −20, 늑대의 기운 +60
  modes/hold.json       게임 모드 camp.hold — camp.lost 가 생기면 패배 · 90 초면 승리
  presentation/         HUD (장작 · 체력 · 덫 수 · 진행 막대 · 버튼 둘) · 키 Q · E
playthrough.json        자동 플레이 — 15 초마다 장작, 불 둘레에 덫 (8 배속) — 승리 tick 2701 (덫 4). 아무것도 하지 않으면 tick 1203 에 패배
```

## 만든 길 (sbx 명령만)

```text
sbx new samples/campfire --id sandbox.campfire --name "모닥불 지키기"   (빈 월드)
  content/camp 를 더하고 매니페스트 content.packs 에 camp · gameMode camp.hold
sbx resave samples/campfire --resume                                   (시작 월드를 지금 콘텐츠로 · 일시정지 풀고)
sbx test samples/campfire
```

## 바꿔 보기

- 난이도: waves.ts 의 600 (파도 간격) · fire.json 의 drainPerSecond · bite.json 의 −20 · hold.json 의 90 초
- 자원: 장작을 판에 흩어 두고 줍기 (Prefab + 행동 effects) — 지금은 무한
- 밤낮: ctx.every 로 상태를 바꾸고 client HUD 에서 보이기
