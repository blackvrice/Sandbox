# ADR-0077. 게임 패키지 형식 2 — 라이선스 요약 (SBOM): 팩마다 version · license · 팩 digest, 여는 쪽이 다시 계산 · pack.json 과 견준다

- 상태: **Accepted** · 날짜: 2026-10-28
- 관련: [ADR-0054](0054-game-package-sbpkg-sha256-digest-save-binding.md) (게임 패키지 .sbpkg — 형식 1), [ADR-0071](0071-v0-4-package-manager-scope-pack-as-package-single-version-resolution.md)
  1 (license — SPDX 식), [ADR-0073](0073-lockfile-format-install-folder-content-packs-unlocked.md) (설치 폴더 · 팩 digest),
  [09-SERIALIZATION](../09-SERIALIZATION.md) 9장, [18-PLATFORM](../18-PLATFORM.md) 6.4 (v0.4 M5-T2)

## 맥락

v0.4 는 다른 제작자의 팩을 설치해 게임에 담는다. 배포하는 게임 패키지 (.sbpkg) 를 받은 쪽은 그 안에 어떤 팩이 어떤 라이선스 ·
버전으로 들어 있는지 알아야 한다 (ADR-0071 M5 — "패키지 index 에 팩마다 license · digest"). 형식 1 의 index 는 파일 목록뿐이다.
정할 것: 어디에 · 무엇을 적나, 요약을 믿을 수 있게 하는 법 (index 만 고쳐 라이선스를 속이기), 옛 패키지.

## 결정

```text
1. index 에 packs: [{id, version, license, digest, path}] (팩 id 순) — 형식 번호를 2 로 올린다 (헤더 format u32). 읽는 쪽은 1 · 2 를
   읽는다 (형식 1 = packs 없음). 쓰는 쪽은 늘 2
2. 대상: 프로젝트 콘텐츠가 읽은 팩 (requires · 의존 포함 — 패키지에 담는 팩과 같다). path = 패키지 안의 팩 폴더 ("content/<폴더>" ·
   ".sandbox/packages/<팩>"). version · license = 그 팩 pack.json 의 값 (없으면 빈 문자열)
3. 팩 digest = 그 폴더 아래 파일들을 팩 폴더 기준 경로로 놓고 패키지 digest 와 같은 규칙 (PackSource 의 팩 digest 와 같다 — 설치한
   팩이면 sandbox.lock 의 digest 와 같은 값)
4. 믿음: readPackageIndex (index 만) 가 팩 digest 를 files 에서 다시 계산해 견주고 (모양 · 팩 id 순 · path 규칙 포함), 파일 바이트를
   보는 verifyPackage · extractPackage 는 그 팩의 pack.json 을 읽어 id · version · license 를 견준다. 하나라도 다르면 열지 않는다.
   game digest (파일 목록의 지문) 는 그대로 — 요약은 파일에서 나온 값이라 따로 지문에 넣지 않는다
5. 도구: sbx build 가 "팩 n — <라이선스> <수> · …" 한 줄, sbx check <패키지> 가 팩마다 "팩 <id> <version> · <license> · <path> ·
   digest <앞 16>…" (형식 1 이면 요약이 없다고 알린다)
```

## 근거

```text
- index 에 두기: 패키지를 풀지 않고 (readPackageIndex — 헤더와 index 만) 요약을 볼 수 있다 (배포 · 심사 도구)
- 다시 계산 · 견주기: 요약을 game digest 에 넣지 않아도 파일과 어긋난 요약은 열리지 않는다 — 요약 자체가 고칠 수 있는 진술이
  아니라 파일에서 나온 사실이 된다
- 형식 번호: index 의 키를 더하는 것도 읽는 쪽의 규칙을 바꾼다 (형식 2 를 옛 엔진이 열면 VersionMismatch 로 분명히 거절)
```

## 결과

- 얻는 것: 배포한 게임에 든 남의 팩과 그 라이선스 · 버전 · digest 를 패키지만으로 확인한다. 설치한 팩의 digest 는 잠금과 같다.
- 포기하는 것: 형식 2 패키지는 v0.4 전의 엔진이 열지 않는다 (v0.4 엔진은 형식 1 을 연다).
- 위험: 라이선스 식의 뜻 (호환성 · 의무) 은 보지 않는다 — 모양 (SPDX 식) 만. 라이선스 정책 · 심사는 v0.6 · v0.7 (ADR-0071 9).

## 대안

| 대안                                   | 기각 사유                                                                 |
|----------------------------------------|---------------------------------------------------------------------------|
| 요약을 따로 파일 (SBOM.json) 로 담기   | 파일이면 game digest 에 들어가 같은 게임의 digest 가 바뀐다 · 풀어야 본다 |
| 요약을 그대로 믿기 (다시 계산 없음)    | index 만 고쳐 라이선스를 속일 수 있다                                     |
| SPDX 문서 형식 그대로                  | 지금 필요한 것은 팩 · 버전 · 라이선스 · digest 넷 — 외부 형식 내보내기는 [계획] |

## 재검토 조건

외부 SBOM 형식 (SPDX · CycloneDX) 내보내기가 필요할 때, 레지스트리 (v0.6) 가 패키지 서명과 함께 요약을 요구할 때.
