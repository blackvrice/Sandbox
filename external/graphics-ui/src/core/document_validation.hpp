#pragma once

#include <graphics-ui/core/document.hpp>

namespace graphics_ui::core::detail {

inline constexpr std::size_t max_document_nodes = 512, max_document_rules = 128, max_selector_parts = 8;

[[nodiscard]] Result<void> validateDocument(const Document& document);

} // namespace graphics_ui::core::detail
