# ADR-0159. 오디오 믹서 · 2D 위치 — playSoundAt · 반복 · stopSound · 버스 볼륨 (스크립트 · client.json), 리스너 = 카메라

- 상태: **Accepted** · 날짜: 2026-11-18
- 관련: [ADR-0153](0153-audio-output-own-mixer-os-devices-sounds-json.md) (믹서 · 장치 · sounds.json), [ADR-0095](0095-audio-v05-events-null-output-v06.md),
  [07-PLATFORM](../07-PLATFORM.md) 7장, [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) (sounds.json), [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md)
  (client API), task-registry RT-06-15

## 맥락

NG-17 (ADR-0153) 의 믹서는 이미 목소리마다 2D 패닝 · 거리 감쇠 (positional), 버스 볼륨, 반복, pitch 를 안다. 게임이 그것을 쓸 길이
없다 — client.playSound(id) 는 자리 없는 소리 하나뿐이고, 리스너는 원점에 있으며, 버스 볼륨을 정할 곳이 없다. 반복 소리 (배경 ·
엔진 소리) 는 멈출 수 없다. RT-06-15 의 완료 기준은 비어 있다 — 아래를 기준으로 한다 (시험: 믹서 표본으로 패닝 · 반복 · 멈춤 ·
볼륨).

## 결정

```text
1. 위치 소리   client ctx.playSoundAt(id, x, y) — 월드 좌표 (positional). 패닝 · 감쇠는 믹서 그대로 (리스너와 x 차이 16 단위 = 한쪽
               끝, 볼륨 = 1 / (1 + 거리 / 24)). playSound 와 같은 상한 (프레임마다 32 — 합쳐서)
2. 리스너      앱이 프레임마다 카메라 가운데를 리스너로 (Application — 월드를 그리는 프레임)
3. 반복 · 멈춤 sounds.json 의 "loop": true · "pitch": 0.25 ~ 4 (기본 false · 1). ctx.stopSound(id) — 이 세션에서 그 id 로 낸
               목소리를 모두 멈춘다 (프레임마다 32 개, 재생 요청과 같은 줄 — 부른 차례)
4. 버스 볼륨   ctx.setVolume(bus, volume) — bus "master" · "sfx" · "ambient" · "ui", 0 ~ 1 (자른다, 모르는 버스 = 아무 일 없음).
               사용자 설정 client.json "audio": { "master": 0 ~ 1, … } 이 시작 값 (스크립트가 그 위에 바꾼다 — 저장하지 않는다)
5. 표시 쪽만   소리는 시뮬레이션 · 해시 · 서버와 무관 (client 스크립트 · 클라이언트 설정). 장치가 없으면 Null 백엔드가 센다
```

## 결과

- 게임이 자리 있는 효과음 · 배경 소리 (반복 + 멈춤) · 옵션 화면의 볼륨 막대를 팩과 client 스크립트로 만든다.
- 음악 스트리밍 (긴 파일을 조금씩) · OGG · 믹서 효과 (리버브 · 필터) · 도플러 · 목소리 우선순위는 [계획].

## 고른 것 · 버린 것

- 목소리 핸들을 스크립트에 (playSound 가 핸들을 돌려줌): 핸들 수명 · 위조 검사 · 세션을 넘는 핸들 — id 로 멈추면 충분하다.
- 리스너를 스크립트가 정함: 대부분 카메라 — 따로 정할 길은 필요할 때 [계획].
