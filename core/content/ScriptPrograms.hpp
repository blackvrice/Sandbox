#pragma once
// 콘텐츠의 스크립트 바이트코드 — 팩마다 모듈 하나 (ADR-0058), 로드 0 단계가 만든다 (v0.3 M4-T1, ADR-0062).
// ContentDatabase::scriptPrograms 가 나눈다 (오버레이를 적용한 DB 도 같은 것 — 스크립트는 팩 원본에서만 온다).
// 시스템 실행 순서 = packs 의 차례 (팩 로드 순서) → 모듈의 systems 차례 (id 오름차순).

#include <string>
#include <vector>

#include "script/vm/Bytecode.hpp"

namespace sbx::content {

struct ScriptPackProgram {
    std::string pack;   // 팩 id
    std::string folder; // 진단 · 로그 ("<폴더>/scripts/a.ts")
    script::bc::Module module;
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
};

} // namespace sbx::content
