# check_docs 자체 시험 픽스처

tools/check_docs.py --root tests/docs/fixtures --expect 9 — 일부러 틀린 작은 저장소. 검사마다 기대하는 문제:
L 1 (README 의 없는 링크) · A 1 (없는 ADR 0002 번) · T 2 (README 의 NG-99 · releases 의 NG-98) · S 1 (Done M1-T1 이 [계획] 줄) ·
V 1 (M1-T4 목표 v0.99) · D 1 (M1-T3 의 선행 M1-T2 가 더 뒤) · R 2 (M1-T2 가 두 절 · NG-98 이 등록부에 없음).
