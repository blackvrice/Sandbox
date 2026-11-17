# ADR-0153. 오디오 출력 — 직접 만든 소프트웨어 믹서 + OS 장치 (waveOut · AudioQueue · ALSA dlopen), WAV, 팩 sounds.json

- 상태: **Accepted** · 날짜: 2026-11-17 — [ADR-0095](0095-audio-v05-events-null-output-v06.md) 2 (miniaudio 후보) 를 대신한다
- 관련: [07-PLATFORM](../07-PLATFORM.md) 7장, [26-ASSET-PIPELINE](../26-ASSET-PIPELINE.md) 3장 (sound importer),
  [11-CONTENT-SCHEMA](../11-CONTENT-SCHEMA.md) (presentation/sounds.json), [19-SANDBOXSCRIPT](../19-SANDBOXSCRIPT.md) 21.1 (playSound),
  task-registry NG-17

## 맥락

ADR-0095 는 v0.5 에 소리 이벤트 · Null 백엔드를 두고 실제 출력 · sound importer · sounds.json 을 뒤로 미뤘다 (후보 miniaudio — 단일 파일,
라이선스 확인은 도입 ADR 에서). 외부 라이브러리를 새로 들이려면 원본을 받아 와 검토해야 한다 (external/README — 출처 · 해시). 필요한 것은
작다: 짧은 효과음 몇 개 · 버스 볼륨 · 2D 패닝 · 세 OS 의 기본 출력.

## 결정

```text
1. 믹서      platform/audio/MixerAudioBackend — IAudioBackend 를 소프트웨어로: 목소리마다 디코드된 f32 PCM 을 선형 보간 (소리
             표본율 → 장치 표본율 × pitch), 목소리 볼륨 × 버스 × 마스터, positional 이면 리스너와의 x 차이로 상수 파워 패닝 (16 단위
             에서 끝) · 거리 감쇠 1 / (1 + d / 24). 반복 아닌 목소리는 끝까지 그린 뒤 update 가 거둔다 (Null 과 같은 수명 규칙). 모든
             공개 메서드 · 장치 스레드의 render 는 한 뮤텍스
2. 장치      IAudioDevice — start(표본율, 채널, render) · stop. Windows = waveOut (winmm, 이벤트 · 버퍼 셋 × 20 ms · 16 비트),
             macOS = AudioToolbox AudioQueue (f32 · 버퍼 셋 × 20 ms), Linux = ALSA (libasound.so.2 를 실행 중에 dlopen — 빌드에 ALSA
             개발 파일이 없어도 되고, 없는 PC · 컨테이너는 장치 없음). 그 밖은 null. 지연은 약 60 ms (더 낮은 WASAPI · AUHAL 은 [계획])
3. 앱        SandboxClient: 헤드리스 · --no-audio 면 Null, 아니면 OS 장치 위의 믹서 — 장치가 없거나 열지 못하면 Null (경고). 서버는
             오디오를 링크하지 않는다 (그대로)
4. 디코더    foundation/audio/WavDecoder — WAV PCM 8 · 16 · 24 · 32 · float 32 (EXTENSIBLE 포함), 1 · 2 채널, 8 ~ 192 kHz, 10 분까지.
             믿지 않는 파일 (덩어리 크기 · 정렬 · blockAlign · 잘림). OGG 는 [계획]. 에셋 importer 는 .wav 를 "sound@1"
5. 데이터    팩 presentation/sounds.json — { "sounds": { "<id>": { "file": "asset:<id>" | "assets/…", "bus": "sfx" | "ui" | "ambient",
             "volume": 0 ~ 1 } } } (머티리얼 sprite 와 같은 경로 규칙, 클라 전용 — contentHash 밖, 로더는 원문만 PackInfo.sounds).
             NetworkSession 이 열 때 디코드해 백엔드에 올리고 (apps/client/presentation/PackSounds), client.playSound(id) 를 그
             프레임에 재생 (모르는 id 는 센다)
6. 시뮬레이션 오디오는 표시 쪽만 — 시뮬레이션 · WorldHash · 세이브에 닿지 않는다 (04)
```

## 결과

- 외부 의존을 늘리지 않고 세 OS 에서 소리가 난다 — 실제 장치 출력은 사람이 듣는 수동 QA (세 OS — MANUAL-QA), 믹서 · 디코더 · 이벤트 →
  재생은 단위 시험 (장치 없이 render 를 직접).
- 위치 소리 (playSound 에 자리) · 음악 스트리밍 · OGG · 믹서 효과는 RT-06-15 · [계획].

## 고른 것 · 버린 것

- miniaudio (ADR-0095 후보): 원본을 받아 출처 · 라이선스를 검토해야 한다 — 지금 쓰는 기능이 작아 직접 만들었다. 기능이 커지면
  (디코더 여럿 · 장치 고르기 · 낮은 지연) 다시 본다.
- WASAPI (Windows): COM · 이벤트 주도 · 형식 협상 코드가 크다 — waveOut 으로 먼저.
- ALSA 링크 (-lasound): 개발 파일이 없는 빌드 PC · CI 에서 깨진다 — dlopen.
