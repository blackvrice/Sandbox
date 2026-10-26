#include "script/lang/Types.hpp"

#include <algorithm>

namespace sbx::script {

const StructField* StructType::find(std::string_view n) const noexcept {
    for (const StructField& f : fields) {
        if (f.name == n) {
            return &f;
        }
    }
    return nullptr;
}

i32 StructType::indexOf(std::string_view n) const noexcept {
    for (usize i = 0; i < fields.size(); ++i) {
        if (fields[i].name == n) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

TypeTable::TypeTable() {
    // kError … kString 의 순서 그대로
    for (const TypeKind k : {TypeKind::Error, TypeKind::Void, TypeKind::Null, TypeKind::Bool, TypeKind::I32,
                             TypeKind::Number, TypeKind::String}) {
        TypeInfo t;
        t.kind = k;
        m_types.push_back(t);
    }
}

TypeId TypeTable::add(TypeInfo t) {
    m_types.push_back(t);
    return static_cast<TypeId>(m_types.size() - 1);
}

TypeId TypeTable::arrayOf(TypeId elem) {
    if (elem == kError) {
        return kError;
    }
    for (usize i = 0; i < m_types.size(); ++i) {
        if (m_types[i].kind == TypeKind::Array && m_types[i].elem == elem) {
            return static_cast<TypeId>(i);
        }
    }
    TypeInfo t;
    t.kind = TypeKind::Array;
    t.elem = elem;
    return add(t);
}

TypeId TypeTable::nullableOf(TypeId elem) {
    if (elem == kError || elem == kNull || kind(elem) == TypeKind::Nullable) {
        return elem;
    }
    for (usize i = 0; i < m_types.size(); ++i) {
        if (m_types[i].kind == TypeKind::Nullable && m_types[i].elem == elem) {
            return static_cast<TypeId>(i);
        }
    }
    TypeInfo t;
    t.kind = TypeKind::Nullable;
    t.elem = elem;
    return add(t);
}

TypeId TypeTable::opaque(api::AbiType abi) {
    for (usize i = 0; i < m_types.size(); ++i) {
        if (m_types[i].kind == TypeKind::Opaque && m_types[i].abi == abi) {
            return static_cast<TypeId>(i);
        }
    }
    TypeInfo t;
    t.kind = TypeKind::Opaque;
    t.abi = abi;
    return add(t);
}

TypeId TypeTable::newStruct(std::string n) {
    m_structs.push_back(StructType{std::move(n), {}});
    TypeInfo t;
    t.kind = TypeKind::Struct;
    t.index = static_cast<u32>(m_structs.size() - 1);
    return add(t);
}

TypeId TypeTable::newEnum(std::string n) {
    m_enums.push_back(EnumType{std::move(n), {}});
    TypeInfo t;
    t.kind = TypeKind::Enum;
    t.index = static_cast<u32>(m_enums.size() - 1);
    return add(t);
}

TypeId TypeTable::newComponentRef(TypeId data, u32 componentIndex) {
    TypeInfo t;
    t.kind = TypeKind::Component;
    t.elem = data;
    t.index = componentIndex;
    return add(t);
}

bool TypeTable::isNumeric(TypeId t) const {
    const TypeKind k = kind(t);
    return k == TypeKind::I32 || k == TypeKind::Number || k == TypeKind::Enum;
}

TypeId TypeTable::nonNull(TypeId t) const {
    return kind(t) == TypeKind::Nullable ? m_types[t].elem : t;
}

bool TypeTable::assignable(TypeId src, TypeId dst) const {
    std::vector<std::pair<TypeId, TypeId>> assumed;
    return assignableRec(src, dst, assumed);
}

bool TypeTable::assignableRec(TypeId src, TypeId dst, std::vector<std::pair<TypeId, TypeId>>& assumed) const {
    if (src == dst || src == kError || dst == kError) {
        return true;
    }
    const TypeKind sk = kind(src);
    const TypeKind dk = kind(dst);
    if (dk == TypeKind::Nullable) {
        return sk == TypeKind::Null || assignableRec(src, m_types[dst].elem, assumed) ||
               (sk == TypeKind::Nullable && assignableRec(m_types[src].elem, m_types[dst].elem, assumed));
    }
    if (dk == TypeKind::Number) {
        return sk == TypeKind::I32 || sk == TypeKind::Enum;
    }
    if (dk == TypeKind::I32) {
        return sk == TypeKind::Enum;
    }
    if (sk == TypeKind::Array && dk == TypeKind::Array) {
        // 배열은 참조라 원소 타입이 같아야 한다 (i32[] 를 number[] 자리에 넣으면 거기서 1.5 를 넣을 수 있다)
        return m_types[src].elem == m_types[dst].elem ||
               (assignableRec(m_types[src].elem, m_types[dst].elem, assumed) &&
                assignableRec(m_types[dst].elem, m_types[src].elem, assumed));
    }
    if (sk == TypeKind::Struct && dk == TypeKind::Struct) {
        // 재귀 구조체: 지금 비교 중인 짝은 맞다고 가정한다
        if (std::find(assumed.begin(), assumed.end(), std::pair{src, dst}) != assumed.end()) {
            return true;
        }
        assumed.emplace_back(src, dst);
        const StructType& s = structOf(src);
        const StructType& d = structOf(dst);
        if (s.fields.size() != d.fields.size()) {
            return false;
        }
        for (const StructField& df : d.fields) {
            const StructField* sf = s.find(df.name);
            if (sf == nullptr) {
                return false;
            }
            // 필드는 양쪽으로 — 같은 객체를 두 타입으로 본다 (참조). readonly 필드도: 실행 표현 (i32 · f64 칸) 이
            // 같아야 한다 (ADR-0058 — 구조체 배치는 필드 이름 순, 같은 필드 집합 + 같은 필드 타입 = 같은 배치)
            if (!assignableRec(sf->type, df.type, assumed) || !assignableRec(df.type, sf->type, assumed)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

std::string TypeTable::name(TypeId t) const {
    const TypeInfo& i = m_types[t];
    switch (i.kind) {
    case TypeKind::Error:
        return "(오류)";
    case TypeKind::Void:
        return "void";
    case TypeKind::Null:
        return "null";
    case TypeKind::Bool:
        return "boolean";
    case TypeKind::I32:
        return "i32";
    case TypeKind::Number:
        return "number";
    case TypeKind::String:
        return "string";
    case TypeKind::Array: {
        const TypeKind ek = kind(i.elem);
        const std::string e = name(i.elem);
        return ek == TypeKind::Nullable ? "(" + e + ")[]" : e + "[]";
    }
    case TypeKind::Nullable:
        return name(i.elem) + " | null";
    case TypeKind::Struct: {
        const StructType& s = m_structs[i.index];
        if (!s.name.empty()) {
            return s.name;
        }
        std::string out = "{ ";
        for (usize k = 0; k < s.fields.size(); ++k) {
            if (k > 0) {
                out += "; ";
            }
            out += s.fields[k].name + ": " + name(s.fields[k].type);
        }
        return out + " }";
    }
    case TypeKind::Enum:
        return m_enums[i.index].name;
    case TypeKind::Opaque:
        return std::string(api::abiTypeName(i.abi));
    case TypeKind::Component:
        return "ComponentRef<" + name(i.elem) + ">";
    }
    return "?";
}

} // namespace sbx::script
