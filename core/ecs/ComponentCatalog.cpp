#include "core/ecs/ComponentCatalog.hpp"

#include <algorithm>
#include <cstring>
#include <format>

namespace sbx::ecs {

const ComponentInfo* ComponentCatalog::find(StableId id) const noexcept {
    const auto it = std::lower_bound(m_infos.begin(), m_infos.end(), id,
                                     [](const ComponentInfo& i, StableId v) { return i.stableId < v; });
    return (it != m_infos.end() && it->stableId == id) ? &*it : nullptr;
}

const ComponentInfo* ComponentCatalog::find(std::string_view name) const noexcept {
    return find(fnv1a64(name));
}

Expected<void> ComponentCatalog::addDynamic(DynamicComponentDesc desc) {
    auto layout = DynamicComponentLayout::make(std::move(desc));
    if (!layout) {
        return std::unexpected(layout.error());
    }
    return addDynamic(std::move(*layout));
}

Expected<void> ComponentCatalog::addDynamic(std::shared_ptr<const DynamicComponentLayout> layout) {
    using L = std::shared_ptr<const DynamicComponentLayout>;
    const DynamicComponentLayout& lay = *layout;
    ComponentInfo info;
    info.name = lay.name();
    info.stableId = lay.stableId();
    info.version = lay.version();
    info.flags = lay.flags();
    info.typeId = lay.typeId();
    info.size = lay.stride();
    info.align = 8;
    info.makePool = [L = layout]() -> std::unique_ptr<ComponentPoolBase> {
        return std::make_unique<DynamicComponentPool>(L);
    };
    // 아래는 내장 컴포넌트 (ComponentCatalog::add) 의 람다와 한 줄씩 같다 — reflect 대신 reflectDynamic
    info.writeJson = [L = layout](const void* c, Json& out) {
        out = Json::object();
        JsonWriter w(out);
        reflectDynamicConst(w, *L, static_cast<const std::byte*>(c));
    };
    auto read = [](const DynamicComponentLayout& l, std::byte* c, const Json& in,
                   std::string_view ctx) -> Expected<void> {
        if (!in.is_object()) {
            return json_detail::typeError(ctx, "객체", in);
        }
        JsonReader r(in, ctx);
        reflectDynamic(r, l, c);
        return r.finish();
    };
    info.readJson = [L = layout, read](void* c, const Json& in, std::string_view ctx) {
        return read(*L, static_cast<std::byte*>(c), in, ctx);
    };
    info.patchJson = [L = layout, read](void* c, const Json& patch, std::string_view ctx) -> Expected<void> {
        std::vector<std::byte> copy(static_cast<const std::byte*>(c), static_cast<const std::byte*>(c) + L->stride());
        if (auto r = read(*L, copy.data(), patch, ctx); !r) {
            return r;
        }
        std::memcpy(c, copy.data(), L->stride());
        return {};
    };
    info.validateJson = [L = layout, read](const Json& in, std::string_view ctx) {
        std::vector<std::byte> probe(L->defaults(), L->defaults() + L->stride());
        return read(*L, probe.data(), in, ctx);
    };
    info.hash = [L = layout](const void* c, Fnv1a64& h) {
        HashVisitor v(h);
        reflectDynamicConst(v, *L, static_cast<const std::byte*>(c));
    };
    info.writeBinary = [L = layout](const void* c, std::vector<u8>& out, const EntityRefCodec* refs) {
        ByteWriter w(out);
        BinaryWriteVisitor v(w, refs);
        reflectDynamicConst(v, *L, static_cast<const std::byte*>(c));
    };
    info.readBinary = [L = layout](void* c, const u8* data, usize size, const EntityRefCodec* refs) {
        ByteReader r(data, size);
        BinaryReadVisitor v(r, refs);
        reflectDynamic(v, *L, static_cast<std::byte*>(c));
        return r.atEnd();
    };
    info.getNumber = [L = layout](const void* c, std::string_view field) {
        NumberGetVisitor v(field);
        reflectDynamicConst(v, *L, static_cast<const std::byte*>(c));
        return v.result;
    };
    info.applyNumber = [L = layout](void* c, std::string_view field, f64 value, bool add) {
        NumberSetVisitor v(field, value, add);
        reflectDynamic(v, *L, static_cast<std::byte*>(c));
        return v.done;
    };
    {
        std::vector<std::byte> probe(lay.defaults(), lay.defaults() + lay.stride());
        FieldListVisitor v(info.fields);
        reflectDynamic(v, lay, probe.data());
    }
    info.dynamicLayout = std::move(layout);
    return insert(info);
}

bool ComponentCatalog::hasDynamic() const noexcept {
    return std::any_of(m_infos.begin(), m_infos.end(),
                       [](const ComponentInfo& i) { return i.dynamicLayout != nullptr; });
}

ComponentCatalog ComponentCatalog::withoutDynamic() const {
    ComponentCatalog out;
    for (const ComponentInfo& i : m_infos) {
        if (i.dynamicLayout == nullptr) {
            out.m_infos.push_back(i); // 이미 stableId 순
        }
    }
    return out;
}

Expected<void> ComponentCatalog::insert(const ComponentInfo& info) {
    const auto it = std::lower_bound(m_infos.begin(), m_infos.end(), info.stableId,
                                     [](const ComponentInfo& i, StableId v) { return i.stableId < v; });
    if (it != m_infos.end() && it->stableId == info.stableId) {
        if (it->name == info.name) {
            return makeError(ErrorCode::AlreadyExists,
                             std::format("컴포넌트 '{}' 이(가) 이미 등록되어 있습니다", info.name));
        }
        return makeError(ErrorCode::ValidationFailed,
                         std::format("stableId 충돌: '{}' 와 '{}' (이름을 바꾸십시오)", it->name, info.name));
    }
    m_infos.insert(it, info);
    return {};
}

} // namespace sbx::ecs
