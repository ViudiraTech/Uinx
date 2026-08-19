// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/lexer.hpp"
#include "uinx/parser.hpp"
#include "uinx/types.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string_view>

using namespace uinx;

namespace {

bool require(bool condition, std::string_view message) {
    if (condition)
        return true;
    std::cerr << "compiler unit failure: " << message << '\n';
    return false;
}

bool contains_token(const std::vector<Token>& tokens, TokenKind kind) {
    return std::any_of(
        tokens.begin(), tokens.end(), [kind](const Token& token) { return token.kind == kind; });
}

} // namespace

int main() {
    constexpr std::string_view source = R"(dontneed std
need core

func identity[T: Copy](value: T) -> T:
    return value

func main() -> i32:
    val answer = identity[i32](40 + 2)
    return answer - 42
)";

    std::ostringstream diagnostics_text;
    Diagnostics diagnostics(&diagnostics_text);
    Lexer lexer("compiler_unit.ux", source, diagnostics);
    auto tokens = lexer.lex();

    if (!require(!diagnostics.has_errors(), diagnostics_text.str()))
        return 1;
    if (!require(contains_token(tokens, TokenKind::Indent), "lexer did not emit Indent"))
        return 1;
    if (!require(contains_token(tokens, TokenKind::Dedent), "lexer did not emit Dedent"))
        return 1;
    if (!require(contains_token(tokens, TokenKind::KwNeed), "lexer did not recognize need"))
        return 1;
    if (!require(contains_token(tokens, TokenKind::KwDontNeed), "lexer did not recognize dontneed"))
        return 1;

    Parser parser(std::move(tokens), diagnostics);
    auto module = parser.parse_module("compiler_unit.ux");
    if (!require(!diagnostics.has_errors(), diagnostics_text.str()))
        return 1;
    if (!require(module.items.size() == 2, "parser did not produce two functions"))
        return 1;
    if (!require(module.no_std, "dontneed std did not set no_std semantics"))
        return 1;
    if (!require(module.needs == std::vector<std::string>{"core"}, "need core was not recorded"))
        return 1;
    if (!require(module.dontneeds == std::vector<std::string>{"std"},
                 "dontneed std was not recorded"))
        return 1;

    if (!require(Type::builtin("i32").str() == "i32",
                 "builtin type rendering changed unexpectedly"))
        return 1;
    if (!require(Type::builtin("u64").is_copy(), "u64 must remain Copy"))
        return 1;
    if (!require(Type::ref(Type::builtin("i32"), true).str() == "mutref i32",
                 "reference diagnostics must use canonical Uinx syntax")) {
        return 1;
    }

    return 0;
}
