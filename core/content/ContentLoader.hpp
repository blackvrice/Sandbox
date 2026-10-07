#pragma once
// 콘텐츠 팩 로더 + 검증기. docs/11-CONTENT-SCHEMA.md 1·8장, docs/09-SERIALIZATION.md 7장.
//
//   content/<dir>/pack.json            {"id", "version", "requires", "description"}
//                 tags.json            ["animal", ...]
//                 terrain.json         [{"id", "moveCost", "flags"}, ...]
//                 prefabs/*.json       Prefab 하나 또는 배열
//                 rules/*.json         Rule 배열
//                 behaviors/*.json     BehaviorGraph 하나
//                 presentation/**      클라 전용 — 읽지 않고 contentHash 에서도 뺀다
//
// 로드 순서: 요청한 팩 + requires 를 위상 정렬 (동순위는 id), 팩 안의 파일은 경로 문자열 정렬 (04 4.5).
// 검증 규칙 V1~V7 (11 8장). 문제를 모두 모아 돌려준다 — 첫 오류에서 멈추지 않는다.
// 메시지 형식: "<팩 폴더>/<파일>:<JSON 포인터>: <규칙> — <설명>"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"

namespace sbx::content {

struct ContentIssue {
    enum class Severity : u8 { Error, Warning };
    Severity severity = Severity::Error;
    std::string rule;    // "V1" … "V7", "IO", "JSON"
    std::string file;    // "eco/prefabs/rabbit.json"
    std::string pointer; // "/components/life.energy/value"
    std::string message;

    [[nodiscard]] std::string describe() const;
};

struct ContentLoadResult {
    ContentDatabase db; // ok() 일 때만 쓸 수 있다
    std::vector<ContentIssue> issues;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] usize errorCount() const noexcept;
    [[nodiscard]] usize warningCount() const noexcept;
};

class ContentLoader {
public:
    // root 아래에서 packIds 와 그 requires 를 찾아 읽는다. 팩 폴더 이름은 무엇이든 되고, pack.json 의 id 로 찾는다.
    [[nodiscard]] static ContentLoadResult load(const std::filesystem::path& root, std::span<const std::string> packIds,
                                                const ecs::ComponentCatalog& catalog);
};

// 편의: 오류가 있으면 처음 몇 개를 묶은 Error 로
[[nodiscard]] Expected<ContentDatabase> loadContent(const std::filesystem::path& root,
                                                    std::span<const std::string> packIds,
                                                    const ecs::ComponentCatalog& catalog);

// 엔진이 관리하는 컴포넌트 — Prefab·명령이 직접 넣을 수 없다 (core.tags 는 Prefab 의 "tags" 로)
[[nodiscard]] bool isEngineManagedComponent(std::string_view name) noexcept;
// 서버가 몰라도 되는 컴포넌트 접두사 (Opaque 로 보관): render. client.
[[nodiscard]] bool isOpaqueAllowedComponent(std::string_view name) noexcept;

} // namespace sbx::content
