// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Internal helpers shared by the translation units split out of mir.cpp.

#pragma once

#include "uinx/mir.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace uinx::mir_detail {
inline bool unsigned_integer_kind(TypeKind kind) {
    return kind == TypeKind::U8 || kind == TypeKind::U16 || kind == TypeKind::U32 ||
           kind == TypeKind::U64 || kind == TypeKind::U128 || kind == TypeKind::Usize;
}

inline bool builtin_function(std::string_view name) {
    static const std::unordered_set<std::string_view> names{
        "print", "println", "eprint", "eprintln", "input", "str", "int", "float", "bool", "len"};
    return names.contains(name);
}
} // namespace uinx::mir_detail
