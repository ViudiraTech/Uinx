// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/diagnostic.hpp"

#include <iostream>

namespace uinx {
static const char* level_name(DiagLevel l) {
    switch (l) {
        case DiagLevel::Note:
            return "note";
        case DiagLevel::Warning:
            return "warning";
        case DiagLevel::Error:
            return "error";
    }
    return "error";
}
void Diagnostics::report(Diagnostic d) {
    std::lock_guard lock(mu_);
    if (stream_) {
        *stream_ << loc_string(d.range.begin) << ": " << level_name(d.level) << "[" << d.code
                 << "]: " << d.message << '\n';
        for (const auto& n : d.notes)
            *stream_ << "  note: " << n << '\n';
    }
    diags_.push_back(std::move(d));
}
void Diagnostics::note(const SourceRange& r, std::string code, std::string msg) {
    report({DiagLevel::Note, r, std::move(code), std::move(msg), {}});
}
void Diagnostics::error(const SourceRange& r, std::string code, std::string msg) {
    report({DiagLevel::Error, r, std::move(code), std::move(msg), {}});
}
void Diagnostics::warning(const SourceRange& r, std::string code, std::string msg) {
    report({DiagLevel::Warning, r, std::move(code), std::move(msg), {}});
}
bool Diagnostics::has_errors() const {
    std::lock_guard lock(mu_);
    for (const auto& d : diags_)
        if (d.level == DiagLevel::Error)
            return true;
    return false;
}
std::size_t Diagnostics::error_count() const {
    std::lock_guard lock(mu_);
    std::size_t n = 0;
    for (const auto& d : diags_)
        if (d.level == DiagLevel::Error)
            ++n;
    return n;
}
} // namespace uinx
