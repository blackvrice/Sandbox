#pragma once
// CPU 쪽 RGBA8 이미지 + PNG 읽기·쓰기 + 비교. docs/06-RENDERING.md 7.3·14장.
// Phase 7A: sbx_render_tests 의 기준 이미지. Phase 8: AssetManager 의 텍스처 디코드(Worker)가 같은 loadPng 를 쓴다.

#include <filesystem>
#include <vector>

#include "foundation/types/Error.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::render {

struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba; // width * height * 4, 좌상단 원점, 행 우선

    [[nodiscard]] static Image filled(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a = 255);
    [[nodiscard]] const u8* pixel(u32 x, u32 y) const noexcept {
        return rgba.data() + (static_cast<usize>(y) * width + x) * 4;
    }
    [[nodiscard]] u8* pixel(u32 x, u32 y) noexcept { return rgba.data() + (static_cast<usize>(y) * width + x) * 4; }
    void setPixel(u32 x, u32 y, u8 r, u8 g, u8 b, u8 a = 255) noexcept;
};

[[nodiscard]] Expected<Image> loadPng(const std::filesystem::path& path);
[[nodiscard]] Expected<void> savePng(const std::filesystem::path& path, const Image& image);

struct ImageDiff {
    bool sameSize = false;
    u32 maxChannelDiff = 0; // 모든 픽셀·채널 중 가장 큰 차이 (0~255)
    u64 pixelsOverTolerance = 0;
    u64 totalPixels = 0;
};
// tolerance: 채널 차이가 이 값을 넘는 픽셀을 센다
[[nodiscard]] ImageDiff compareImages(const Image& expected, const Image& actual, u32 tolerance);
// 차이를 보이는 그림: 같으면 어둡게 비친 기대 이미지, 다르면 빨강 (실패 진단용)
[[nodiscard]] Image diffImage(const Image& expected, const Image& actual, u32 tolerance);

} // namespace sbx::render
