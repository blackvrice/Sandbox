#!/usr/bin/env python3
"""문서 · 로드맵 일관성 검사 (docs/roadmap/README.md 5장, docs/audit/ROADMAP-CONSISTENCY.md).

  check_docs.py --root <저장소 루트>                 문제 0 건이면 종료 코드 0
  check_docs.py --root <픽스처> --expect N            정확히 N 건이면 0 (검사기 자체 시험 — tests/docs/fixtures)

검사:
  L  상대 링크 — docs/ 의 .md 와 루트 README · AGENTS 의 [..](경로) 가 실제 파일을 가리킨다
  A  ADR 참조 — "ADR-NNNN" 마다 docs/adr/NNNN-*.md 가 있다
  T  태스크 ID — 현행 계획 문서 (아래 PLAN_DOCS) 의 v0.5 이후 태스크 ID (M1-T1 · RT-06-3 · ST-06-1 · NG-01 …) 가
     docs/roadmap/task-registry.md 의 표 (또는 그 "옛 번호" 표) 에 있다. "v0.2 M1-T1" 처럼 옛 버전이 앞에 붙은 것은 건너뛴다
  V  목표 버전 — 태스크 등록부의 Target 이 버전 목록 (docs/roadmap/versioning.md 의 "버전 목록") 에 있다
  S  상태 — 등록부에서 Done 인 태스크가 현행 계획 문서에 "[계획]" 과 같은 줄로 남아 있지 않다 (같은 줄에 ID 와 [계획])
  D  선행 — 등록부의 선행 태스크의 목표 버전이 그 태스크의 목표보다 뒤가 아니다 (버전은 정수로 비교 — v0.10 > v0.9)
  R  배정 — 끝나지 않은 태스크는 docs/roadmap/releases.md 의 "## vX.Y" 절 하나의 "태스크" 줄에만 있고, 그 절의 버전 = 등록부 목표

알려진 예외는 KNOWN 에 이유와 함께 적는다 (기존 ADR 은 고치지 않는다 — AGENTS 6장).
"""
import argparse
import os
import re
import sys

LINK = re.compile(r'\]\(([^)#\s]+)(#[^)]*)?\)')
ADR = re.compile(r'ADR-(\d{4})')
TASK = re.compile(r'(?<![\w.-])((?:M[1-9]-T\d+)|(?:RT-0\d-\d+)|(?:ST-0\d-\d+)|(?:PS-\d\d-\d+)|(?:RH-\d\d-\d+)|(?:NG-\d\d))(?![\w])')
OLD_VERSION_PREFIX = re.compile(r'v0\.[1-4]\s*$')

# 현행 계획 문서 — 태스크 ID · 상태를 검사한다 (이력 · 동결 문서는 링크 · ADR 만)
PLAN_DOCS = [
    'docs/18-PLATFORM.md', 'docs/20-CREATOR-ALPHA.md', 'docs/22-RUNTIME-V2.md', 'docs/23-ROADMAP.md',
    'docs/24-SECURITY-GATES.md', 'docs/25-STUDIO-ARCHITECTURE.md', 'docs/26-ASSET-PIPELINE.md',
    'docs/27-SCRIPT-IDE.md', 'docs/28-CREATOR-UX-GATES.md', 'docs/roadmap/README.md', 'docs/roadmap/releases.md',
    'docs/roadmap/release-gates.md', 'docs/audit/SECOND-PASS-GAP-ANALYSIS.md', 'docs/audit/ALPHA-READINESS.md',
]

KNOWN = {
    # (검사, 파일, 대상) — 이유
    ('L', 'docs/adr/0085-script-standard-library-intrinsics-collections-callbacks.md',
     '0057-host-api-single-table-sdk-generation.md'):
        '기존 ADR 의 오타 링크 (바른 파일 0057-host-api-single-definition-cpp-table-sdk-generated.md) — ADR 은 고치지 않는다',
    ('T', 'docs/18-PLATFORM.md', 'M1-T5'):
        '18 7장은 v0.2 마일스톤별 문서 이력 (v0.2 M1-T5) — 줄 앞에 버전이 없다. 이력은 고치지 않는다',
}


def md_files(root):
    out = []
    for base, _, files in os.walk(os.path.join(root, 'docs')):
        for f in files:
            if f.endswith('.md'):
                out.append(os.path.relpath(os.path.join(base, f), root).replace('\\', '/'))
    for f in ('README.md', 'AGENTS.md'):
        if os.path.exists(os.path.join(root, f)):
            out.append(f)
    return sorted(out)


def read(root, rel):
    with open(os.path.join(root, rel), encoding='utf-8') as fh:
        return fh.read()


def registry(root):
    """등록부의 ID → (상태, 목표) 와 옛 번호 표의 ID 집합"""
    path = 'docs/roadmap/task-registry.md'
    if not os.path.exists(os.path.join(root, path)):
        return None, set()
    tasks = {}
    aliases = set()
    section = ''
    for line in read(root, path).splitlines():
        if line.startswith('## '):
            section = line
        if not line.startswith('| ') or line.startswith('| ID') or line.startswith('|--') or line.startswith('| 옛'):
            continue
        cells = [c.strip() for c in line.strip('|').split('|')]
        if '옛 번호' in section:
            aliases.add(cells[0].strip('`'))
            continue
        if len(cells) < 6:
            continue
        tasks[cells[0].strip('`')] = {'status': cells[3], 'target': cells[5], 'deps': cells[7] if len(cells) > 7 else ''}
    return tasks, aliases


def versions(root):
    path = 'docs/roadmap/versioning.md'
    if not os.path.exists(os.path.join(root, path)):
        return None
    text = read(root, path)
    i = text.find('버전 목록')
    return set(re.findall(r'`(v\d+\.\d+)`', text[i:] if i >= 0 else text))


def vkey(text):
    """'v0.10' → (0, 10), 'v1.x' → (1, 999). 정수 비교 (ADR-0096)"""
    m = re.search(r'v(\d+)\.(\d+|x)', text)
    if not m:
        return None
    return int(m.group(1)), 999 if m.group(2) == 'x' else int(m.group(2))


def release_sections(root):
    """releases.md 의 "## vX.Y" 절마다 "태스크" 줄 (과 들여 쓴 이어지는 줄) 의 ID 목록"""
    path = 'docs/roadmap/releases.md'
    if not os.path.exists(os.path.join(root, path)):
        return None
    out = {}
    version = None
    in_tasks = False
    for line in read(root, path).splitlines():
        if line.startswith('## '):
            version = vkey(line)
            in_tasks = False
            continue
        if version is None:
            continue
        if line.startswith('태스크'):
            in_tasks = True
        elif not line.startswith(' ') or not line.strip():
            in_tasks = False
        if in_tasks:
            for m in TASK.finditer(line):
                out.setdefault(m.group(1), []).append(version)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--expect', type=int, default=None, help='정확히 이 수의 문제를 기대 (자체 시험용)')
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    problems = []

    def problem(kind, rel, target, msg):
        if (kind, rel, target) in KNOWN:
            return
        problems.append(f'{kind} {rel}: {msg}')

    adr_ids = {f[:4] for f in os.listdir(os.path.join(root, 'docs', 'adr')) if re.match(r'\d{4}-', f)}
    files = md_files(root)
    for rel in files:
        text = read(root, rel)
        for m in LINK.finditer(text):
            target = m.group(1)
            if '://' in target or target.startswith('mailto:'):
                continue
            path = os.path.normpath(os.path.join(root, os.path.dirname(rel), target))
            if not os.path.exists(path):
                problem('L', rel, target, f'링크 대상이 없다 — {target}')
        for m in ADR.finditer(text):
            if m.group(1) not in adr_ids:
                problem('A', rel, m.group(1), f'ADR-{m.group(1)} 파일이 없다')

    tasks, aliases = registry(root)
    known_versions = versions(root)
    if tasks is None:
        problems.append('T docs/roadmap/task-registry.md 가 없다')
    else:
        if known_versions is not None:
            for tid, row in tasks.items():
                for v in re.findall(r'v\d+\.\d+', row['target']):
                    if v not in known_versions:
                        problems.append(f'V task-registry: {tid} 의 목표 {v} 가 버전 목록에 없다')
        for tid, row in tasks.items():
            mine = vkey(row['target'])
            for m in TASK.finditer(row['deps']):
                dep = tasks.get(m.group(1))
                if dep is None:
                    if m.group(1) not in aliases:
                        problems.append(f'D task-registry: {tid} 의 선행 {m.group(1)} 가 등록부에 없다')
                    continue
                theirs = vkey(dep['target'])
                if mine and theirs and theirs > mine:
                    problems.append(f'D task-registry: {tid} ({row["target"]}) 의 선행 {m.group(1)} 가 더 뒤 ({dep["target"]})')
        sections = release_sections(root)
        if sections is not None:
            for tid, row in tasks.items():
                if row['status'] == 'Done':
                    continue
                where = sections.get(tid, [])
                if len(where) != 1:
                    problems.append(f'R releases: {tid} 가 "태스크" 줄에 {len(where)} 번 (한 버전에만 있어야 한다)')
                elif where[0] != vkey(row['target']):
                    problems.append(f'R releases: {tid} 의 절 v{where[0][0]}.{where[0][1]} ≠ 등록부 목표 {row["target"]}')
            for tid in sections:
                if tid not in tasks:
                    problems.append(f'R releases: {tid} 가 등록부에 없다')
        for rel in PLAN_DOCS:
            if not os.path.exists(os.path.join(root, rel)):
                if args.expect is None:
                    problems.append(f'T {rel}: 계획 문서가 없다')
                continue
            for n, line in enumerate(read(root, rel).splitlines(), 1):
                for m in TASK.finditer(line):
                    tid = m.group(1)
                    if OLD_VERSION_PREFIX.search(line[:m.start()]):
                        continue  # "v0.3 M4-T2" — 옛 버전의 태스크
                    if tid not in tasks and tid not in aliases:
                        problem('T', rel, tid, f'{n}행: 태스크 {tid} 가 등록부에 없다')
                    elif tid in tasks and tasks[tid]['status'] == 'Done' and '[계획' in line and '✅' not in line:
                        problem('S', rel, tid, f'{n}행: {tid} 는 Done 인데 같은 줄이 [계획]')

    for p in problems:
        print(p)
    print(f'check_docs: {len(files)} files, {len(problems)} problem(s)')
    if args.expect is not None:
        if len(problems) != args.expect:
            print(f'selftest FAILED: expected {args.expect}, found {len(problems)}', file=sys.stderr)
            return 1
        return 0
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
