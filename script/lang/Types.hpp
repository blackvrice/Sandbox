#pragma once
// SandboxScript 타입. docs/19-SANDBOXSCRIPT.md 5장, ADR-0056 3.
//
// 타입은 TypeTable 에 한 번씩 만든다 (같은 모양 = 같은 TypeId — 배열 · 널 가능은 원소로 찾고, 구조체 · enum · 불투명은
// 선언마다 하나). Error 는 이미 보고한 오류의 자리 — 무엇과도 맞는다고 보고 같은 오류가 이어서 나지 않게 한다.

#include <string>
#include <string_view>
#include <vector>

#include "script/api/HostApi.hpp"

namespace sbx::script {

using TypeId = u32;

enum class TypeKind : u8 {
    Error,
    Void,
    Null,   // null 리터럴의 타입
    Bool,   // TS boolean
    I32,    // SDK 의 i32 — 32 비트로 감싼다
    Number, // f64
    String,
    Array,    // T[] · Array<T> (elem)
    Nullable, // T | null (elem)
    Struct,   // interface · 객체 타입 (index → structs)
    Enum,     // enum (index → enums)
    Opaque,   // SDK 불투명 타입 (abi)
};

struct TypeInfo {
    TypeKind kind = TypeKind::Error;
    TypeId elem = 0;
    u32 index = 0;
    api::AbiType abi = api::AbiType::Void;
};

struct StructField {
    std::string name;
    TypeId type = 0;
    bool readonly = false;
};
struct StructType {
    std::string name; // interface 이름, 익명이면 비어 있다 ("{ x: number }" 로 보인다)
    std::vector<StructField> fields;
    [[nodiscard]] const StructField* find(std::string_view n) const noexcept;
    [[nodiscard]] i32 indexOf(std::string_view n) const noexcept;
};
struct EnumMemberInfo {
    std::string name;
    i32 value = 0;
};
struct EnumType {
    std::string name;
    std::vector<EnumMemberInfo> members;
};

class TypeTable {
public:
    static constexpr TypeId kError = 0;
    static constexpr TypeId kVoid = 1;
    static constexpr TypeId kNull = 2;
    static constexpr TypeId kBool = 3;
    static constexpr TypeId kI32 = 4;
    static constexpr TypeId kNumber = 5;
    static constexpr TypeId kString = 6;

    TypeTable();

    [[nodiscard]] TypeId arrayOf(TypeId elem);
    // T | null — T 가 이미 널 가능 · null 이면 그대로
    [[nodiscard]] TypeId nullableOf(TypeId elem);
    [[nodiscard]] TypeId opaque(api::AbiType abi);
    // 구조체 · enum 은 선언마다 새 타입 (필드는 나중에 채울 수 있다 — 재귀 interface)
    TypeId newStruct(std::string name);
    TypeId newEnum(std::string name);

    [[nodiscard]] const TypeInfo& info(TypeId t) const { return m_types[t]; }
    [[nodiscard]] TypeKind kind(TypeId t) const { return m_types[t].kind; }
    [[nodiscard]] StructType& structOf(TypeId t) { return m_structs[m_types[t].index]; }
    [[nodiscard]] const StructType& structOf(TypeId t) const { return m_structs[m_types[t].index]; }
    [[nodiscard]] EnumType& enumOf(TypeId t) { return m_enums[m_types[t].index]; }
    [[nodiscard]] const EnumType& enumOf(TypeId t) const { return m_enums[m_types[t].index]; }

    [[nodiscard]] bool isNumeric(TypeId t) const; // i32 · number · enum
    [[nodiscard]] bool isError(TypeId t) const { return t == kError; }
    // null 을 뺀 타입 (T | null → T)
    [[nodiscard]] TypeId nonNull(TypeId t) const;

    // src 값을 dst 자리에 넣을 수 있나 (상수 · 리터럴의 number → i32 는 부르는 쪽이 따로 본다).
    // i32 → number, enum → i32 · number, T · null → T | null, 구조체는 같은 필드 이름 집합 + 필드마다 넣을 수 있음
    [[nodiscard]] bool assignable(TypeId src, TypeId dst) const;
    [[nodiscard]] std::string name(TypeId t) const;

private:
    bool assignableRec(TypeId src, TypeId dst, std::vector<std::pair<TypeId, TypeId>>& assumed) const;
    TypeId add(TypeInfo t);

    std::vector<TypeInfo> m_types;
    std::vector<StructType> m_structs;
    std::vector<EnumType> m_enums;
};

} // namespace sbx::script
