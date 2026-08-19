// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once

#include "uinx/diagnostic.hpp"
#include "uinx/token.hpp"

namespace uinx {

class Lexer {
  public:
    Lexer(std::string file, std::string_view source, Diagnostics& diagnostics);
    std::vector<Token> lex();

  private:
    char peek(std::size_t n = 0) const;
    bool eof(std::size_t n = 0) const;
    char advance();
    bool match(char c);

    void skip_horizontal_space();
    bool skip_comment();
    bool consume_blank_or_comment_line();
    void emit_indentation(std::vector<Token>& out);
    void emit_newline(std::vector<Token>& out, SourceLoc begin);

    Token identifier_or_keyword();
    Token number();
    Token string_literal();
    Token char_literal();
    Token make(TokenKind kind, std::size_t start, SourceLoc begin, std::string text = {});

    std::string file_;
    std::string_view src_;
    Diagnostics& diags_;
    std::size_t pos_{0};
    SourceLoc loc_{};
    std::vector<std::size_t> indent_stack_{0};
    int delimiter_depth_{0};
    bool line_start_{true};
};

}  // namespace uinx
