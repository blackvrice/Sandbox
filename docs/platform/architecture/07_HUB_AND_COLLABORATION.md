# 08. Sandbox Hub / GitHub형 개발자 협업 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 목적
Sandbox Hub는 게임/패키지 중심 개발자 허브다. Repository, 협업, Issue/Discussion, Release, Game-specific Diff, CI/CD, 팀 권한과 배포 내역을 제공한다. **무료 개인 Private Repository를 허용**하고 Pro/Business는 생산성·거버넌스에서 과금한다.

## 2. 초기 연동 전략
- v0.6까지 기존 Git/GitHub를 소스 저장소의 주요 백엔드로 활용(선택 연결). Sandbox는 프로젝트 메타데이터·패키지 Registry·빌드·게임 특화 리뷰를 제공.
- 이중 진실원천 최소화: 커밋/PR은 Git 제공처가 권위, Package Release·World Diff/Build artifact는 Sandbox가 권위.
- 장기적으로 네이티브 Game Repository 서비스를 자체 구현할지는 비용·사용량으로 결정.

## 3. 주요 도메인
| 도메인 | 주요 객체 |
| --- | --- |
| Accounts | User, Organization, Membership, Role |
| Projects | GameProject, RepoLink, Branch, Commit, Release |
| Collaboration | PullRequest, Review, Issue, Discussion, AssetComment |
| Package | Package, Version, Dependency, License, Download |
| Build | Pipeline, Job, Artifact, TestResult, DeployApproval |
| Access | Permission, AuditEvent, Secret, Quota |

## 4. 게임 전용 리뷰 기능
- TypeScript: Code diff + TypeCheck + Lint + Script budget 영향.
- Component Schema: 필드 추가·삭제·타입 변경, Save/Replication compatibility 표시.
- World/Map: 변경된 타일/오브젝트/Prefab을 시각적으로 표시하고 충돌 후보 표시.
- Asset: Sprite/Texture 비교, 크기·라이선스 변경, 중복 해시.
- Package: Lockfile dependency diff, 권한 상승 위험, 취약 의존성 확인.

## 5. 협업 흐름
```text
Branch/Fork → Edit & Commit → PR → CI compile/content/tests
→ Schema/Map Visual Diff → Review → Merge → Package Release
→ Staging Session → Production Deploy
```
- 게임 데이터는 텍스트 기반 정규화 포맷 우선(안정 정렬·고정 ID); 바이너리는 Lock/Asset Versioning 및 명시적 충돌 해결.
- 동일 World의 동시 변경은 최초에는 역할·영역 잠금과 충돌 보고, 이후 의미적 병합 도입.

## 6. 가격 모델 (제안)
- Free: 공개/비공개 프로젝트, 기본 팀 협업, Issue/PR, 제한된 빌드 시간 및 보관량.
- Pro: 고급 빌드/테스트, 추가 용량, 성능/이용 분석.
- Business: 조직 권한, Merge 승인, 감사 로그, 통합 자동 배포, 팀 비밀 관리.
- Enterprise: SSO, 전용 관리 영역, 조직 정책, SLA, 지원 계약.

## 7. 최소 성공 기준
개발자 A가 공개 패키지 Repo 생성 → B가 기여 PR → CI 검증 → 리뷰 승인 → 릴리스 → C가 Package Manager로 설치하는 전체 사이클 수행. 초기에는 GitHub PR을 링크해서 동일 흐름 구현 가능.
