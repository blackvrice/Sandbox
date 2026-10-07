#include "render/rhi/PipelineValidation.hpp"

#include <algorithm>
#include <format>

namespace sbx::rhi {
namespace {

bool isBufferType(BindingType t) {
    return t == BindingType::ConstantBuffer || t == BindingType::StorageBuffer || t == BindingType::StorageBufferRW;
}
bool isTextureType(BindingType t) {
    return t == BindingType::Texture || t == BindingType::StorageTexture;
}

} // namespace

std::string validateLayout(const BindGroupLayoutDesc& layout) {
    for (usize i = 0; i < layout.entries.size(); ++i) {
        for (usize j = i + 1; j < layout.entries.size(); ++j) {
            if (layout.entries[i].binding == layout.entries[j].binding) {
                return std::format("레이아웃 '{}': binding {} 이 두 번 (그룹 안 binding 은 종류를 가리지 않고 고유)",
                                   layout.debugName, layout.entries[i].binding);
            }
        }
        if (layout.entries[i].stages == 0) {
            return std::format("레이아웃 '{}': binding {} 의 stages 가 비었다", layout.debugName,
                               layout.entries[i].binding);
        }
    }
    return {};
}

std::string validateAgainstReflection(std::span<const ShaderReflection* const> shaders, const LayoutSet& layouts,
                                      u32 pushConstantBytes, std::span<const VertexAttribute> attributes) {
    if (pushConstantBytes % 4 != 0 || pushConstantBytes > kMaxPushConstantBytes) {
        return std::format("push constant {} 바이트: 4 의 배수, {} 이하", pushConstantBytes, kMaxPushConstantBytes);
    }
    for (const ShaderReflection* r : shaders) {
        if (r == nullptr) {
            continue;
        }
        for (const ReflectedBinding& b : r->bindings) {
            if (b.group >= kMaxBindGroups || layouts[b.group] == nullptr) {
                return std::format("셰이더의 '{}' (group {} binding {}) 를 위한 BindGroupLayout 이 슬롯 {} 에 없다",
                                   b.name, b.group, b.binding, b.group);
            }
            const auto& entries = layouts[b.group]->entries;
            const auto it = std::ranges::find(entries, b.binding, &BindGroupLayoutEntry::binding);
            if (it == entries.end()) {
                return std::format("셰이더의 '{}' (group {} binding {}) 가 레이아웃 '{}' 에 없다", b.name, b.group,
                                   b.binding, layouts[b.group]->debugName);
            }
            if (it->type != b.type) {
                return std::format("'{}' (group {} binding {}): 셰이더는 {}, 레이아웃은 {}", b.name, b.group, b.binding,
                                   bindingTypeName(b.type), bindingTypeName(it->type));
            }
            if ((it->stages & b.stages) != b.stages) {
                return std::format("'{}' (group {} binding {}): 레이아웃의 stages 에 셰이더 단계가 빠졌다", b.name,
                                   b.group, b.binding);
            }
        }
        if (r->pushConstantBytes > pushConstantBytes) {
            return std::format("셰이더 push constant {} 바이트 > 파이프라인 {} 바이트", r->pushConstantBytes,
                               pushConstantBytes);
        }
        for (const ReflectedVertexInput& in : r->vertexInputs) {
            const auto it = std::ranges::find_if(attributes, [&](const VertexAttribute& a) {
                return a.semantic == in.semantic && a.semanticIndex == in.semanticIndex;
            });
            if (it == attributes.end()) {
                return std::format("정점 입력 {}{} (location {}) 를 공급하는 속성이 없다", in.semantic,
                                   in.semanticIndex, in.location);
            }
        }
    }
    return {};
}

std::string validateBindGroup(const BindGroupLayoutDesc& layout, const BindGroupDesc& group,
                              const BindGroupResourceCheck& alive) {
    if (group.entries.size() != layout.entries.size()) {
        return std::format("바인드 그룹 '{}': 항목 {} 개, 레이아웃 '{}' 은 {} 개", group.debugName,
                           group.entries.size(), layout.debugName, layout.entries.size());
    }
    for (const BindGroupLayoutEntry& le : layout.entries) {
        const auto n = std::ranges::count(group.entries, le.binding, &BindGroupEntry::binding);
        if (n != 1) {
            return std::format("바인드 그룹 '{}': binding {} 항목이 {} 개 (정확히 1)", group.debugName, le.binding, n);
        }
        const BindGroupEntry& e = *std::ranges::find(group.entries, le.binding, &BindGroupEntry::binding);
        if (isBufferType(le.type)) {
            if (alive.bufferAlive != nullptr && !alive.bufferAlive(alive.ctx, e.buffer)) {
                return std::format("바인드 그룹 '{}': binding {} ({}) 에 살아 있는 버퍼가 없다", group.debugName,
                                   le.binding, bindingTypeName(le.type));
            }
            if (le.type != BindingType::ConstantBuffer && e.stride == 0) {
                return std::format("바인드 그룹 '{}': binding {} StorageBuffer 의 stride 가 0", group.debugName,
                                   le.binding);
            }
        } else if (isTextureType(le.type)) {
            if (alive.textureAlive != nullptr && !alive.textureAlive(alive.ctx, e.texture)) {
                return std::format("바인드 그룹 '{}': binding {} 에 살아 있는 텍스처가 없다", group.debugName,
                                   le.binding);
            }
        } else if (alive.samplerAlive != nullptr && !alive.samplerAlive(alive.ctx, e.sampler)) {
            return std::format("바인드 그룹 '{}': binding {} 에 살아 있는 샘플러가 없다", group.debugName, le.binding);
        }
    }
    return {};
}

std::string layoutSignature(const BindGroupLayoutDesc& layout) {
    std::string s;
    for (const BindGroupLayoutEntry& e : layout.entries) {
        s += std::format("{}:{}:{}:{};", e.binding, static_cast<u32>(e.type), static_cast<u32>(e.stages),
                         static_cast<u32>(e.dim));
    }
    return s;
}

} // namespace sbx::rhi
