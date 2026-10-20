#include "core/trigger/TriggerCompiler.hpp"

#include <format>

namespace sbx::trigger {
namespace {

using content::TriggerStep;

struct Children {
    std::vector<u16> then;
    std::vector<u16> otherwise; // else
};

void emit(u16 node, std::span<const GraphNode> nodes, const std::vector<Children>& kids,
          std::vector<CompiledStep>& out) {
    const Children& c = kids[node];
    switch (nodes[node].category) {
    case NodeCategory::Event:
        for (const u16 k : c.then) {
            emit(k, nodes, kids, out);
        }
        return;
    case NodeCategory::Effect:
        out.push_back(CompiledStep{TriggerStep::Op::Effect, node, 0});
        for (const u16 k : c.then) {
            emit(k, nodes, kids, out);
        }
        return;
    case NodeCategory::Condition: {
        const usize cond = out.size();
        out.push_back(CompiledStep{TriggerStep::Op::Cond, node, 0});
        for (const u16 k : c.then) {
            emit(k, nodes, kids, out);
        }
        if (c.otherwise.empty()) {
            out[cond].jump = static_cast<u16>(out.size());
            return;
        }
        const usize jump = out.size();
        out.push_back(CompiledStep{TriggerStep::Op::Jump, node, 0});
        out[cond].jump = static_cast<u16>(out.size());
        for (const u16 k : c.otherwise) {
            emit(k, nodes, kids, out);
        }
        out[jump].jump = static_cast<u16>(out.size());
        return;
    }
    }
}

} // namespace

CompiledGraph compileGraph(std::span<const GraphNode> nodes, std::span<const GraphLink> links) {
    CompiledGraph g;
    const auto error = [&](GraphIssue::Where w, usize i, std::string m) {
        g.issues.push_back(GraphIssue{w, i, std::move(m), false});
    };
    if (nodes.empty()) {
        error(GraphIssue::Where::Graph, 0, "노드가 없다 (이벤트 노드가 하나 있어야 한다)");
        return g;
    }
    if (nodes.size() > content::kMaxTriggerNodes) {
        error(GraphIssue::Where::Graph, 0,
              std::format("노드 {} 개 — 트리거 하나에 {} 개까지", nodes.size(), content::kMaxTriggerNodes));
        return g;
    }
    // 이벤트 노드 — 하나
    usize events = 0;
    for (usize i = 0; i < nodes.size(); ++i) {
        if (nodes[i].category == NodeCategory::Event) {
            if (events++ == 0) {
                g.eventNode = static_cast<u16>(i);
            } else {
                error(GraphIssue::Where::Node, i, "이벤트 노드는 트리거에 하나뿐 (트리거를 나누십시오)");
            }
        }
    }
    if (events == 0) {
        error(GraphIssue::Where::Graph, 0, "이벤트 노드가 없다 (event.* 하나에서 시작한다)");
        return g;
    }

    // 연결 → 자식 목록 (연결 순서), 들어오는 수
    std::vector<Children> kids(nodes.size());
    std::vector<u32> indegree(nodes.size(), 0);
    for (usize l = 0; l < links.size(); ++l) {
        const GraphLink& k = links[l];
        if (k.from >= nodes.size() || k.to >= nodes.size()) {
            error(GraphIssue::Where::Link, l, "없는 노드를 잇는다");
            continue;
        }
        if (nodes[k.to].category == NodeCategory::Event) {
            error(GraphIssue::Where::Link, l, "이벤트 노드로 들어오는 연결은 쓸 수 없다");
            continue;
        }
        if (k.elseBranch && nodes[k.from].category != NodeCategory::Condition) {
            error(GraphIssue::Where::Link, l, "else 연결은 조건 노드에서만");
            continue;
        }
        if (k.from == k.to) {
            error(GraphIssue::Where::Link, l, "자기 자신으로 가는 연결 (순환)");
            continue;
        }
        (k.elseBranch ? kids[k.from].otherwise : kids[k.from].then).push_back(k.to);
        if (++indegree[k.to] == 2) {
            error(GraphIssue::Where::Node, k.to,
                  "들어오는 연결이 둘 이상 — 트리거는 나무다 (같은 노드를 두 길에서 쓰려면 복사하십시오)");
        }
    }
    if (!g.issues.empty()) {
        return g; // 아래 검사는 나무 모양을 가정한다
    }

    // 순환: 들어오는 연결이 하나뿐이면 순환은 이벤트에서 닿지 않는 고리로만 생긴다 — 닿지 않는 노드에서 부모를 따라가
    // 같은 노드를 다시 만나면 순환이다
    std::vector<i32> parent(nodes.size(), -1);
    for (usize i = 0; i < nodes.size(); ++i) {
        for (const u16 k : kids[i].then) {
            parent[k] = static_cast<i32>(i);
        }
        for (const u16 k : kids[i].otherwise) {
            parent[k] = static_cast<i32>(i);
        }
    }
    // 깊이 · 닿음 (이벤트에서 너비 우선)
    std::vector<u32> depth(nodes.size(), 0);
    std::vector<u16> order{g.eventNode};
    depth[g.eventNode] = 1;
    for (usize head = 0; head < order.size(); ++head) {
        const u16 n = order[head];
        for (const auto* list : {&kids[n].then, &kids[n].otherwise}) {
            for (const u16 k : *list) {
                depth[k] = depth[n] + 1;
                order.push_back(k);
            }
        }
    }
    std::vector<bool> cycleReported(nodes.size(), false);
    for (usize i = 0; i < nodes.size(); ++i) {
        if (depth[i] != 0 || cycleReported[i]) {
            continue;
        }
        // 부모를 따라 올라간다 — 이벤트에 닿지 않고 처음 노드로 돌아오면 순환
        std::vector<bool> seen(nodes.size(), false);
        i32 at = static_cast<i32>(i);
        while (at >= 0 && !seen[static_cast<usize>(at)] && depth[static_cast<usize>(at)] == 0) {
            seen[static_cast<usize>(at)] = true;
            at = parent[static_cast<usize>(at)];
        }
        if (at >= 0 && seen[static_cast<usize>(at)]) {
            // at 이 고리 위의 노드 — 고리 전체를 한 번만 알린다
            i32 c = at;
            do {
                cycleReported[static_cast<usize>(c)] = true;
                c = parent[static_cast<usize>(c)];
            } while (c != at);
            error(GraphIssue::Where::Node, static_cast<usize>(at), "순환 — 연결이 되돌아온다");
        } else {
            error(GraphIssue::Where::Node, i, "이벤트 노드에서 닿지 않는다");
        }
    }
    if (!g.issues.empty()) {
        return g;
    }
    bool anyEffect = false;
    for (const u16 n : order) {
        if (depth[n] > content::kMaxTriggerDepth) {
            error(GraphIssue::Where::Node, n,
                  std::format("이벤트에서 {} 번째 노드 — 깊이는 {} 까지", depth[n], content::kMaxTriggerDepth));
            break;
        }
        if (nodes[n].needsSubject && !nodes[g.eventNode].givesSubject) {
            error(GraphIssue::Where::Node, n,
                  "대상이 필요한 노드 — 이 이벤트는 대상을 주지 않는다 (area_enter · death 만)");
        }
        anyEffect = anyEffect || nodes[n].category == NodeCategory::Effect;
    }
    if (!anyEffect) {
        g.issues.push_back(
            GraphIssue{GraphIssue::Where::Graph, 0, "닿는 효과 노드가 없다 — 아무것도 하지 않는다", true});
    }
    for (const GraphIssue& i : g.issues) {
        if (!i.warning) {
            return g;
        }
    }
    emit(g.eventNode, nodes, kids, g.steps);
    g.ok = true;
    return g;
}

} // namespace sbx::trigger
