# ADR-0095. 오디오 (D-14) — v0.5 는 소리 이벤트 · Null 백엔드 (센다), 실제 출력 · sounds.json 은 v0.6

- 상태: **Accepted** · 날짜: 2026-11-03
- 관련: [20-CREATOR-ALPHA](../20-CREATOR-ALPHA.md) 7.4 (D-14 — "정하지 못하면 이벤트 · Null, 출력 v0.6"), [07-PLATFORM](../07-PLATFORM.md) 7장
  (IAudioBackend — 핸들 · 목소리 · 버스, NullAudioBackend 현행), [18-PLATFORM](../18-PLATFORM.md) 6.1 (D-14), [26-ASSET-PIPELINE](../26-ASSET-PIPELINE.md) 3장 (sound importer)

## 맥락

v0.5 M2-T6 은 client 스크립트의 소리 이벤트와 오디오 백엔드 결정을 함께 가진다. 지금은 IAudioBackend 인터페이스와 NullAudioBackend (소리 없음 ·
수명 규칙만) 뿐이고, 소리 에셋을 가져오는 길 (WAV · OGG importer) · 이벤트 → 소리 표 (presentation/sounds.json) 도 없다. 실제 출력에는 백엔드
(WASAPI 직접 · CoreAudio · ALSA/Pulse — 또는 miniaudio 같은 단일 파일 라이브러리) 와 오디오 스레드 · 디코더 · 라이선스 검토가 필요하다.

## 결정

```text
1. v0.5: client.playSound(id) 는 소리 이벤트를 낸다 (프레임마다 32 개 · 트랩난 호출은 버림). 앱은 이벤트를 가져가 센다 (NullAudioBackend 그대로) —
   소리는 나지 않는다. 스크립트 · 콘텐츠 API 는 v0.6 에서 바뀌지 않는다 (id 문자열 → v0.6 의 sounds.json 이 소리 에셋에 묶는다)
2. v0.6: 실제 출력 백엔드 + sound importer (WAV · OGG) + presentation/sounds.json (이벤트 id → 에셋 · 버스 · 볼륨). 후보는 miniaudio (단일 파일 ·
   Windows · macOS · Linux · MIT-0 또는 공개 도메인 — 라이선스 원문 확인은 도입 ADR 에서) — WASAPI 직접은 플랫폼마다 따로 만들어야 해서 뒤
3. 오디오는 표시 쪽 (클라이언트) 만 — 시뮬레이션 · WorldHash 에 닿지 않는다 (04 그대로)
```

## 선택지

| 안 | 내용 | 판단 |
|----|------|------|
| A (채택) | v0.5 이벤트 · Null, v0.6 출력 | 알파 G1 ~ G9 (28) 에 소리가 없어도 된다. 의존 · 스레드 · 라이선스를 v0.5 에 더하지 않는다 |
| B | v0.5 에 miniaudio 바로 | 에셋 importer · sounds.json · 스레드 · 플랫폼 세 개 시험이 함께 와야 의미가 있다 — v0.5 범위가 넘친다 (ADR-0092 결과) |
| C | WASAPI 직접 (Windows 만) | Linux · macOS 를 따로 — 지원 매트릭스와 어긋난다 |

## 결과

- D-14 닫힘 (18 6.1). 제작자는 v0.5 에서 소리 이벤트를 코드에 넣어 둘 수 있고, v0.6 에서 sounds.json 만 더하면 들린다.
- 26 의 sound importer · 23 의 RT-06-15 (믹서 · 2D) 가 v0.6 출력 작업의 자리.
