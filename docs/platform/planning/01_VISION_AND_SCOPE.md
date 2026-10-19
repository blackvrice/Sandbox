# 01. 제품 비전 및 범위

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 제품 정의
Sandbox는 **Sandbox Engine + Sandbox Hub + Sandbox Cloud + Marketplace**를 통합하는 게임 제작·배포 플랫폼이다. 초보자는 시각적 편집과 Feature Bundle로 게임을 만들고, 고급 개발자는 TypeScript 기반 SandboxScript와 재사용 가능한 Component/System/Asset 패키지를 개발하며, 전문 팀은 저장소·리뷰·빌드·배포·호스팅을 한곳에서 관리한다.

## 2. 사용자 그룹과 핵심 과업
| 사용자 | 핵심 과업 | 기본 진입 경로 |
| --- | --- | --- |
| 플레이어 | 게임 검색, 공개·비밀 방 참가, 구매 게임 실행 | Sandbox Play |
| 비개발자 제작자 | 템플릿·Prefab·폼·Visual Trigger 조합 | Sandbox Editor |
| 스크립트 개발자 | TypeScript Component/System, 디버깅, 패키지 배포 | Sandbox SDK + Editor |
| 오픈소스 기여자 | Fork, Issue, Pull Request, Release, 의존성 갱신 | Sandbox Hub |
| 스튜디오 | 비공개 프로젝트, 리뷰·빌드·배포 승인, 팀 권한 | Sandbox Business |
| 서버 운영자 | Self-Hosting, 등록·모니터링, 유료 상시 호스팅 | Sandbox Cloud |
| 에셋 창작자 | Sprite/모델/음원/Prefab/Script 라이선스 판매 | Marketplace |

## 3. 제품 원칙
1. **무료로 제작·공유·학습**: 공개/비공개 개인 프로젝트, 공개·코드방 검색, 기본 협업 및 합리적 한도 내 무료 세션.
2. **구독은 비용과 전문 가치에 연결**: 상시 서버 자원 예약 및 고급 팀 자동화·감사·보안.
3. **게임 실행과 배포 위치 분리**: 동일 GamePackage를 공식 호스팅과 개발자 자체 서버에서 실행.
4. **Native 기반 성능 + UGC 격리**: 성능 민감 시스템 C++23, 사용자 규칙은 검증된 Script/데이터로 확장.
5. **개발자의 창작 자산 존중**: 명시적 라이선스, 이식성, 저장 데이터 반출, 사용자 콘텐츠 소유권.
6. **버전별 독립 완료 기준**: 모든 마일스톤은 작동하는 세로 슬라이스와 자동화 검증으로 종료.

## 4. 명시적인 초기 비목표
- GCP/AWS에 맞서는 범용 IaaS, 자체 데이터센터 구축.
- 초기부터 GitHub의 모든 기능을 재구현하거나 모든 Git 충돌을 해결하는 범용 VCS.
- 서드파티 Native DLL을 무제한 허용하는 UGC 모델.
- 엔진에 장르별 게임 로직을 전부 내장하는 구조.
- 처음부터 독자 JIT/AOT·모든 플랫폼·공식 글로벌 CDN·대규모 자동 확장을 동시에 달성.

## 5. 제품 북극성 지표 (초기 제안)
- 제작자가 프로젝트 생성부터 **첫 게임 실행**까지 도달한 비율.
- 외부 패키지 설치 후 **빌드 성공률**, 재현 가능한 잠금 파일 생성률.
- 다른 사용자가 제작자의 게임을 다운로드/실행한 비율.
- 서버 매칭·접속 성공률과 상시 서버의 가용성.
- 30 TPS 시뮬레이션의 p95/p99 Tick 처리시간.
- Marketplace의 결제 실패·환불·권리 침해 신고 대응 시간.

## 6. 제품 구조
```text
SandboxEditor / SandboxClient
        │
  SandboxGamePackage + SandboxScript SDK
        │
 ┌──────┴────────┐
 Sandbox Hub    Sandbox Cloud
 Repository     Directory/Room/Match/Hosting
 Package Registry & Marketplace
        │
     SandboxServer (C++23, Self-host 또는 관리형)
```

## 7. 교차 문서
- `architecture/01_SYSTEM_ARCHITECTURE.md`: 구현 경계.
- `architecture/05_PACKAGE_MANAGER.md`: 개발자 생태계 중심.
- `architecture/08_NETWORK_AND_ROOMS.md`: 게임 접속.
- `roadmap/`: v0.1~v1.0 완료 기준.
