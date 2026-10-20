#pragma once
// 게임 키 (v0.2 M3-T3, ADR-0049): 콘텐츠 팩이 presentation/input.json 으로 주는 키 바인딩 기본값.
//
//   팩 presentation/input.json   { "format": "sandbox.input", "version": 1,
//                                  "actions": { "play.action.eco.feed": ["Q"], "play.action.eco.trap": ["E"] } }
//
// "play.action.<PlayerAction id>" 를 누르면 커서 아래 월드 점에 그 행동을 쓴다 (Application). 팩 순서대로 합치고
// (뒤 팩이 이긴다), 마지막에 사용자 --input 파일의 play.* 가 덮어쓴다. play.* 가 아닌 이름은 무시하고 경고한다 —
// 팩이 엔진 키(카메라 · 에디터)를 바꾸지 못한다. 엔진 키와 같은 바인딩이면 경고만 한다 (둘 다 켜진다).
// 엔진 키(Application 의 ActionMap)와 따로 둔다: ActionId 가 이름 순 번호라 합치면 엔진 키의 번호가 바뀐다.

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "apps/client/WorldSession.hpp"
#include "platform/common/ActionMap.hpp"

namespace sbx::client {

inline constexpr std::string_view kPlayPrefix = "play.";
inline constexpr std::string_view kPlayActionPrefix = "play.action.";

struct PlayKeys {
    platform::ActionMap map;           // play.* 만
    std::vector<std::string> warnings; // 형식 오류 · 무시한 이름 · 엔진 키와 겹침
};

// user = 엔진 기본값 + 사용자 --input 을 합친 맵 (play.* 덮어쓰기 · 겹침 검사)
[[nodiscard]] PlayKeys buildPlayKeys(std::span<const PackInput> packs, const platform::ActionMap& user);

// PlayerAction 의 키 표시 ("Q", 여럿이면 "Q / F5", 없으면 빈 문자열)
[[nodiscard]] std::string playKeyLabel(const platform::ActionMap& map, std::string_view actionId);

} // namespace sbx::client
