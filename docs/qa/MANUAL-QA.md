# 수동 QA 체크리스트

> L7 수동 QA ([13-TESTING](../13-TESTING.md)). 자동 테스트를 대신하지 않습니다. 마일스톤(Phase 완료) 때 수행하고
> 결과를 `DEVELOPMENT_LOG.md`에 "QA: 통과 n / 실패 m (항목)"으로 남깁니다.

## Phase 6 — 창·입력

```text
[ ] 창 생성·닫기·최소화·복원·최대화
[ ] 리사이즈 중 깨짐·멈춤 없음
[ ] 다른 DPI 모니터로 이동 시 UI 배율 갱신
[ ] 한글 IME 조합 입력 (ImGui 텍스트 필드)
[ ] Alt+Tab 후 키 끈적임 없음
[ ] 마우스 휠·가운데 버튼·더블클릭
```

## Phase 8 — 렌더러

```text
[ ] 리사이즈·최소화 후 렌더 복구, 디바이스 제거 없음
[ ] VSync on/off, frames-in-flight 2/3 전환
[ ] 카메라 팬·줌이 부드럽다 (60 FPS 이상)
[ ] --rhi-debug 실행 시 Debug Layer 경고 0
[ ] PIX 캡처 1회 성공
```

## Phase 10~12 — 네트워크·에디터

```text
[ ] 두 PC(LAN) 접속, 한쪽 편집이 다른 쪽에 0.5초 내 반영
[ ] 서버 재시작 없이 클라이언트 재접속 (토큰)
[ ] Observer 역할에서 편집 UI 비활성 + 서버 거절
[ ] Undo/Redo 10단계, 다른 사용자 편집과 섞였을 때 경고
[ ] 저장 → 서버 재시작 → 로드 → 동일 상태
[ ] 리플레이 재생이 라이브와 같아 보인다
```

## Phase 13·14 — 이식

```text
[ ] 같은 세이브를 Windows/Linux/macOS 에서 열었을 때 같은 화면 구성
[ ] Linux X11·Wayland 각각 입력·리사이즈
[ ] macOS Retina 배율, 메뉴 바, Cmd 단축키
```
