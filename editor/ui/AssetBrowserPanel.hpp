#pragma once
// "에셋" 창 — Asset Browser (v0.10 ST-06-3 — docs/26-ASSET-PIPELINE.md 5 · 7장 · 25 5장, ADR-0146). 프로젝트 assets/ 의
// 목록 (경로 · id · 크기 · 참조 수 · 썸네일 — PNG 를 64 픽셀로 줄여 ImGui 사용자 텍스처로), id 없는 원본 가져오기, 고른
// 에셋의 참조 (팩 materials.json), 옮기기 · 이름 바꾸기 (경로 참조를 고쳐 쓴다), 지우기 (참조가 있으면 확인 뒤 — 휴지통
// .sandbox/trash), 다시 읽기 (Hot Reload — 앱이 스프라이트를 다시 올린다), 끌기 원천 (payload kAssetPayload = 에셋 id —
// UI 디자이너의 image 가 받는다). 파일을 쓰는 일은 core/asset 의 함수 (AssetDatabase · AssetRefs) 를 부른다.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/asset/AssetRefs.hpp"

namespace sbx::editor {

inline constexpr const char* kAssetPayload = "SBX_ASSET"; // ImGui 끌어다 놓기 — 값 = 에셋 id (32 글자 + '\0')

// 걸러 보기 — 경로 · id 에 filter 가 들어 있는 것 (대소문자 무시 — ASCII), 경로 순
[[nodiscard]] std::vector<const asset::AssetEntry*> filterAssets(const asset::AssetScan& scan, std::string_view filter);
// 그 경로를 부르는 참조 수
[[nodiscard]] usize countReferences(const std::vector<asset::AssetRef>& refs, std::string_view path);
// 썸네일 크기 — 긴 변이 max (작으면 그대로), 비율 유지, 0 이 아니다
[[nodiscard]] std::array<u32, 2> thumbnailSize(u32 width, u32 height, u32 max);

class ThumbnailCache; // ImGui 사용자 텍스처 (AssetBrowserPanel.cpp)

struct AssetBrowserState {
    AssetBrowserState();
    ~AssetBrowserState();
    AssetBrowserState(const AssetBrowserState&) = delete;
    AssetBrowserState& operator=(const AssetBrowserState&) = delete;

    bool stale = true; // 다음 그리기에서 다시 훑는다
    std::filesystem::path project;
    asset::AssetScan scan;
    std::vector<asset::AssetRef> refs;
    std::string filter;
    std::string selected; // 경로
    std::string moveTo;
    std::string status;
    bool forceDelete = false;
    std::unique_ptr<ThumbnailCache> thumbs;
};

// 앱이 할 일 — 원본이 바뀌었다 (가져오기 · 옮기기 · 다시 읽기): 그 에셋의 스프라이트를 다시 올린다 (render
// AssetManager)
struct AssetChange {
    std::string id;
    std::string path;    // 지금 경로
    std::string oldPath; // 옮겼으면 옛 경로 (아니면 빈 글자)
};

std::optional<AssetChange> drawAssetBrowser(AssetBrowserState& state, const std::filesystem::path& projectDir);

} // namespace sbx::editor
