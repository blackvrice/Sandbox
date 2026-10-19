# 07. Sandbox Package Manager / Registry 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 비전
npm/Cargo 같은 의존성 경험과 게임 콘텐츠에 특화된 설치·편집·빌드가 목표다. 모든 개발자가 Components, Systems, Script, Visual Trigger, Prefab, UI, Sprite/Audio, Feature Bundle을 제작하여 공개·비공개/무료·유료로 배포할 수 있다.

## 2. 패키지 정의
```json
{
  "schemaVersion": 1,
  "name": "@community/inventory",
  "version": "1.2.0",
  "type": "feature",
  "license": "MIT",
  "engine": ">=0.4 <1.0",
  "scriptApi": 1,
  "dependencies": {"@sandbox/items": "^1.0.0"},
  "permissions": {"serverHttp": false},
  "exports": {"components": ["inventory"], "systems": ["inventorySystem"]}
}
```
- 저장/배포는 내용 주소 해시와 서명 검증을 사용.
- 공개 GitHub 저장소라고 자유 재배포 허용은 아님. 명시적 소프트웨어·아트 라이선스가 필요.
- `type`은 `component`, `system`, `feature`, `asset`, `editor-tool`, `game` 등으로 확장 가능하되 MVP는 단일 Manifest + exports를 권장.

## 3. CLI 계약 (제안)
| 명령 | 의미 |
| --- | --- |
| `sbx init` | 새 GameProject/Feature 생성 |
| `sbx add @owner/pkg@^1.2` | 패키지 추가, 잠금 파일 수정 |
| `sbx remove @owner/pkg` | 의존성 제거, 영향 확인 |
| `sbx update` | 허용 범위 내 업데이트 제안/검증 |
| `sbx build` | Lockfile 기반 재현 빌드 |
| `sbx test` | SandboxScript/콘텐츠/정합성 테스트 |
| `sbx publish --dry-run` | 라이선스·의존성·바이트코드·보안 검증 |
| `sbx publish` | 승인 Registry 배포, 불변 릴리스 |

## 4. 의존성 해결 및 Lockfile
- SemVer 범위는 Manifest, 정확 버전·digest·source URL·license는 `sandbox.lock`에 고정.
- 순환 의존성은 명시적으로 거절하거나 타입 전용 의존성으로 분리.
- 같은 Component stableId의 충돌은 우선순위가 아닌 **빌드 실패**.
- 런타임에 여러 버전 공존이 어려운 공유 ECS Schema는 단일 호환 버전으로 해결하도록 제약.
- 게임 패키지로 배포할 때 필수 실행 의존성은 잠금 결과에 따라 묶는다.

## 5. 설치 과정
```text
Search → Select version → Resolve graph → License/Permission Review
→ Download → Verify hash/signature → Validate Schema/Bytecode
→ Update sandbox.lock → Generate Type Declarations → Reload Editor
```
- 패키지 설치로 앱 외부 Native 명령을 자동 실행하지 않는 것을 원칙으로 한다.
- 실행 권한(`http`, `storage`, `ui`, `gameplay`)은 패키지 심사 및 설치 화면에서 표시.
- 유료 패키지의 사용권 검사는 합법적 오프라인 개발 흐름과 환불 정책을 고려.

## 6. Registry / GitHub 연동
- v0.4: 로컬 폴더·Git 태그/커밋 기반 설치, 잠금 파일, 오프라인 캐시.
- v0.6: Sandbox Registry API (`GET /packages`, `/versions`, `/download`, `POST /publish`).
- 저장소의 소스 협업은 초기에 GitHub 연동, Registry는 패키지 정체성과 검증·배포 관리.
- README, changelog, 버전별 API docs, 다운로드, 호환성 Matrix, 이슈 링크 제공.

## 7. 보안·운영
Typosquatting, 악성 의존성, 토큰 유출, 보안 취약점, 라이선스 위반을 신고/차단하는 경로와 긴급 릴리스 비활성화 정책 마련. 기존 게임은 lockfile 상태를 유지하되 중대한 위험에서는 경고와 서버 정책으로 차단할 수 있다.

## 8. 완료 기준
Git 저장소의 Component+Script 패키지를 설치 → SDK 자동완성 → 빌드·테스트 → 다른 PC에서 Lockfile 재현 → 업데이트/롤백. 충돌·검증 실패 시 프로젝트 상태는 이전 버전으로 복원.
