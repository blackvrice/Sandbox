#pragma once
// Graphics SDK ICommandList 어댑터 (Stage-Before-Record 지연 명령 재생 포함).

#include <graphics/Graphics.hpp>
#include <memory>
#include <span>
#include <string_view>

#include "render/rhi/RenderDevice.hpp"

namespace sbx::rhi::graphics_sdk {

class GraphicsSdkDevice;

[[nodiscard]] std::unique_ptr<ICommandList> makeGraphicsSdkCommandList(GraphicsSdkDevice& dev, QueueType type);

// GraphicsSdkCommandQueue::submit 이 부르는 내부 함수:
// Stage-Before-Record 에 따라 이번 프레임의 UploadRing/Upload 버퍼 쓰기를 먼저 SDK 버퍼에 반영한 뒤
// 기록된 명령을 SDK CommandList 에 재생(replay)하고 close 한다.
[[nodiscard]] gfx::CommandList* prepareForSubmit(ICommandList& list);

// 제출 완료 직후 Readback 복사 대상 버퍼들을 동기화하기 위한 후처리
void notifySubmitted(ICommandList& list, gfx::Fence fence);

} // namespace sbx::rhi::graphics_sdk
