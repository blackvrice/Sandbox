#include "core/ecs/DynamicComponent.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <mutex>

namespace sbx::ecs {

namespace {

u32 kindSize(DynamicFieldKind k) noexcept {
    switch (k) {
    case DynamicFieldKind::Bool:
        return 1;
    case DynamicFieldKind::I32:
    case DynamicFieldKind::F32:
    case DynamicFieldKind::Enum:
        return 4;
    case DynamicFieldKind::F64:
    case DynamicFieldKind::EntityRef:
    case DynamicFieldKind::Vec2:
        return 8;
    }
    return 8;
}

u32 kindAlign(DynamicFieldKind k) noexcept {
    return k == DynamicFieldKind::Vec2 ? 4 : kindSize(k); // vec2 = f32 둘
}

bool integral(DynamicFieldKind k) noexcept {
    return k == DynamicFieldKind::I32 || k == DynamicFieldKind::Enum;
}

} // namespace

const DynamicComponentLayout::Field* DynamicComponentLayout::findField(std::string_view name) const noexcept {
    for (const Field& f : m_fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

Expected<std::shared_ptr<const DynamicComponentLayout>> DynamicComponentLayout::make(DynamicComponentDesc desc) {
    const std::string ctx = desc.name;
    if (!isValidComponentName(desc.name)) {
        return makeError(ErrorCode::ValidationFailed, "컴포넌트 이름 규칙 위반 (docs/02-ECS.md 9.2)", ctx);
    }
    if (desc.version < 1) {
        return makeError(ErrorCode::ValidationFailed, "컴포넌트 버전은 1 부터", ctx);
    }
    if (desc.fields.size() > kMaxFields) {
        return makeError(ErrorCode::ValidationFailed, std::format("필드는 {} 개까지", kMaxFields), ctx);
    }
    std::sort(desc.fields.begin(), desc.fields.end(),
              [](const DynamicFieldDesc& a, const DynamicFieldDesc& b) { return a.name < b.name; });
    auto layout = std::shared_ptr<DynamicComponentLayout>(new DynamicComponentLayout());
    layout->m_name = desc.name;
    layout->m_stableId = fnv1a64(desc.name);
    layout->m_version = desc.version;
    layout->m_flags = effectiveFlags(desc.flags);
    u32 offset = 0;
    for (usize i = 0; i < desc.fields.size(); ++i) {
        const DynamicFieldDesc& d = desc.fields[i];
        const std::string fctx = std::format("{}.{}", ctx, d.name);
        if (d.name.empty() || (i > 0 && d.name == desc.fields[i - 1].name)) {
            return makeError(ErrorCode::ValidationFailed, "필드 이름이 비었거나 두 번 있다", fctx);
        }
        const bool ranged =
            d.kind == DynamicFieldKind::I32 || d.kind == DynamicFieldKind::F32 || d.kind == DynamicFieldKind::F64;
        if (d.hasRange && (!ranged || !(d.min <= d.max))) {
            return makeError(ErrorCode::ValidationFailed, "범위는 i32 · f32 · f64 에만, min ≤ max", fctx);
        }
        if (!std::isfinite(d.defaultValue) || !std::isfinite(d.defaultY)) {
            return makeError(ErrorCode::ValidationFailed, "기본값은 유한한 수", fctx);
        }
        if (integral(d.kind) && (d.defaultValue != std::trunc(d.defaultValue) || d.defaultValue < -2147483648.0 ||
                                 d.defaultValue > 2147483647.0)) {
            return makeError(ErrorCode::ValidationFailed, "정수 필드의 기본값은 i32 정수", fctx);
        }
        if (d.hasRange && !(d.defaultValue >= d.min && d.defaultValue <= d.max)) {
            return makeError(ErrorCode::ValidationFailed,
                             std::format("기본값 {} 이 범위 [{}, {}] 밖", d.defaultValue, d.min, d.max), fctx);
        }
        const u32 align = kindAlign(d.kind);
        offset = (offset + align - 1) / align * align;
        Field f;
        f.name = d.name;
        f.kind = d.kind;
        f.offset = offset;
        if (d.hasRange) {
            f.meta = FieldMeta::range(d.min, d.max);
        }
        layout->m_fields.push_back(std::move(f));
        offset += kindSize(d.kind);
    }
    layout->m_stride = std::max<usize>(8, (static_cast<usize>(offset) + 7) / 8 * 8);
    layout->m_defaults.assign(layout->m_stride, std::byte{0});
    for (usize i = 0; i < desc.fields.size(); ++i) {
        const DynamicFieldDesc& d = desc.fields[i];
        std::byte* p = layout->m_defaults.data() + layout->m_fields[i].offset;
        switch (d.kind) {
        case DynamicFieldKind::I32:
        case DynamicFieldKind::Enum:
            dynamic_detail::store(p, static_cast<i32>(d.defaultValue));
            break;
        case DynamicFieldKind::F32:
            if (std::fabs(d.defaultValue) > static_cast<f64>(std::numeric_limits<f32>::max())) {
                return makeError(ErrorCode::ValidationFailed, "기본값이 f32 범위 밖", ctx);
            }
            dynamic_detail::store(p, static_cast<f32>(d.defaultValue));
            break;
        case DynamicFieldKind::F64:
            dynamic_detail::store(p, d.defaultValue);
            break;
        case DynamicFieldKind::Bool:
            dynamic_detail::store(p, d.defaultValue != 0.0);
            break;
        case DynamicFieldKind::Vec2:
            if (std::fabs(d.defaultValue) > static_cast<f64>(std::numeric_limits<f32>::max()) ||
                std::fabs(d.defaultY) > static_cast<f64>(std::numeric_limits<f32>::max())) {
                return makeError(ErrorCode::ValidationFailed, "기본값이 f32 범위 밖", ctx);
            }
            dynamic_detail::store(p, Vec2{static_cast<f32>(d.defaultValue), static_cast<f32>(d.defaultY)});
            break;
        case DynamicFieldKind::EntityRef:
            dynamic_detail::store(p, u64{0});
            break;
        }
    }
    layout->m_typeId = dynamicComponentTypeId(layout->m_name);
    return std::shared_ptr<const DynamicComponentLayout>(std::move(layout));
}

ComponentTypeId dynamicComponentTypeId(std::string_view name) {
    static std::mutex mutex;
    static std::map<std::string, ComponentTypeId, std::less<>> ids;
    const std::lock_guard lock(mutex);
    if (const auto it = ids.find(name); it != ids.end()) {
        return it->second;
    }
    const ComponentTypeId id = detail::allocateComponentTypeId();
    ids.emplace(std::string(name), id);
    return id;
}

DynamicComponentPool::DynamicComponentPool(std::shared_ptr<const DynamicComponentLayout> layout)
    : m_layout(std::move(layout)) {}

bool DynamicComponentPool::remove(EntityId e) {
    const u32 i = indexOf(e);
    if (i == kNone) {
        return false;
    }
    const usize stride = m_layout->stride();
    const usize last = m_dense.size() - 1;
    if (i != last) {
        std::memcpy(m_data.data() + static_cast<usize>(i) * stride, m_data.data() + last * stride, stride);
        m_dense[i] = m_dense[last];
        m_changed[i] = m_changed[last];
        m_added[i] = m_added[last];
        setSparse(m_dense[i], i);
    }
    clearSparse(e);
    m_data.resize(last * stride);
    m_dense.pop_back();
    m_changed.pop_back();
    m_added.pop_back();
    return true;
}

void DynamicComponentPool::clear() noexcept {
    m_sparse.clear();
    m_dense.clear();
    m_data.clear();
    m_changed.clear();
    m_added.clear();
}

void* DynamicComponentPool::emplaceDefaultRaw(EntityId e, Tick tick) {
    if (void* existing = getRawForWrite(e, tick)) {
        return existing;
    }
    const auto i = static_cast<u32>(m_dense.size());
    const usize stride = m_layout->stride();
    m_data.insert(m_data.end(), m_layout->defaults(), m_layout->defaults() + stride);
    m_dense.push_back(e);
    m_changed.push_back(tick);
    m_added.push_back(tick);
    setSparse(e, i);
    return m_data.data() + static_cast<usize>(i) * stride;
}

const void* DynamicComponentPool::getRaw(EntityId e) const noexcept {
    const u32 i = indexOf(e);
    return i == kNone ? nullptr : m_data.data() + static_cast<usize>(i) * m_layout->stride();
}

void* DynamicComponentPool::getRawForWrite(EntityId e, Tick tick) noexcept {
    const u32 i = indexOf(e);
    if (i == kNone) {
        return nullptr;
    }
    m_changed[i] = tick;
    return m_data.data() + static_cast<usize>(i) * m_layout->stride();
}

Expected<void> DynamicComponentPool::validate() const {
    if (m_data.size() != m_dense.size() * m_layout->stride()) {
        return makeError(ErrorCode::ValidationFailed, "P2: dense/data 크기 불일치 (바이트 ≠ 개수 × stride)",
                         std::string(name()));
    }
    return validateIndex(m_dense.size());
}

} // namespace sbx::ecs
