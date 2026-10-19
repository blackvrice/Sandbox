# 15. 비즈니스 모델 및 생태계 운영 설계

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 네 가지 수익원
1. **Sandbox Cloud Hosting**: 상시 서버 구독, 백업·저장 공간·고급 자원.
2. **Asset/Package Marketplace**: Sprite·사운드·UI·Prefab·Script·Feature 판매 수수료.
3. **Game Store**: 유료 싱글플레이·게임 이용권 판매 수수료.
4. **Pro / Business / Enterprise**: 조직 협업, 클라우드 빌드, 승인·감사·보안, SLA·지원 구독.

## 2. 무료 생태계 원칙
- 무료 계정의 공개/비공개 프로젝트와 기본 PR/Issue/패키지 설치.
- 오픈소스 패키지 공개/기여, 일반 공개·비밀 코드 방 검색/생성.
- 자체 호스팅의 Directory 등록은 무료(공정 사용 정책 적용).
- 무료 세션 서버는 자원 한도 명시; Relay/저장/CDN/CI는 원가를 추적.

## 3. 고객별 유료 전환 이유
| 사용자 | 전환 계기 | 판매 제품 |
| --- | --- | --- |
| 취미 제작자 | 월드 24시간 유지 | Persistent Hosting |
| 인기 게임 운영자 | CPU/메모리/백업 증가 | Hosting Plus |
| 독립 에셋 개발자 | 유료 콘텐츠 판매 | Marketplace 수수료 |
| 상용 게임 개발자 | 싱글게임 판매 | Game Store 수수료 |
| 인디 스튜디오 | 여러 팀·브랜치·검토·배포 | Business 구독 |
| 대형 조직 | 인증·감사·SLA·사내 정책 | Enterprise |

## 4. 기본 경제 모델
- Hosting contribution = 월 구독료 − 서버 CPU/RAM/전력 대체 비용 − bandwidth − storage/backup − payment/support.
- Marketplace contribution = 거래액 × 수수료율 − PSP fee − refund/chargeback − moderation/settlement.
- Business contribution = 구독료 − CI/build minutes − storage − support − security/compliance.
- 가격/수수료는 경쟁 플랫폼과 원가를 측정한 뒤 실험. 예시로 에셋 10~15%, 게임 5~10% 범위를 **가설**로만 사용.

## 5. 규칙
- 판매자와 원저작자 구분, 라이선스 명시, 사용권/재판매권 설명.
- 플랫폼 계정·호스팅·GamePackage는 결제 여부와 무관하게 정당한 데이터 내보내기를 보장.
- 판매/정산·세금·소비자보호 의무는 상용 출시 전에 지역별 법률 검토.
- 개발자에게 무제한 무료 클라우드 자원을 약속하지 않음.

## 6. 시장 진입
Phase A: 엔진/패키지 재사용 개발자 확보 → Phase B: Hub 오픈소스 협업 → Phase C: 무료 방/자체 서버 → Phase D: 초기에 검증된 호스팅·게임/에셋 거래 → Phase E: Business 판매. 초기에는 플랫폼 전체 복제 대신 Sandbox 전용 Game Package 및 게임 특화 리뷰·배포 UX에 집중.

## 7. 추적 지표
Creator 활성화, 첫 게임 빌드 성공률, 패키지 재사용률, PR 참여율, 7/30일 개발자 유지율, 무료 세션 원가, 호스팅 churn, GMV, 결제 후 환불률, Gross/Contribution Margin.
