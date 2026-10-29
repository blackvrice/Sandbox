#pragma once
// 콘텐츠의 스크립트 바이트코드 — 팩마다 모듈 하나 (ADR-0058), 로드 0 단계가 만든다 (v0.3 M4-T1, ADR-0062).
// ContentDatabase::scriptPrograms 가 나눈다 (오버레이를 적용한 DB 도 같은 것 — 스크립트는 팩 원본에서만 온다, 태그는
// 오버레이로 바뀌지 않는다). 시스템 실행 순서 = packs 의 차례 (팩 로드 순서) → 모듈의 systems 차례 (id 오름차순).

#include <string>
#include <vector>

#include "core/content/TagSet.hpp"
#include "script/vm/Bytecode.hpp"

namespace sbx::content {

// 시스템의 구독 하나를 콘텐츠로 푼 것 (v0.3 M4-T2, ADR-0063) — 태그 이름 → 태그 비트 (로드 1 단계 뒤, 없는 태그는 V7)
struct ScriptEventFilter {
    u8 kind = 0; // script::api::GameEventKind
    f32 minX = 0, minY = 0, maxX = 0, maxY = 0;
    TagExpr tags;
};

struct ScriptPackProgram {
    std::string pack;   // 팩 id
    std::string folder; // 진단 · 로그 ("<폴더>/scripts/a.ts")
    script::bc::Module module;
    std::vector<std::vector<ScriptEventFilter>> events{}; // module.systems 와 같은 차례 — 시스템마다 구독 (종류 차례)
};

struct ScriptPrograms {
    std::vector<ScriptPackProgram> packs; // 로드 순서, 스크립트가 있고 오류 없이 컴파일된 팩만

    [[nodiscard]] usize systemCount() const noexcept {
        usize n = 0;
        for (const ScriptPackProgram& p : packs) {
            n += p.module.systems.size();
        }
        return n;
    }
    // 도메인 하나의 시스템 수 — 서버 시뮬레이션은 Server, 클라이언트 표시는 Client 만 돌린다 (ADR-0067)
    [[nodiscard]] usize systemCount(script::api::HostDomain domain) const noexcept {
        usize n = 0;
        for (const ScriptPackProgram& p : packs) {
            for (const script::bc::SystemDesc& s : p.module.systems) {
                n += s.domain == domain ? 1 : 0;
            }
        }
        return n;
    }
};

} // namespace sbx::content
