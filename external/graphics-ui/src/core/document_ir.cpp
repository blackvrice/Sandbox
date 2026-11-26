#include <graphics-ui/core/document_ir.hpp>

#include "document_validation.hpp"
#include "sha256.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace graphics_ui::core {
namespace {
constexpr std::string_view binary_magic = "GUIIR\r\n\x1a", bytecode_magic = "GUIBC\r\n\x1a", hybrid_magic = "GUIHY\r\n\x1a";
constexpr std::uint16_t format_version = 1, schema_version = 2, absent = 0xffff;
constexpr std::size_t max_source_bytes = 65536, max_registry_entries = 128, max_depth = 32, max_children = 128;
constexpr std::array source_kinds{DocumentSourceKind::markup, DocumentSourceKind::style, DocumentSourceKind::model};
constexpr std::array types{DocumentKind::window, DocumentKind::panel, DocumentKind::stack_panel, DocumentKind::grid,
    DocumentKind::flex_panel, DocumentKind::text_block, DocumentKind::text_box, DocumentKind::button};
constexpr std::array states{DocumentState::none, DocumentState::disabled, DocumentState::focus,
    DocumentState::hover, DocumentState::pressed};
constexpr std::array relations{DocumentRelation::none, DocumentRelation::child, DocumentRelation::descendant};
constexpr std::array field_types{StateType::text, StateType::boolean, StateType::integer, StateType::number};

struct Failure { ErrorCode code; };
enum class Opcode : std::uint8_t { stop, source, field, command, resource, node, rule };

void require(bool condition, ErrorCode code = ErrorCode::invalid_argument) {
    if (!condition) throw Failure{code};
}

bool lowerAscii(char character) { return character >= 'a' && character <= 'z'; }
bool upperAscii(char character) { return character >= 'A' && character <= 'Z'; }
bool digit(char character) { return character >= '0' && character <= '9'; }

bool modelName(std::string_view name) {
    const auto first = [](char character) { return lowerAscii(character) || upperAscii(character) || character == '_'; };
    return !name.empty() && name.size() <= 64 && first(name.front()) &&
        std::ranges::all_of(name, [&](char character) { return first(character) || digit(character); });
}

bool nodeId(std::string_view id) {
    return !id.empty() && id.size() <= 64 && lowerAscii(id.front()) &&
        std::ranges::all_of(id, [](char character) { return lowerAscii(character) || digit(character) || character == '-'; });
}

bool qualifiedId(std::string_view id) {
    for (std::size_t depth = 0; depth < max_depth; ++depth) {
        const auto separator = id.find('/');
        if (!nodeId(id.substr(0, separator))) return false;
        if (separator == id.npos) return true;
        id.remove_prefix(separator + 1);
    }
    return false;
}

bool textBytes(std::string_view text) {
    return detail::validUtf8(text) && std::ranges::all_of(text, [](unsigned char byte) {
        return byte >= 32 || byte == '\t' || byte == '\n';
    });
}

bool leaf(DocumentKind type) {
    return type == DocumentKind::text_block || type == DocumentKind::text_box || type == DocumentKind::button;
}

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : remaining(bytes) {}

    std::span<const std::byte> take(std::size_t count) {
        require(count <= remaining.size());
        const auto value = remaining.first(count);
        remaining = remaining.subspan(count);
        return value;
    }

    template<class T>
    T integer() {
        const auto bytes = take(sizeof(T));
        std::uint64_t value{};
        for (std::size_t index = 0; index < bytes.size(); ++index)
            value |= std::to_integer<std::uint64_t>(bytes[index]) << (index * 8);
        return static_cast<T>(value);
    }

    std::uint8_t byte() { return integer<std::uint8_t>(); }
    std::uint16_t word() { return integer<std::uint16_t>(); }
    std::uint32_t offset() { return integer<std::uint32_t>(); }

    bool boolean() {
        const auto value = byte();
        require(value <= 1);
        return value == 1;
    }

    std::string_view rawString(std::size_t count) {
        const auto bytes = take(count);
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    std::string string(std::size_t limit) {
        if (!uses_string_pool) return std::string(inlineString(limit));
        const auto index = word();
        require(index < string_pool.size());
        const auto value = string_pool[index];
        require(value.size() <= limit, ErrorCode::limit_exceeded);
        return std::string(value);
    }

    void beginBytecode() {
        const auto count = word();
        string_pool.reserve(count);
        for (std::size_t index = 0; index < count; ++index) string_pool.push_back(inlineString(max_source_bytes));
        uses_string_pool = true;
    }

    void opcode(Opcode expected) {
        if (uses_string_pool) require(byte() == static_cast<std::uint8_t>(expected));
    }

    std::string_view inlineString(std::size_t limit) {
        const auto size = offset();
        require(size <= limit, ErrorCode::limit_exceeded);
        const auto value = rawString(size);
        require(textBytes(value));
        return value;
    }

    std::size_t count(std::size_t limit) {
        const auto value = word();
        require(value <= limit, ErrorCode::limit_exceeded);
        return value;
    }

    RenderColor color() { return {byte(), byte(), byte()}; }

    FontId font() {
        const auto value = integer<FontId>();
        require(value != 0);
        return value;
    }

    double length(std::uint16_t minimum) {
        const auto value = word();
        require(minimum <= value && value <= 4096);
        return value;
    }

    std::span<const std::byte> remaining;

private:
    bool uses_string_pool{};
    std::vector<std::string_view> string_pool;
};

template<class Collection>
const auto& entry(const Collection& collection, std::size_t index) {
    require(index < collection.size());
    return collection[index];
}

void validateDependencies(const std::vector<DocumentIrField>& fields) {
    enum class Visit { pending, active, done };
    std::array<Visit, max_registry_entries> visits{};
    const auto visit = [&](auto&& next, std::size_t index) -> void {
        auto& current = visits[index];
        require(current != Visit::active, ErrorCode::dependency_cycle);
        if (current == Visit::done) return;
        current = Visit::active;
        if (const auto& dependencies = fields[index].dependencies)
            for (const auto dependency : *dependencies) next(next, dependency);
        current = Visit::done;
    };
    for (std::size_t index = 0; index < fields.size(); ++index) visit(visit, index);
}

class DocumentReader {
public:
    explicit DocumentReader(std::span<const std::byte> payload) : read(payload) {}

    DocumentIr load(bool bytecode, DocumentTopology* topology) {
        if (bytecode) read.beginBytecode();
        require(read.byte() == 0); // Core profile.
        sources();
        fieldRegistry();
        commandRegistry();
        resourceRegistry();
        nodes(topology);
        rules();
        read.opcode(Opcode::stop);
        require(read.remaining.empty());
        const auto valid = detail::validateDocument(ir.document);
        if (!valid) throw Failure{valid.error()};
        return std::move(ir);
    }

private:
    Reader read;
    DocumentIr ir;
    std::array<std::uint32_t, source_kinds.size()> source_sizes{};

    void sources() {
        const auto count = read.byte();
        require(count > 0 && count <= source_kinds.size());
        int previous = -1;
        for (std::size_t index = 0; index < count; ++index) {
            read.opcode(Opcode::source);
            const auto role = read.byte();
            require(role < source_kinds.size() && role > previous && (index != 0 || role == 0));
            previous = role;
            auto name = read.string(1024);
            require(!name.empty() && name != "." && name != ".." && std::ranges::none_of(name, [](unsigned char byte) {
                return byte < 32 || byte == '/' || byte == '\\' || byte == ':';
            }));
            const auto size = read.offset();
            require(size <= max_source_bytes, ErrorCode::limit_exceeded);
            source_sizes[role] = size;
            DocumentSource source{source_kinds[role], std::move(name), size, {}};
            std::ranges::copy(read.take(source.sha256.size()), source.sha256.begin());
            ir.sources.push_back(std::move(source));
        }
    }

    DocumentSourceSpan span(DocumentSourceKind source) {
        const auto begin = read.offset(), end = read.offset();
        require(begin < end && end <= source_sizes[static_cast<std::size_t>(source)]);
        return {source, begin, end};
    }

    void fieldRegistry() {
        const auto count = read.count(max_registry_entries);
        require(count == 0 || source_sizes[2] != 0);
        ir.fields.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            read.opcode(Opcode::field);
            auto name = read.string(64);
            require(modelName(name) && (ir.fields.empty() || ir.fields.back().name < name));
            const auto type = entry(field_types, read.byte());
            DocumentIrField field{std::move(name), type, {}};
            const auto dependencies = read.word();
            if (dependencies != absent) {
                require(dependencies <= count);
                auto& list = field.dependencies.emplace();
                list.reserve(dependencies);
                for (std::size_t position = 0; position < dependencies; ++position) {
                    const auto target = read.word();
                    require(target < count && (list.empty() || list.back() < target));
                    list.push_back(target);
                }
            }
            ir.fields.push_back(std::move(field));
        }
        validateDependencies(ir.fields);
    }

    void commandRegistry() {
        const auto count = read.count(max_registry_entries);
        require(count == 0 || source_sizes[2] != 0);
        ir.commands.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            read.opcode(Opcode::command);
            auto name = read.string(64);
            require(modelName(name) && (ir.commands.empty() || ir.commands.back().name < name));
            DocumentIrCommand command{std::move(name), {}, {}};
            const auto argument = read.byte();
            if (argument != 255) command.argument = entry(field_types, argument);
            const auto writes = read.word();
            if (writes != absent) {
                const auto& field = entry(ir.fields, writes);
                require(command.argument == field.type && !field.dependencies);
                command.writes = writes;
            }
            ir.commands.push_back(std::move(command));
        }
    }

    void resourceRegistry() {
        const auto count = read.count(max_registry_entries);
        ir.resources.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            read.opcode(Opcode::resource);
            auto id = read.string(64);
            require(nodeId(id) && (ir.resources.empty() || ir.resources.back().id < id));
            const auto kind = read.byte();
            DocumentIrResource resource{std::move(id), {}, span(DocumentSourceKind::markup)};
            switch (kind) {
            case 0: resource.value = read.string(max_source_bytes); break;
            case 1: resource.value = read.color(); break;
            case 2: resource.value = read.font(); break;
            default: throw Failure{ErrorCode::invalid_argument};
            }
            ir.resources.push_back(std::move(resource));
        }
    }

    template<class T>
    const T& resource() {
        const auto* value = std::get_if<T>(&entry(ir.resources, read.word()).value);
        require(value != nullptr);
        return *value;
    }

    DocumentStyle style(std::uint8_t allowed, bool required = false) {
        const auto mask = read.byte();
        require((mask & allowed) == mask && (!required || mask != 0));
        DocumentStyle result;
        if (mask & 1) result.width = read.length(1);
        if (mask & 2) result.height = read.length(1);
        if (mask & 4) result.gap = read.length(0);
        if (mask & 8) result.font_size = read.length(1);
        if (mask & 16) result.padding = LayoutInsets{read.length(0), read.length(0), read.length(0), read.length(0)};
        if (mask & 32) result.color = read.boolean() ? resource<RenderColor>() : read.color();
        if (mask & 64) result.background = read.boolean() ? resource<RenderColor>() : read.color();
        if (mask & 128) result.font = read.boolean() ? resource<FontId>() : read.font();
        return result;
    }

    std::vector<std::string> names(std::size_t limit, bool (*is_name)(std::string_view)) {
        const auto count = read.count(limit);
        std::vector<std::string> result;
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            auto name = read.string(64);
            require(is_name(name));
            result.push_back(std::move(name));
        }
        return result;
    }

    void bindings(DocumentNode& node) {
        const auto text_source = read.byte();
        const bool editable = node.type == DocumentKind::text_box;
        if (text_source == 0) {
            require(!editable);
            node.text = read.string(max_source_bytes);
        } else if (text_source == 1) {
            const auto& field = entry(ir.fields, read.word());
            require(leaf(node.type) && field.type == StateType::text && (!editable || !field.dependencies));
            node.text_field = field.name;
        } else {
            require(text_source == 2 && !editable && leaf(node.type));
            node.text = resource<std::string>();
        }
        const auto enabled_source = read.byte();
        require(enabled_source <= 2 && (node.type == DocumentKind::button || enabled_source == 1));
        if (enabled_source == 2) {
            const auto& field = entry(ir.fields, read.word());
            require(field.type == StateType::boolean);
            node.enabled_field = field.name;
        } else {
            node.enabled = enabled_source == 1;
        }
        const auto command_index = read.word();
        if (command_index == absent) {
            require(!editable);
            return;
        }
        const auto& command = entry(ir.commands, command_index);
        if (editable) {
            require(command.argument == StateType::text && command.writes &&
                ir.fields[*command.writes].name == node.text_field);
        } else {
            require(node.type == DocumentKind::button && !command.argument);
        }
        node.command = command.name;
    }

    DocumentTopologyNode readShape() {
        auto id = read.string(max_depth * 65 - 1);
        std::optional<std::size_t> parent;
        const auto parent_index = read.word();
        if (parent_index != absent) parent = parent_index;
        return {std::move(id), parent, entry(types, read.byte()), names(8 * detail::max_document_nodes, modelName),
            names(detail::max_document_nodes, nodeId), read.boolean(), read.byte()};
    }

    void nodes(DocumentTopology* topology) {
        if (topology) require(std::ranges::equal(read.take(topology->fingerprint.size()), topology->fingerprint));
        const auto count = read.count(detail::max_document_nodes);
        if (topology) require(topology->nodes.size() == count);
        ir.document.nodes.reserve(count);
        ir.node_sources.reserve(count);
        std::array<std::size_t, detail::max_document_nodes> depths{}, children{};
        for (std::size_t index = 0; index < count; ++index) {
            read.opcode(Opcode::node);
            auto shape = topology ? std::move(topology->nodes[index]) : readShape();
            DocumentNode node{std::move(shape.id), shape.parent, shape.type, std::move(shape.classes),
                std::move(shape.template_ids), shape.horizontal, shape.columns};
            require(index == 0 ? nodeId(node.id) : qualifiedId(node.id));
            depths[index] = 1;
            if (node.parent) {
                require(*node.parent < index);
                depths[index] += depths[*node.parent];
                require(depths[index] <= max_depth && ++children[*node.parent] <= max_children, ErrorCode::limit_exceeded);
            }
            require(!node.horizontal || node.type == DocumentKind::stack_panel || node.type == DocumentKind::flex_panel);
            require(node.type == DocumentKind::grid ? 1 <= node.columns && node.columns <= 64 : node.columns == 1);
            node.style = style(leaf(node.type) ? 0xa3 : 0x03);
            bindings(node);
            DocumentNodeSource source{span(DocumentSourceKind::markup), {}};
            const auto use_count = read.count(detail::max_document_nodes);
            source.uses.reserve(use_count);
            for (std::size_t use = 0; use < use_count; ++use) source.uses.push_back(span(DocumentSourceKind::markup));
            ir.document.nodes.push_back(std::move(node));
            ir.node_sources.push_back(std::move(source));
        }
    }

    void rules() {
        const auto count = read.count(detail::max_document_rules);
        ir.document.rules.reserve(count);
        ir.rule_sources.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            read.opcode(Opcode::rule);
            const auto parts = read.byte();
            require(parts > 0 && parts <= detail::max_selector_parts);
            DocumentRule rule;
            rule.selector.reserve(parts);
            for (std::size_t position = 0; position < parts; ++position) {
                DocumentSelectorPart part;
                const auto kind = read.byte();
                if (kind != 255) part.type = entry(types, kind);
                part.id = read.string(64);
                require(part.id.empty() || nodeId(part.id));
                part.classes = names(8, modelName);
                part.state = entry(states, read.byte());
                part.root = read.boolean();
                part.relation = entry(relations, read.byte());
                rule.selector.push_back(std::move(part));
            }
            rule.style = style(0xff, true);
            ir.rule_sources.push_back(span(DocumentSourceKind::style));
            ir.document.rules.push_back(std::move(rule));
        }
    }
};
Result<DocumentIr> loadDocument(std::span<const std::byte> bytes, std::string_view magic, DocumentTopology* topology = nullptr) {
    try {
        require(bytes.size() <= document_ir_byte_limit, ErrorCode::limit_exceeded);
        Reader file(bytes);
        require(file.rawString(magic.size()) == magic);
        require(file.word() == format_version && file.word() == schema_version);
        const auto size = file.offset();
        const auto digest = file.take(32);
        require(size == file.remaining.size());
        require(std::ranges::equal(digest, detail::sha256(file.remaining)));
        return DocumentReader(file.remaining).load(magic == bytecode_magic, topology);
    } catch (const Failure& failure) {
        return std::unexpected(failure.code);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}
} // namespace

Result<DocumentIr> loadDocumentIr(std::span<const std::byte> bytes) {
    return loadDocument(bytes, binary_magic);
}

Result<DocumentIr> loadDocumentBytecode(std::span<const std::byte> bytes) {
    return loadDocument(bytes, bytecode_magic);
}

Result<DocumentIr> loadDocumentHybrid(std::span<const std::byte> bytes, DocumentTopology topology) {
    return loadDocument(bytes, hybrid_magic, &topology);
}

bool matchesDocumentIrSource(const DocumentSource& source, std::span<const std::byte> bytes) noexcept {
    return bytes.size() <= max_source_bytes && bytes.size() == source.byte_size && detail::sha256(bytes) == source.sha256;
}

} // namespace graphics_ui::core
