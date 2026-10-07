// RHI 의 백엔드 독립 로직: 핸들 풀 · 지연 해제 큐 · 업로드 링 · 포맷 표 · 백엔드 팩토리. docs/06-RENDERING.md 4장·14장.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <random>

#include "render/rhi/DeferredDestruction.hpp"
#include "render/rhi/HandlePool.hpp"
#include "render/rhi/RenderDevice.hpp"
#include "render/rhi/UploadRing.hpp"

using namespace sbx;
using namespace sbx::rhi;

TEST_SUITE("render") {

    TEST_CASE("handle pool: stale handles are rejected after take, slots are reused with a new generation") {
        HandlePool<TextureTag, int> pool;
        const RhiTexture a = pool.insert(1);
        const RhiTexture b = pool.insert(2);
        CHECK(pool.size() == 2);
        REQUIRE(pool.get(a) != nullptr);
        CHECK(*pool.get(b) == 2);

        const auto taken = pool.take(a);
        REQUIRE(taken.has_value());
        CHECK(*taken == 1);
        CHECK(pool.get(a) == nullptr); // 즉시 무효
        CHECK_FALSE(pool.take(a).has_value());
        CHECK(pool.size() == 1);

        const RhiTexture c = pool.insert(3);
        CHECK(c.index == a.index);
        CHECK(c.generation != a.generation);
        CHECK(pool.get(a) == nullptr);
        CHECK(*pool.get(c) == 3);
        CHECK(pool.get(RhiTexture{}) == nullptr);
        CHECK(pool.get(RhiTexture{99, 0}) == nullptr);

        int sum = 0;
        pool.forEach([&](RhiTexture, int v) { sum += v; });
        CHECK(sum == 5);
    }

    TEST_CASE("deferred destruction releases objects only after their fence completes") {
        struct Tracker {
            int* counter;
            explicit Tracker(int* c) : counter(c) {}
            Tracker(Tracker&& o) noexcept : counter(o.counter) { o.counter = nullptr; }
            Tracker& operator=(Tracker&&) = delete;
            ~Tracker() {
                if (counter != nullptr) {
                    ++*counter;
                }
            }
        };
        int released = 0;
        DeferredDestructionQueue<Tracker> q;
        q.push(Tracker(&released), 5);
        q.push(Tracker(&released), 5);
        q.push(Tracker(&released), 7);
        CHECK(q.size() == 3);
        CHECK(q.collect(4) == 0);
        CHECK(released == 0);
        CHECK(q.collect(5) == 2);
        CHECK(released == 2);
        CHECK(q.collect(6) == 0);
        q.push(Tracker(&released), 9);
        CHECK(q.clear() == 2); // 종료: waitIdle 뒤 전부
        CHECK(released == 4);
    }

    TEST_CASE("upload ring: alignment, wrap-around, retire by fence") {
        UploadRing ring(1024);
        auto a = ring.allocate(100, 1);
        REQUIRE(a);
        CHECK(*a == 0);
        auto b = ring.allocate(100, 256);
        REQUIRE(b);
        CHECK(*b == 256); // 정렬 여분 156 바이트도 사용량
        CHECK(ring.used() == 356);
        ring.endFrame(1);

        auto c = ring.allocate(600, 64); // 356 → 384 정렬, 384+600 = 984 ≤ 1024
        REQUIRE(c);
        CHECK(*c == 384);
        ring.endFrame(2);
        CHECK_FALSE(ring.allocate(200, 1)); // 끝에 40 바이트, 앞은 아직 프레임 1 이 쓴다

        CHECK(ring.used() == 984); // 프레임 2: 정렬 여분 28 + 600

        ring.retire(1); // 프레임 1 반납 → [0, 356) 이 빈다
        CHECK(ring.used() == 628);
        auto d = ring.allocate(200, 1); // 끝 40 바이트를 버리고 앞으로 감는다
        REQUIRE(d);
        CHECK(*d == 0);
        CHECK(ring.used() == 868);          // 버린 꼬리 40 도 센다
        CHECK_FALSE(ring.allocate(200, 1)); // [200, 356) 은 156 바이트뿐
        auto e = ring.allocate(156, 1);
        REQUIRE(e);
        CHECK(*e == 200);
        CHECK(ring.used() == 1024);
        CHECK_FALSE(ring.allocate(1, 1)); // 가득
        ring.endFrame(3);

        ring.retire(3);
        CHECK(ring.used() == 0);
        CHECK(ring.pendingFrames() == 0);
        auto f = ring.allocate(1024, 1); // 비면 처음부터 — 링 전체도 가능
        REQUIRE(f);
        CHECK(*f == 0);
        CHECK_FALSE(ring.allocate(1, 1));
        CHECK_FALSE(UploadRing(16).allocate(17, 1));
        CHECK_FALSE(UploadRing(16).allocate(0, 1));
    }

    TEST_CASE("upload ring: random frames never hand out overlapping live ranges") {
        // 참조 모델: 살아 있는 구간 집합. 할당이 살아 있는 구간과 겹치면 실패
        std::mt19937 rng(7);
        UploadRing ring(4096);
        struct Live {
            u64 fence, offset, size;
        };
        std::vector<Live> live;
        u64 fence = 0;
        u64 completed = 0;
        usize allocations = 0;
        usize wraps = 0;
        u64 lastOffset = 0;
        for (int frame = 0; frame < 2000; ++frame) {
            ++fence;
            const int n = static_cast<int>(rng() % 5);
            for (int i = 0; i < n; ++i) {
                const u64 size = 1 + rng() % 700;
                const u64 align = u64{1} << (rng() % 9);
                const auto off = ring.allocate(size, align);
                if (!off) {
                    continue;
                }
                ++allocations;
                if (*off < lastOffset) {
                    ++wraps;
                }
                lastOffset = *off;
                CHECK(*off % align == 0);
                CHECK(*off + size <= ring.capacity());
                for (const Live& l : live) {
                    const bool overlap = *off < l.offset + l.size && l.offset < *off + size;
                    CHECK_FALSE(overlap);
                }
                live.push_back({fence, *off, size});
            }
            ring.endFrame(fence);
            // GPU 는 2~3 프레임 뒤처진다
            if (fence > 3 && rng() % 4 != 0) {
                completed = fence - 2 - rng() % 2;
                ring.retire(completed);
                std::erase_if(live, [&](const Live& l) { return l.fence <= completed; });
            }
        }
        CHECK(allocations > 3000);
        CHECK(wraps > 100);
        ring.retire(fence);
        CHECK(ring.used() == 0);
    }

    TEST_CASE("format table and names") {
        CHECK(formatInfo(Format::RGBA8Unorm).bytesPerPixel == 4);
        CHECK(formatInfo(Format::RGBA16Float).bytesPerPixel == 8);
        CHECK(formatInfo(Format::D24UnormS8Uint).depth);
        CHECK(formatInfo(Format::D24UnormS8Uint).stencil);
        CHECK(formatInfo(Format::BGRA8Srgb).srgb);
        CHECK(formatInfo(Format::BGRA8Unorm).name == "BGRA8Unorm");
        CHECK(formatInfo(static_cast<Format>(200)).name == "Unknown");
        CHECK(backendName(BackendType::D3D12) == "D3D12");
        CHECK(resourceStateName(ResourceState::CopyDst) == "CopyDst");
        CHECK(alignUp(257, 256) == 512);
        CHECK(alignUp(256, 256) == 256);
        CHECK(alignUp(5, 0) == 5);
        CHECK(hasFlag(TextureUsage::RenderTarget | TextureUsage::CopySrc, TextureUsage::CopySrc));
        CHECK_FALSE(hasFlag(TextureUsage::RenderTarget, TextureUsage::Sampled));
    }

#ifndef _WIN32
    TEST_CASE("render device factory: no backend on this OS yet") {
        CHECK(defaultBackend() == BackendType::Auto);
        auto dev = createRenderDevice({});
        REQUIRE_FALSE(dev.has_value());
        CHECK(dev.error().code == ErrorCode::Unsupported);
    }
#endif
}
