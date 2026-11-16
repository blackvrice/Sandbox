#!/usr/bin/env python3
"""로드맵 보기 문서 생성 (docs/development/DOCUMENTATION_GUIDE.md 4장).

정본 (손으로 고치는 곳) 은 그대로다 — 이 도구는 거기서 읽어 "보는 문서" 의 생성 구간만 다시 쓴다:

  읽는 것  docs/roadmap/task-registry.md   태스크 · 상태 · 목표 버전 · 선행 · 완료 기준
           docs/roadmap/releases.md        버전 제목 · 목표 · 완료 기준 (원문 그대로 싣는다)
           docs/roadmap/versioning.md      성숙 단계의 첫 버전
           docs/qa/MANUAL-QA.md            "[ ] (v0.N …)" · "[x] (v0.N …)" 줄 수
           docs/releases/v0.N.0.md         있으면 그 버전은 출시됨
           CMakeLists.txt                  제품 버전 (project VERSION)
  쓰는 것  docs/roadmap/MASTER_ROADMAP.md      생성 구간 (버전 표 · 알파 · GA 까지 남은 것)
           docs/roadmap/versions/v0.N.md       생성 구간 (작업 요약 · 체크리스트 · 미완료 · 완료 조건) — 구간 아래 손으로 쓴 세부는 그대로
           docs/overview/CURRENT_STATUS.md     생성 구간 (버전 · 진행 숫자)

  gen_roadmap.py --root <저장소 루트>            다시 쓴다
  gen_roadmap.py --root <저장소 루트> --check    바뀔 것이 있거나 세부 체크리스트가 등록부와 어긋나면 종료 코드 1 (CTest docs_roadmap_generated)

상태 아이콘 (DOCUMENTATION_GUIDE 2장): Done ✅ · Review ⚠️ · Planned · Proposed ⬜ (세부 체크리스트에 [x] 가 있으면 🟡) · Missing 🔴.
버전은 정수로 비교한다 — v0.10 > v0.9 (ADR-0096).
"""
import argparse
import os
import re
import sys

BEGIN = '<!-- GENERATED:BEGIN — tools/gen_roadmap.py 가 쓴다. 이 구간을 손으로 고치지 않는다 (정본: task-registry · releases) -->'
END = '<!-- GENERATED:END -->'
TASK = re.compile(r'(?<![\w.-])((?:M[1-9]-T\d+)|(?:RT-0\d-\d+)|(?:ST-0\d-\d+)|(?:PS-\d\d-\d+)|(?:RH-\d\d-\d+)|(?:NG-\d\d)|(?:UI-\d))(?![\w])')
STAGES = [  # versioning 2장 — 성숙 단계의 첫 버전 (지금 계획)
    ('v0.5', 'Technical Alpha'), ('v0.13', 'Closed Creator Alpha'), ('v0.15', 'Public Creator Alpha'),
    ('v0.17', 'Closed Beta'), ('v0.21', 'Public Beta 후보'), ('v0.24', 'Release Candidate'), ('v1.0', 'General Availability'),
]
ICON = {'Done': '✅ 완료', 'Review': '⚠️ 확인 필요', 'Planned': '⬜ 미착수', 'Proposed': '⬜ 미착수', 'Missing': '🔴 차단 (정의 필요)'}


def read(root, rel):
    with open(os.path.join(root, rel), encoding='utf-8') as fh:
        return fh.read()


def vkey(text):
    m = re.search(r'v(\d+)\.(\d+|x)', text)
    return (int(m.group(1)), 999 if m.group(2) == 'x' else int(m.group(2))) if m else None


def vname(key):
    return f'v{key[0]}.{key[1]}'


def load_registry(root):
    tasks = []
    for line in read(root, 'docs/roadmap/task-registry.md').splitlines():
        if line.startswith('## 2'):
            break
        if not line.startswith('| ') or line.startswith('| ID') or line.startswith('|--'):
            continue
        c = [x.strip() for x in line.strip().strip('|').split('|')]
        if len(c) < 9:
            continue
        tasks.append({'id': c[0], 'title': c[1], 'track': c[2], 'status': c[3], 'origin': c[4], 'target': vkey(c[5]),
                      'p': c[6], 'deps': c[7], 'done_when': ' | '.join(c[8:])})
    return tasks


def load_releases(root):
    """버전 → {title, body (그 절의 text 블록 원문), goal}"""
    out = {}
    cur = None
    in_code = False
    goal_open = False
    for line in read(root, 'docs/roadmap/releases.md').splitlines():
        if line.startswith('## v'):
            key = vkey(line)
            title = line[3:].split('—', 1)[1].strip() if '—' in line else ''
            title = re.sub(r'\s*✅.*$', '', title).strip()
            cur = out[key] = {'title': title, 'body': [], 'goal': ''}
            in_code = False
            continue
        if cur is None:
            continue
        if line.startswith('```'):
            in_code = not in_code
            continue
        if in_code:
            cur['body'].append(line)
            if line.startswith('목표') and not cur['goal']:
                cur['goal'] = line[2:].strip()
                goal_open = True
            elif goal_open and line.startswith(' ') and line.strip():
                cur['goal'] += ' ' + line.strip()
            else:
                goal_open = False
    return out


def qa_counts(root):
    """MANUAL-QA 의 버전별 (안 한 줄, 한 줄). 줄 머리의 (v0.N …) 이 먼저, 없으면 그 절 제목의 v0.N"""
    counts = {}
    section = None
    for line in read(root, 'docs/qa/MANUAL-QA.md').splitlines():
        if line.startswith('## '):
            section = vkey(line) if re.match(r'## v\d', line) else None
            continue
        m = re.match(r'\[( |x)\] (\(v\d+\.\d+)?', line)
        if not m:
            continue
        key = vkey(m.group(2)) if m.group(2) else section
        if key is None:
            continue
        todo, done = counts.get(key, (0, 0))
        counts[key] = (todo + (m.group(1) == ' '), done + (m.group(1) == 'x'))
    return counts


def product_version(root):
    m = re.search(r'^\s*VERSION\s+(\d+\.\d+\.\d+)', read(root, 'CMakeLists.txt'), re.M)
    return m.group(1) if m else '?'


def detail_progress(tail):
    """손으로 쓴 세부 체크리스트: '### <ID> …' 절마다 ([x] 수, [ ] 수)"""
    out = {}
    cur = None
    for line in tail.splitlines():
        if line.startswith('#'):
            m = TASK.search(line) if line.startswith('### ') else None
            cur = m.group(1) if m else None
            if cur:
                out.setdefault(cur, [0, 0])
            continue
        if cur:
            m = re.match(r'\s*- \[( |x)\]', line)
            if m:
                out[cur][0 if m.group(1) == 'x' else 1] += 1
    return out


def split_doc(text):
    """(머리, 꼬리) — 생성 구간 앞 · 뒤. 구간이 없으면 None"""
    i, j = text.find(BEGIN), text.find(END)
    if i < 0 or j < 0:
        return None
    return text[:i], text[j + len(END):]


def pct(n, d):
    return '집계 불가' if d == 0 else f'{round(100 * n / d)} %'


class Model:
    def __init__(self, root):
        self.root = root
        self.tasks = load_registry(root)
        self.by_id = {t['id']: t for t in self.tasks}
        self.releases = load_releases(root)
        self.qa = qa_counts(root)
        self.product = product_version(root)
        self.versions = sorted(k for k in self.releases if k[1] != 999)
        self.details = {}   # 버전 → 세부 체크리스트 진행
        self.problems = []
        for key in self.versions:
            rel = f'docs/roadmap/versions/{vname(key)}.md'
            if os.path.exists(os.path.join(root, rel)):
                parts = split_doc(read(root, rel))
                if parts:
                    self.details[key] = detail_progress(parts[1])
        for key, det in self.details.items():
            for tid, (done, todo) in det.items():
                t = self.by_id.get(tid)
                if t is None:
                    self.problems.append(f'{vname(key)}: 세부 체크리스트의 {tid} 가 등록부에 없다')
                elif t['target'] != key:
                    self.problems.append(f'{vname(key)}: 세부 체크리스트의 {tid} 는 등록부 목표가 {vname(t["target"])}')
                elif t['status'] == 'Done' and todo:
                    self.problems.append(f'{vname(key)}: {tid} 는 등록부 Done 인데 세부 체크리스트에 [ ] 가 {todo} 개')
                elif t['status'] != 'Done' and done and not todo:
                    self.problems.append(f'{vname(key)}: {tid} 의 세부 체크리스트가 모두 [x] 인데 등록부는 {t["status"]}')

    def of(self, key):
        return [t for t in self.tasks if t['target'] == key]

    def released(self, key):
        return os.path.exists(os.path.join(self.root, f'docs/releases/v{key[0]}.{key[1]}.0.md'))

    def icon(self, t):
        if t['status'] in ('Planned', 'Proposed'):
            done, _ = self.details.get(t['target'], {}).get(t['id'], (0, 0))
            if done:
                return '🟡 진행 중'
        return ICON.get(t['status'], '⚠️ 확인 필요')

    def blocked(self, t):
        """끝나지 않은 태스크를 막는 것 — 끝나지 않은 선행 · 정의 없음"""
        if t['status'] == 'Missing':
            return '🔴 정의 필요'
        waiting = [d for d in TASK.findall(t['deps']) if d in self.by_id and self.by_id[d]['status'] != 'Done']
        if waiting:
            return '🔴 선행 대기 — ' + ' · '.join(waiting)
        if t['status'] == 'Review':
            return '아니오 (판정 대기)'
        return '아니오'

    def stats(self, key):
        ts = self.of(key)
        n = len(ts)
        done = sum(t['status'] == 'Done' for t in ts)
        review = sum(t['status'] == 'Review' for t in ts)
        design = sum(t['status'] in ('Done', 'Review', 'Planned') for t in ts)
        return n, done, review, design

    def version_status(self, key):
        n, done, review, _ = self.stats(key)
        if self.released(key):
            return f'✅ 완료 ({key[0]}.{key[1]}.0)'
        if n == 0:
            return '⬜ 미착수 (태스크 없음)'
        if done == n:
            return '🟡 태스크 끝 · 출시 판정 대기'
        if done and done + review == n:
            return '🟡 판정 대기 (태스크 ' + str(review) + ' 개 확인 필요)'
        if done or any(self.icon(t).startswith('🟡') for t in self.of(key)):
            return '🟡 진행 중'
        return '⬜ 미착수'

    def stage(self, key):
        return next((s for v, s in STAGES if vkey(v) == key), '')

    def qa_cell(self, key):
        todo, done = self.qa.get(key, (0, 0))
        return '—' if todo + done == 0 else f'{done} / {todo + done}'

    def developing(self):
        """지금 개발 중인 버전 — 시작하지 않은 (Planned · Proposed · Missing) 태스크가 남은 첫 버전"""
        for key in self.versions:
            if any(t['status'] in ('Planned', 'Proposed', 'Missing') for t in self.of(key)):
                return key
        return None

    def waiting_release(self):
        return [k for k in self.versions if not self.released(k) and self.stats(k)[1] and self.stats(k)[1] + self.stats(k)[2] == self.stats(k)[0]]

    def last_released(self):
        rel = [k for k in self.versions if self.released(k)]
        return rel[-1] if rel else None


def gen_version(m, key):
    rel = m.releases[key]
    ts = m.of(key)
    n, done, review, design = m.stats(key)
    todo_qa, done_qa = m.qa.get(key, (0, 0))
    name = vname(key)
    L = [BEGIN, '']
    L += ['| 항목 | 내용 |', '|---|---|',
          f'| 목표 | {rel["goal"] or rel["title"] or "—"} |',
          f'| 성숙 단계 | {m.stage(key) or "—"} |',
          f'| 상태 | {m.version_status(key)} |',
          f'| 진행률 (구현 · 자동 검증 = Done / 전체) | {done} / {n} — {pct(done, n)} |',
          f'| 판정 대기 (Review) · 설계 완료율 (카드 있음) | {review} 개 · {pct(design, n)} |',
          f'| 수동 QA (창 · GPU — [MANUAL-QA](../../qa/MANUAL-QA.md)) | {"줄 없음" if todo_qa + done_qa == 0 else f"{done_qa} / {todo_qa + done_qa} 확인"} |',
          f'| 릴리스 노트 | {f"[v{key[0]}.{key[1]}.0](../../releases/v{key[0]}.{key[1]}.0.md)" if m.released(key) else "없음 (출시 전)"} |',
          f'| 출시 기준 | [release-gates](../release-gates.md) 2장의 {name} 행 |', '']
    L += ['## 작업 요약', '']
    if ts:
        L += ['| ID | 기능 | 우선순위 | 상태 | 트랙 | 선행 |', '|---|---|---|---|---|---|']
        L += [f'| {t["id"]} | {t["title"]} | {t["p"]} | {m.icon(t)} | {t["track"]} | {t["deps"]} |' for t in ts]
    else:
        L += ['등록부에 이 버전을 목표로 한 태스크가 없다.']
    L += ['', '## 작업 체크리스트', '',
          '태스크 하나가 한 줄이다 — `[x]` 는 등록부 Done (완료 기준의 시험 · 기록이 있다). 근거는 줄 끝의 완료 기준.', '']
    L += [f'- [{"x" if t["status"] == "Done" else " "}] **{t["id"]}** {t["title"]} — {m.icon(t)} · 완료 기준: {t["done_when"]}' for t in ts]
    L += ['', '## 미완료 작업', '']
    open_ts = [t for t in ts if t['status'] != 'Done']
    if open_ts:
        L += ['| 작업 ID | 남은 작업 (완료 기준) | 우선순위 | 선행 작업 | 차단 여부 |', '|---|---|---|---|---|']
        L += [f'| {t["id"]} | {t["title"]} — {t["done_when"]} | {t["p"]} | {t["deps"]} | {m.blocked(t)} |' for t in open_ts]
    else:
        L += ['없음 — 이 버전의 태스크는 모두 등록부 Done.' if ts else '없음.']
    L += ['', '## 버전 완료 조건', '']
    integ = [t for t in ts if '통합 시험' in t['title']]
    box = lambda ok: '[x]' if ok else '[ ]'
    L += [f'- {box(bool(ts) and done == n)} 이 버전의 태스크가 모두 Done ({done} / {n})']
    for t in integ:
        L += [f'- {box(t["status"] == "Done")} 통합 시험 {t["id"]} 통과']
    if todo_qa + done_qa:
        L += [f'- {box(todo_qa == 0)} 수동 QA 줄 확인 ({done_qa} / {todo_qa + done_qa}) — 증거는 [QA-EVIDENCE](../../qa/QA-EVIDENCE.md)']
    L += [f'- {box(m.released(key))} 출시 기준 판정 · 릴리스 노트 (`docs/releases/v{key[0]}.{key[1]}.0.md`) — 기능 · 데이터 · 안정성 · 보안 · 성능 · 호환 여섯 칸',
          f'- {box(m.released(key))} 제품 버전 {key[0]}.{key[1]}.0 (CMake `project VERSION` — 지금 {m.product})', '']
    if rel['body']:
        L += ['### 목표 · 완료 기준 원문 ([releases](../releases.md) 에서 그대로)', '', '```text'] + rel['body'] + ['```', '']
    L += [END]
    return '\n'.join(L)


def remaining_table(m, upto):
    L = ['| 버전 | 남은 태스크 | 그중 P0 | 남은 것 |', '|---|---:|---:|---|']
    total = 0
    for key in m.versions:
        if key > upto:
            break
        left = [t for t in m.of(key) if t['status'] != 'Done']
        if not left:
            continue
        total += len(left)
        p0 = sum(t['p'] == 'P0' for t in left)
        L += [f'| [{vname(key)}](versions/{vname(key)}.md) | {len(left)} | {p0} | {" · ".join(t["id"] for t in left)} |']
    L += [f'| **합계** | **{total}** | | |']
    return L


def gen_master(m):
    L = [BEGIN, '', '## 2. 버전 표 (v0.5 ~ v1.0 — 태스크 등록부에서 집계)', '',
         '| 버전 | 핵심 목표 | 성숙 단계 | 완료 | 판정 대기 | 전체 | 진행률 | 설계 | 수동 QA | 상태 |',
         '|---|---|---|---:|---:|---:|---:|---:|---:|---|']
    for key in m.versions:
        n, done, review, design = m.stats(key)
        L += [f'| [{vname(key)}](versions/{vname(key)}.md) | {m.releases[key]["title"] or "—"} | {m.stage(key)} | {done} | {review} | {n} | {pct(done, n)} | '
              f'{pct(design, n)} | {m.qa_cell(key)} | {m.version_status(key)} |']
    n = len(m.tasks)
    done = sum(t['status'] == 'Done' for t in m.tasks)
    review = sum(t['status'] == 'Review' for t in m.tasks)
    design = sum(t['status'] in ('Done', 'Review', 'Planned') for t in m.tasks)
    L += [f'| **합계** | | | **{done}** | {review} | **{n}** | **{pct(done, n)}** | {pct(design, n)} | | |', '']
    alpha = vkey('v0.13')
    L += ['## 3. Closed Creator Alpha (v0.13) 까지 남은 작업', ''] + remaining_table(m, alpha) + ['']
    L += ['## 4. v1.0 (GA) 까지 남은 작업', ''] + remaining_table(m, vkey('v1.0')) + ['', END]
    return '\n'.join(L)


def gen_status(m):
    dev = m.developing()
    last = m.last_released()
    waiting = m.waiting_release()
    n = len(m.tasks)
    done = sum(t['status'] == 'Done' for t in m.tasks)
    link = lambda k: f'[{vname(k)}](../roadmap/versions/{vname(k)}.md)'
    nxt = next((k for k in m.versions if dev and k > dev), None)
    L = [BEGIN, '', '| 항목 | 현재 상태 (등록부 · CMake · MANUAL-QA 에서 집계) |', '|---|---|',
         f'| 제품 버전 (CMake `project VERSION`) | {m.product} |',
         f'| 최근 완료 (출시) 버전 | {link(last) if last else "—"} — {m.releases[last]["title"] if last else ""} |',
         '| 태스크는 끝났고 출시 판정을 기다리는 버전 | ' + (' · '.join(
             f'{link(k)} ({m.stats(k)[1]} / {m.stats(k)[0]} Done, 수동 QA {m.qa_cell(k)})' for k in waiting) or '없음') + ' |']
    if dev:
        dn, dd, _, _ = m.stats(dev)
        L += [f'| 현재 개발 버전 | {link(dev)} — {m.releases[dev]["title"]} ({dd} / {dn}, {pct(dd, dn)}) |']
    if nxt:
        L += [f'| 다음 목표 버전 | {link(nxt)} — {m.releases[nxt]["title"]} |']
    todo_qa = sum(v[0] for v in m.qa.values())
    done_qa = sum(v[1] for v in m.qa.values())
    alpha_left = sum(1 for t in m.tasks if t['target'] <= vkey('v0.13') and t['status'] != 'Done')
    L += [f'| 전체 진행 (v0.5 ~ v1.0 태스크) | {done} / {n} Done — {pct(done, n)} |',
          f'| Closed Creator Alpha (v0.13) 까지 남은 태스크 | {alpha_left} |',
          f'| v1.0 (GA) 까지 남은 태스크 | {n - done} |',
          f'| 수동 QA (v0.7 이후 버전 줄) | {done_qa} / {todo_qa + done_qa} 확인 |', '', END]
    return '\n'.join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    m = Model(root)
    outputs = {'docs/roadmap/MASTER_ROADMAP.md': gen_master(m), 'docs/overview/CURRENT_STATUS.md': gen_status(m)}
    for key in m.versions:
        outputs[f'docs/roadmap/versions/{vname(key)}.md'] = gen_version(m, key)

    stale = []
    for rel, block in outputs.items():
        path = os.path.join(root, rel)
        if os.path.exists(path):
            old = read(root, rel)
            parts = split_doc(old)
            if parts is None:
                m.problems.append(f'{rel}: 생성 구간 표시 (GENERATED:BEGIN · END) 가 없다')
                continue
            new = parts[0] + block + parts[1]
        else:
            key = vkey(os.path.basename(rel))
            old = None
            new = (f'# {vname(key)} — {m.releases[key]["title"] or "버전 계획"}\n\n'
                   f'> 버전 작업 문서. 위 구간은 [task-registry](../task-registry.md) · [releases](../releases.md) 에서 생성한다 '
                   f'([문서 가이드](../../development/DOCUMENTATION_GUIDE.md) 4장). 전체 표는 [MASTER_ROADMAP](../MASTER_ROADMAP.md).\n\n'
                   + block + '\n') if key else None
            if new is None:
                m.problems.append(f'{rel}: 파일이 없다 (생성 구간 표시를 넣은 문서를 먼저 만든다)')
                continue
        if new != old:
            stale.append(rel)
            if not args.check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, 'w', encoding='utf-8', newline='\n') as fh:
                    fh.write(new)

    for p in m.problems:
        print('gen_roadmap: ' + p)
    if args.check:
        for rel in stale:
            print(f'gen_roadmap: {rel} 가 정본과 다르다 — python tools/gen_roadmap.py --root . 로 다시 만든다')
        print(f'gen_roadmap: {len(outputs)} 문서, 낡은 것 {len(stale)}, 문제 {len(m.problems)}')
        return 1 if stale or m.problems else 0
    print(f'gen_roadmap: {len(outputs)} 문서 중 {len(stale)} 개를 다시 썼다, 문제 {len(m.problems)}')
    return 1 if m.problems else 0


if __name__ == '__main__':
    sys.exit(main())
