#pragma once

#include <graphics-ui/core/document.hpp>

#include <array>

namespace graphics_ui::core {

inline constexpr std::size_t document_ir_byte_limit = 8 * 1024 * 1024;

enum class DocumentSourceKind : std::uint8_t { markup, style, model };

struct DocumentSource {
    DocumentSourceKind kind;
    std::string name;
    std::uint32_t byte_size;
    std::array<std::byte, 32> sha256;
};

struct DocumentSourceSpan {
    DocumentSourceKind source;
    std::uint32_t begin, end; // Half-open byte offsets in the original UTF-8 file.
};

struct DocumentNodeSource {
    DocumentSourceSpan definition;
    std::vector<DocumentSourceSpan> uses; // Outermost to innermost Use tag.
};

struct DocumentIrField {
    std::string name;
    StateType type;
    std::optional<std::vector<std::uint16_t>> dependencies; // Indices in fields. Absent for a writable field.
};

struct DocumentIrCommand {
    std::string name;
    std::optional<StateType> argument;
    std::optional<std::uint16_t> writes; // Index in fields.
};

struct DocumentIrResource {
    std::string id;
    std::variant<std::string, RenderColor, FontId> value;
    DocumentSourceSpan source;
};

struct DocumentIr {
    Document document;
    std::vector<DocumentSource> sources;
    std::vector<DocumentNodeSource> node_sources; // Same order as document.nodes.
    std::vector<DocumentSourceSpan> rule_sources; // Same order as document.rules.
    std::vector<DocumentIrField> fields;
    std::vector<DocumentIrCommand> commands;
    std::vector<DocumentIrResource> resources;
};

// Loads an owned Core document after validating the entire file. No host or filesystem callbacks are invoked.
[[nodiscard]] Result<DocumentIr> loadDocumentIr(std::span<const std::byte> bytes);

// The bytecode executes bounded document construction instructions, then shares the Core contract.
[[nodiscard]] Result<DocumentIr> loadDocumentBytecode(std::span<const std::byte> bytes);

struct DocumentTopologyNode {
    std::string id;
    std::optional<std::size_t> parent;
    DocumentKind type = DocumentKind::panel;
    std::vector<std::string> classes, template_ids;
    bool horizontal{};
    std::uint32_t columns = 1;
};

struct DocumentTopology {
    std::array<std::byte, 32> fingerprint{};
    std::vector<DocumentTopologyNode> nodes;
};

// Use the generated native topology together with external hybrid data from the same shape.
[[nodiscard]] Result<DocumentIr> loadDocumentHybrid(std::span<const std::byte> bytes, DocumentTopology topology);

// Checks the revision before a tool uses any stored offsets against external source bytes.
[[nodiscard]] bool matchesDocumentIrSource(const DocumentSource& source, std::span<const std::byte> bytes) noexcept;

} // namespace graphics_ui::core
