#include "render/asset/Image.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <stb_image.h>
#include <stb_image_write.h>
#include <string>

#include "foundation/io/FileIo.hpp"

namespace sbx::render {

Image Image::filled(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) {
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<usize>(w) * h * 4);
    for (usize i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r;
        img.rgba[i + 1] = g;
        img.rgba[i + 2] = b;
        img.rgba[i + 3] = a;
    }
    return img;
}

void Image::setPixel(u32 x, u32 y, u8 r, u8 g, u8 b, u8 a) noexcept {
    u8* p = pixel(x, y);
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

Expected<Image> loadPng(const std::filesystem::path& path) {
    // 파일은 우리 IO 로 읽는다 (Windows 의 한글 경로 — stb 의 fopen 은 ANSI)
    auto bytes = io::readFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes->data()),
                                          static_cast<int>(bytes->size()), &w, &h, &channels, 4);
    if (data == nullptr) {
        return makeError(ErrorCode::ParseError, std::format("이미지를 읽을 수 없습니다: {}", stbi_failure_reason()),
                         io::displayPath(path));
    }
    Image img;
    img.width = static_cast<u32>(w);
    img.height = static_cast<u32>(h);
    img.rgba.assign(data, data + static_cast<usize>(w) * static_cast<usize>(h) * 4);
    stbi_image_free(data);
    return img;
}

Expected<void> savePng(const std::filesystem::path& path, const Image& image) {
    if (image.width == 0 || image.height == 0 ||
        image.rgba.size() != static_cast<usize>(image.width) * image.height * 4) {
        return makeError(ErrorCode::InvalidArgument, "빈 이미지 또는 크기 불일치", io::displayPath(path));
    }
    std::string png;
    const auto sink = [](void* ctx, void* data, int size) {
        static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), static_cast<usize>(size));
    };
    if (stbi_write_png_to_func(sink, &png, static_cast<int>(image.width), static_cast<int>(image.height), 4,
                               image.rgba.data(), static_cast<int>(image.width * 4)) == 0) {
        return makeError(ErrorCode::IoError, "PNG 인코딩 실패", io::displayPath(path));
    }
    return io::writeFileAtomic(path, png);
}

ImageDiff compareImages(const Image& expected, const Image& actual, u32 tolerance) {
    ImageDiff d;
    d.sameSize = expected.width == actual.width && expected.height == actual.height &&
                 expected.rgba.size() == actual.rgba.size();
    if (!d.sameSize) {
        return d;
    }
    d.totalPixels = static_cast<u64>(expected.width) * expected.height;
    for (usize i = 0; i < expected.rgba.size(); i += 4) {
        u32 worst = 0;
        for (usize c = 0; c < 4; ++c) {
            const int diff = std::abs(static_cast<int>(expected.rgba[i + c]) - static_cast<int>(actual.rgba[i + c]));
            worst = std::max(worst, static_cast<u32>(diff));
        }
        d.maxChannelDiff = std::max(d.maxChannelDiff, worst);
        if (worst > tolerance) {
            ++d.pixelsOverTolerance;
        }
    }
    return d;
}

Image diffImage(const Image& expected, const Image& actual, u32 tolerance) {
    const u32 w = std::max(expected.width, actual.width);
    const u32 h = std::max(expected.height, actual.height);
    Image out = Image::filled(w, h, 255, 0, 255); // 크기가 다르면 넘치는 부분은 자홍
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            if (x >= expected.width || y >= expected.height || x >= actual.width || y >= actual.height) {
                continue;
            }
            const u8* e = expected.pixel(x, y);
            const u8* a = actual.pixel(x, y);
            u32 worst = 0;
            for (int c = 0; c < 4; ++c) {
                worst = std::max(worst, static_cast<u32>(std::abs(static_cast<int>(e[c]) - static_cast<int>(a[c]))));
            }
            if (worst > tolerance) {
                out.setPixel(x, y, 255, 0, 0);
            } else {
                out.setPixel(x, y, static_cast<u8>(e[0] / 4), static_cast<u8>(e[1] / 4), static_cast<u8>(e[2] / 4));
            }
        }
    }
    return out;
}

} // namespace sbx::render
