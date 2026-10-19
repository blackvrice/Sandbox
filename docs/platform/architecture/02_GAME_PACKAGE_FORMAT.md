# 03. GameProject / GamePackage / World / Save 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 네 가지 분리
- **GameProject**: 편집 가능한 원본 리포지토리. 소스 `.ts`, 시각 트리거 그래프, 아트 소스, UI 원본, 테스트.
- **GamePackage (`.sbpkg`, 제안)**: 검증된 실행 배포 단위. 정적 콘텐츠·Bytecode·런타임 Manifest·리소스 인덱스·락 파일.
- **World/Map**: 지형, Prefab 인스턴스, Spawn, 규칙, 게임 모드 구성. 여러 World가 한 게임에 속할 수 있다.
- **SaveGame**: 런타임 ECS 상태·게임 진행도·플레이어 상태. 패키지 버전/마이그레이션 정보 포함.

## 2. 디렉터리 구조 (제안)
```text
MyGame/
  sandbox.project.json
  sandbox.lock
  src/server/          # 서버 SandboxScript
  src/client/          # UI·카메라·연출
  src/shared/          # 타입·공용 유틸
  worlds/              # 맵, GameMode 설정
  assets/              # Sprite/Audio/Model 원본 또는 링크
  prefabs/             # 재사용 가능한 개체
  visual/              # 그래프 원본
  tests/               # 게임 통합·스크립트 테스트
  build/MyGame.sbpkg   # 배포 산출물(읽기 전용)
```

## 3. 프로젝트 Manifest 예시
```json
{
  "schemaVersion": 1,
  "gameId": "studio.example.rts",
  "name": "Sample RTS",
  "version": "0.1.0",
  "engine": ">=0.3 <1.0",
  "scriptApi": 1,
  "entryWorld": "worlds/main.world",
  "dependencies": {"@community/path-ui": "^1.1.0"},
  "permissions": {"serverHttp": false},
  "network": {"maxPlayers": 8}
}
```
`engine`·`scriptApi`·의존성을 빌드 시 검사한다. 위 형식은 설계 예시이며 기존 콘텐츠 팩 JSON과 호환 어댑터가 필요하다.

## 4. 빌드 파이프라인
```text
Project + Dependencies → Manifest 검증 → Import/Cook assets
 → Schema 검증 → TS AST/typecheck/subset → Typed IR
 → Bytecode + Verifier → Visual Trigger Compile
 → Manifest/Hash/SBOM/Lock → Signing → .sbpkg
```
- 배포 패키지에는 실행에 필요한 컴파일된 Bytecode를 포함. 원본 `.ts` 동봉은 라이선스/공개 정책으로 선택.
- C++ Reflection의 내장 Component `StableId`와 Custom Schema를 패키지 빌드 시 검증한다.
- 현재 콘텐츠 로더는 게임플레이 미등록 컴포넌트를 거절한다. Custom Component 동적 Registry가 준비되기 전까지 이 제약을 변경하지 않는다.

## 5. 무결성/재현성
- Artifact 파일별 SHA-256, GamePackage의 content digest 및 Lockfile 정확 버전 기록.
- 다운로드 중단 재개 및 임시 저장 → 검증 → 원자적 교체.
- 서버/클라이언트 패키지 호환 검사, 미일치 시 허가된 채널에서 다운로드 후 재접속.
- 패키지 공급망 검사: 라이선스, 의존성, 허용 API, Bytecode 검증, 리소스 크기/압축 폭탄 제한.

## 6. 저장 및 마이그레이션
- SaveGame은 GamePackage digest, component schemaVersion, simulationVersion, worldVersion에 결속.
- Component 필드 추가는 기본값 적용, 삭제·타입 변경은 명시적 migration step 필요.
- 저장은 서버 권위, 임시 파일로 기록 후 안전한 교체, 백업 및 체크섬 확인.
- 이전 버전 패키지 롤백 시 저장 데이터가 다운그레이드 불가능할 수 있어 스냅샷 유지.

## 7. 완료 기준
A. 동일 프로젝트 두 빌드의 digest 동일(빌드 환경 고정 시). B. 수정된 파일은 로드 거부. C. Lockfile 없이 재빌드해 버전이 변경되는 일을 막는다. D. vN Save를 vN+1에서 마이그레이션하고 재현성 검증. E. 동일 패키지를 Local/Remote에서 실행.
