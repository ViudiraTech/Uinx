// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Internal helpers shared by the translation units split out of borrow.cpp.

#pragma once

#include "uinx/borrow.hpp"

#include <algorithm>
#include <sstream>

namespace uinx::borrow_detail {
inline constexpr std::size_t kMaxDataflowIterations = 4096;

inline std::string root_of(std::string_view place) {
    const auto dot = place.find('.');
    return std::string(place.substr(0, dot));
}

inline bool is_strict_child(std::string_view parent, std::string_view child) {
    return child.size() > parent.size() && child.substr(0, parent.size()) == parent &&
           child[parent.size()] == '.';
}

inline std::string symbol_key(std::string_view name, hir::SymbolId id) {
    return std::string(name) + "#" + std::to_string(id);
}

inline std::string display_place(std::string_view place) {
    if (place.starts_with("<external:"))
        return std::string(place);
    const auto dot = place.find('.');
    const std::string_view root = place.substr(0, dot);
    const auto hash = root.rfind('#');
    std::string shown =
        hash == std::string_view::npos ? std::string(root) : std::string(root.substr(0, hash));
    if (dot != std::string_view::npos)
        shown += std::string(place.substr(dot));
    return shown;
}

inline std::vector<std::string> split_place(std::string_view place) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto dot = place.find('.', start);
        if (dot == std::string_view::npos) {
            parts.push_back(std::string(place.substr(start)));
            break;
        }
        parts.push_back(std::string(place.substr(start, dot - start)));
        start = dot + 1;
    }
    return parts;
}
} // namespace uinx::borrow_detail
