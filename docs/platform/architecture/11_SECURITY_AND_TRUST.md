# 12. 보안·신뢰·권한·운영 정책

> **문서 상태:** 설계 초안 v0.1 | **기준일:** 2026-10-13 | **대상:** Sandbox C++23 기반 게임 제작·배포 플랫폼
> **범례:** [현행] GitHub `blackvrice/Sandbox`에서 확인한 구현, [신규] 새로 설계·구현할 기능, [결정 필요] 제품 정책 확정 전

## 1. 신뢰 경계
| 경계 | 신뢰 수준 | 핵심 통제 |
| --- | --- | --- |
| SandboxScript/외부 패키지 | 불신 | Bytecode 검증·Fuel·Capability·격리 |
| Game Server ↔ Client | 불신 | 서버 권위, 인증·암호화, 메시지 검증 |
| Self-host ↔ Sandbox Cloud | 부분 신뢰 | 서버 등록 인증·Heartbeat 검증·폐기 |
| Marketplace 업로드 | 불신 | 악성 콘텐츠/라이선스 검사·검역 |
| 개발자 팀 | 역할·조직별 | 저장소 ACL, 승인·감사·비밀 관리 |
| 호스팅 Worker | 분리된 임차인 | 프로세스/컨테이너 격리, 자원 한도 |

## 2. Script Threat Model
- 무한 루프/이벤트 폭주/비정상 메모리 할당/대량 Spawn/반복 Query.
- Host Handle 위조, 타입/버전 불일치, 잘못된 Bytecode, ABI mismatch.
- 네이티브 코드 생성 시 검증 누락으로 인한 프로세스 메모리 손상.
- 직접 파일/소켓 접근, SSRF, 자격 증명 탈취 등.

## 3. 대책
- Verifier + 실행량 계측 + 허가된 함수 Registry + Native Host 검사.
- UGC는 기본적으로 파일/OS/Socket/FFI 접근 불허; 서버 측 명시적으로 승인된 HTTP 엔드포인트만 허용.
- 응답 크기·시간 제한·서킷 브레이커·내부망 주소/리다이렉트 방지·비밀 저장소 사용.
- 서버의 게임 상태 변경은 검증된 API와 명령 경로로만 처리.
- 런타임 모니터링, Abuse 신고, 패키지 격리, 계정·서버 차단 절차.

## 4. Network 보안
- [현행] ENet Transport는 게임용 신뢰성 통신이지만 암호화는 제공하지 않음.
- [신규] 공개 인터넷 전환 전 보안 Transport, Join Ticket, 버전 검증, rate-limit, DDoS 완화 필요.
- Open Room/Secret Room은 표시 정책과 접근 정책을 분리.
- Self-host 관리자와 플랫폼 계정 권한은 분리하고 인증 주체를 로깅.

## 5. 개발자 콘텐츠 보호
- 패키지 해시·소유권·서명·권한·버전 검증; Marketplace의 신고·분쟁 처리.
- 민감한 Server script·API Secret은 Client 번들에 포함되지 않음.
- 게임 구매자에게 서버 운영자/마켓 판매자 권한 제공 금지.

## 6. 정보보호·개인정보
- 데이터 최소 수집, 계정/서버/결제/로그의 보존 정책, 사용자 다운로드·삭제 요청 흐름 정의.
- 운영 로그에서 계정 토큰/비밀번호/서버 Secret 마스킹.
- 취약점 신고 창구, 중요 보안 업데이트, 사고 대응/롤백/패치 배포 표준화.

## 7. 검증
Fuzzing(Manifest/Bytecode/Network/Save), Host Permission Matrix, 악성 패키지 테스트, 격리 탈출 시도, Replay 동등성, 결제 위조/중복 이벤트 테스트 통과.
