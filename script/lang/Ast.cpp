#include "script/lang/Ast.hpp"

#include <format>

namespace sbx::script {

NodeId Ast::add(NodeKind kind, SourcePos pos, std::span<const NodeId> kids) {
    Node n;
    n.kind = kind;
    n.pos = pos;
    n.firstKid = static_cast<u32>(m_kids.size());
    n.kidCount = static_cast<u32>(kids.size());
    m_kids.insert(m_kids.end(), kids.begin(), kids.end());
    m_nodes.push_back(n);
    return static_cast<NodeId>(m_nodes.size() - 1);
}

u32 Ast::addString(std::string s) {
    m_strings.push_back(std::move(s));
    return static_cast<u32>(m_strings.size() - 1);
}

std::span<const NodeId> Ast::kids(NodeId id) const {
    const Node& n = m_nodes[id];
    return std::span<const NodeId>(m_kids).subspan(n.firstKid, n.kidCount);
}

NodeId Ast::kid(NodeId id, u32 i) const {
    const Node& n = m_nodes[id];
    return i < n.kidCount ? m_kids[n.firstKid + i] : kNoNode;
}

std::string_view Ast::str(NodeId id) const {
    const u32 s = m_nodes[id].str;
    return s == kNoString ? std::string_view{} : std::string_view(m_strings[s]);
}

std::string Ast::dump(NodeId id) const {
    std::string out;
    dumpInto(out, id);
    return out;
}

void Ast::dumpInto(std::string& out, NodeId id) const {
    if (id == kNoNode) {
        out += '_';
        return;
    }
    const Node& n = m_nodes[id];
    out += '(';
    out += nodeKindName(n.kind);
    if (n.op != Tok::EndOfFile) {
        out += ' ';
        out += tokName(n.op);
    }
    if (n.flags & NodeFlag::Exported) {
        out += " export";
    }
    if (n.flags & NodeFlag::Const) {
        out += " const";
    }
    if (n.flags & NodeFlag::Readonly) {
        out += " readonly";
    }
    if (n.flags & NodeFlag::Prefix) {
        out += " prefix";
    }
    if (n.flags & NodeFlag::Shorthand) {
        out += " shorthand";
    }
    if (n.kind == NodeKind::Number) {
        out += std::format(" {}{}", n.number, (n.flags & NodeFlag::Integer) ? "" : "f");
    } else if (n.kind == NodeKind::Bool) {
        out += (n.flags & NodeFlag::True) ? " true" : " false";
    } else if (n.kind == NodeKind::String) {
        out += std::format(" \"{}\"", str(id));
    } else if (n.str != kNoString) {
        out += ' ';
        out += str(id);
    }
    for (const NodeId k : kids(id)) {
        out += ' ';
        dumpInto(out, k);
    }
    out += ')';
}

std::string_view nodeKindName(NodeKind k) noexcept {
    switch (k) {
    case NodeKind::Module:
        return "Module";
    case NodeKind::Import:
        return "Import";
    case NodeKind::ImportName:
        return "ImportName";
    case NodeKind::ExportDefault:
        return "ExportDefault";
    case NodeKind::Function:
        return "Function";
    case NodeKind::Param:
        return "Param";
    case NodeKind::VarDecl:
        return "VarDecl";
    case NodeKind::TypeAlias:
        return "TypeAlias";
    case NodeKind::Interface:
        return "Interface";
    case NodeKind::PropertySig:
        return "PropertySig";
    case NodeKind::Enum:
        return "Enum";
    case NodeKind::EnumMember:
        return "EnumMember";
    case NodeKind::TypeName:
        return "TypeName";
    case NodeKind::ArrayType:
        return "ArrayType";
    case NodeKind::UnionType:
        return "UnionType";
    case NodeKind::NullType:
        return "NullType";
    case NodeKind::ObjectType:
        return "ObjectType";
    case NodeKind::Block:
        return "Block";
    case NodeKind::ExprStmt:
        return "ExprStmt";
    case NodeKind::If:
        return "If";
    case NodeKind::While:
        return "While";
    case NodeKind::DoWhile:
        return "DoWhile";
    case NodeKind::For:
        return "For";
    case NodeKind::ForOf:
        return "ForOf";
    case NodeKind::Break:
        return "Break";
    case NodeKind::Continue:
        return "Continue";
    case NodeKind::Return:
        return "Return";
    case NodeKind::Switch:
        return "Switch";
    case NodeKind::Case:
        return "Case";
    case NodeKind::Empty:
        return "Empty";
    case NodeKind::Number:
        return "Number";
    case NodeKind::String:
        return "String";
    case NodeKind::Template:
        return "Template";
    case NodeKind::Bool:
        return "Bool";
    case NodeKind::Null:
        return "Null";
    case NodeKind::Ident:
        return "Ident";
    case NodeKind::Unary:
        return "Unary";
    case NodeKind::Binary:
        return "Binary";
    case NodeKind::Assign:
        return "Assign";
    case NodeKind::Update:
        return "Update";
    case NodeKind::Conditional:
        return "Conditional";
    case NodeKind::Call:
        return "Call";
    case NodeKind::Member:
        return "Member";
    case NodeKind::Index:
        return "Index";
    case NodeKind::ArrayLit:
        return "ArrayLit";
    case NodeKind::ObjectLit:
        return "ObjectLit";
    case NodeKind::Property:
        return "Property";
    case NodeKind::Method:
        return "Method";
    case NodeKind::AsConst:
        return "AsConst";
    case NodeKind::Paren:
        return "Paren";
    }
    return "?";
}

} // namespace sbx::script
