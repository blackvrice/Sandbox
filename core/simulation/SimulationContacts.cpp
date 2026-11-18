// 단계 16a 물리 이벤트 (v0.11 RT-06-5, ADR-0151, docs/22-RUNTIME-V2.md 5장 · 03 2.x) — SimulationWorld 의 일부.
//
//   이번 쌍   Collision (16) 이 밀기 전 위치로 모은 겹침 (layer · mask 를 지난 것, (a, b) 차례). 개체마다 kMaxContacts
//   까지 —
//             상대 saveId 가 작은 차례로, 쌍은 양쪽이 다 남길 때만 (어느 쪽에서 보아도 같은 쌍 집합)
//   지난 쌍   core.contacts (양쪽 개체에 상대 saveId · 트리거 비트) — 시뮬레이션 상태라 세이브 · 해시에 든다. 상대가
//   죽었으면
//             남은 쪽의 목록에만 있다 → 그 쌍은 이번 틱에 Exit
//   바뀐 쌍   (a, b) 차례로 지난 것에만 있으면 Exit, 이번 것에만 있으면 Enter (트리거 비트는 그 쌍의 것 — 겹친 채로
//   트리거가
//             바뀌면 이벤트 없이 비트만 고친다). 틱 상한 kMaxContactChangesPerTick — 넘으면 뒤를 버리고 센다 (상태는
//             그대로 고친다)
//   상태      core.contacts 를 이번 쌍대로 고쳐 쓴다 (바뀐 개체만 — 없으면 뗀다)
// 구독 (server · client 의 collision · trigger 종류) 이 없으면 이 단계도 상태도 없다 — 골든 그대로.

#include <algorithm>
#include <map>
#include <vector>

#include "core/components/core/Contacts.hpp"
#include "core/components/core/Identity.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "script/api/SystemEvents.hpp"

namespace sbx::sim {

bool SimulationWorld::wantsContacts() const noexcept {
    using script::api::GameEventKind;
    return wantsEvent(static_cast<u8>(GameEventKind::CollisionEnter)) ||
           wantsEvent(static_cast<u8>(GameEventKind::CollisionExit)) ||
           wantsEvent(static_cast<u8>(GameEventKind::TriggerEnter)) ||
           wantsEvent(static_cast<u8>(GameEventKind::TriggerExit));
}

void SimulationWorld::updateContacts() {
    const auto byPair = [](const ContactPair& x, const ContactPair& y) { return x.a != y.a ? x.a < y.a : x.b < y.b; };
    const auto samePair = [](const ContactPair& x, const ContactPair& y) { return x.a == y.a && x.b == y.b; };

    // 1) 이번 쌍 — 개체마다 상대가 kMaxContacts 를 넘으면 saveId 가 작은 상대부터, 양쪽이 다 남길 때만
    std::map<SaveId, std::vector<SaveId>> partners;
    for (const ContactPair& p : m_contactPairs) {
        partners[p.a].push_back(p.b);
        partners[p.b].push_back(p.a);
    }
    for (auto& [self, list] : partners) {
        std::sort(list.begin(), list.end());
    }
    const auto keeps = [&](SaveId self, SaveId other) {
        const std::vector<SaveId>& list = partners[self];
        return static_cast<usize>(std::lower_bound(list.begin(), list.end(), other) - list.begin()) <
               comp::kMaxContacts;
    };
    std::vector<ContactPair> current;
    current.reserve(m_contactPairs.size());
    for (const ContactPair& p : m_contactPairs) {
        if (keeps(p.a, p.b) && keeps(p.b, p.a)) {
            current.push_back(p);
        }
    }
    std::sort(current.begin(), current.end(), byPair);

    // 2) 지난 쌍 — core.contacts (양쪽에 있으면 둘 다 같은 쌍)
    std::vector<ContactPair> previous;
    for (auto [e, c, id] : m_registry.view<ecs::Read<comp::Contacts>, ecs::Read<comp::Persistence>>()) {
        (void)e;
        for (usize i = 0; i < c.others.size(); ++i) {
            const SaveId other = c.others[i];
            const bool trigger = ((static_cast<u32>(c.triggers) >> i) & 1u) != 0;
            previous.push_back(id.saveId < other ? ContactPair{id.saveId, other, trigger}
                                                 : ContactPair{other, id.saveId, trigger});
        }
    }
    std::sort(previous.begin(), previous.end(), byPair);
    previous.erase(std::unique(previous.begin(), previous.end(), samePair), previous.end());

    // 3) 바뀐 쌍 — (a, b) 차례
    const auto change = [&](bool enter, const ContactPair& p) {
        if (m_contactChanges.size() >= kMaxContactChangesPerTick) {
            ++m_contactChangesDropped;
            return;
        }
        m_contactChanges.push_back(ContactChange{enter, p.trigger, p.a, p.b});
    };
    usize i = 0;
    usize j = 0;
    while (i < previous.size() || j < current.size()) {
        if (j == current.size() || (i < previous.size() && byPair(previous[i], current[j]))) {
            change(false, previous[i++]);
        } else if (i == previous.size() || byPair(current[j], previous[i])) {
            change(true, current[j++]);
        } else {
            ++i; // 그대로 겹친다
            ++j;
        }
    }

    // 4) core.contacts 를 이번 쌍대로 — 개체마다 상대 오름차순 (current 가 (a, b) 차례라 a 쪽은 b 오름차순, b 쪽은 a
    // 오름차순)
    std::map<SaveId, comp::Contacts> want;
    for (const ContactPair& p : current) {
        for (const auto& [self, other] : {std::pair{p.a, p.b}, std::pair{p.b, p.a}}) {
            comp::Contacts& c = want[self];
            if (p.trigger) {
                c.triggers = static_cast<u16>(c.triggers | (1u << c.others.size()));
            }
            c.others.push_back(other);
        }
    }
    std::vector<ecs::EntityId> stale;
    for (auto [e, c, id] : m_registry.view<ecs::Read<comp::Contacts>, ecs::Read<comp::Persistence>>()) {
        (void)c;
        if (!want.contains(id.saveId)) {
            stale.push_back(e);
        }
    }
    for (const ecs::EntityId e : stale) {
        (void)m_registry.remove<comp::Contacts>(e);
    }
    for (const auto& [self, c] : want) {
        const ecs::EntityId e = m_saves.find(self);
        if (!m_registry.alive(e)) {
            continue;
        }
        const comp::Contacts* had = m_registry.tryRead<comp::Contacts>(e);
        if (had == nullptr) {
            m_registry.emplace<comp::Contacts>(e, c);
        } else if (!(had->triggers == c.triggers && had->others.size() == c.others.size() &&
                     std::equal(had->others.begin(), had->others.end(), c.others.begin()))) {
            m_registry.write<comp::Contacts>(e) = c;
        }
    }
}

} // namespace sbx::sim
