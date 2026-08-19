// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace uinx {
struct SourceLoc {
    std::string file;
    std::uint32_t line{1};
    std::uint32_t column{1};
    std::uint32_t offset{0};
};
struct SourceRange {
    SourceLoc begin;
    SourceLoc end;
};
inline std::string loc_string(const SourceLoc& l) {
    return l.file + ":" + std::to_string(l.line) + ":" + std::to_string(l.column);
}
} // namespace uinx
