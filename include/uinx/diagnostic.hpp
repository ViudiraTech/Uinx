// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/common.hpp"

#include <mutex>

namespace uinx {
enum class DiagLevel { Note, Warning, Error };
struct Diagnostic {
    DiagLevel level{DiagLevel::Error};
    SourceRange range{};
    std::string code;
    std::string message;
    std::vector<std::string> notes;
};
class Diagnostics {
  public:
    explicit Diagnostics(std::ostream* stream = nullptr) : stream_(stream) {
    }
    void report(Diagnostic d);
    void error(const SourceRange& r, std::string code, std::string msg);
    void warning(const SourceRange& r, std::string code, std::string msg);
    [[nodiscard]] bool has_errors() const;
    [[nodiscard]] std::size_t error_count() const;
    [[nodiscard]] const std::vector<Diagnostic>& all() const {
        return diags_;
    }

  private:
    mutable std::mutex mu_;
    std::ostream* stream_{};
    std::vector<Diagnostic> diags_;
};
} // namespace uinx
