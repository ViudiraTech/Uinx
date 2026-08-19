// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/lexer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace uinx {

std::string_view token_name(TokenKind kind) {
#define UINX_TOKEN_CASE(name)                                                                      \
    case TokenKind::name:                                                                          \
        return #name
    switch (kind) {
        UINX_TOKEN_CASE(End);
        UINX_TOKEN_CASE(Newline);
        UINX_TOKEN_CASE(Indent);
        UINX_TOKEN_CASE(Dedent);
        UINX_TOKEN_CASE(Identifier);
        UINX_TOKEN_CASE(Integer);
        UINX_TOKEN_CASE(Float);
        UINX_TOKEN_CASE(String);
        UINX_TOKEN_CASE(Char);
        UINX_TOKEN_CASE(KwFn);
        UINX_TOKEN_CASE(KwLet);
        UINX_TOKEN_CASE(KwVar);
        UINX_TOKEN_CASE(KwMut);
        UINX_TOKEN_CASE(KwReturn);
        UINX_TOKEN_CASE(KwIf);
        UINX_TOKEN_CASE(KwElse);
        UINX_TOKEN_CASE(KwWhile);
        UINX_TOKEN_CASE(KwFor);
        UINX_TOKEN_CASE(KwIn);
        UINX_TOKEN_CASE(KwStruct);
        UINX_TOKEN_CASE(KwEnum);
        UINX_TOKEN_CASE(KwTrait);
        UINX_TOKEN_CASE(KwImpl);
        UINX_TOKEN_CASE(KwWith);
        UINX_TOKEN_CASE(KwPub);
        UINX_TOKEN_CASE(KwUnsafe);
        UINX_TOKEN_CASE(KwAsync);
        UINX_TOKEN_CASE(KwAwait);
        UINX_TOKEN_CASE(KwExtern);
        UINX_TOKEN_CASE(KwConst);
        UINX_TOKEN_CASE(KwStatic);
        UINX_TOKEN_CASE(KwMove);
        UINX_TOKEN_CASE(KwWhere);
        UINX_TOKEN_CASE(KwAs);
        UINX_TOKEN_CASE(KwTrue);
        UINX_TOKEN_CASE(KwFalse);
        UINX_TOKEN_CASE(KwNeed);
        UINX_TOKEN_CASE(KwDontNeed);
        UINX_TOKEN_CASE(KwUse);
        UINX_TOKEN_CASE(KwMod);
        UINX_TOKEN_CASE(KwSelf);
        UINX_TOKEN_CASE(KwSelfTy);
        UINX_TOKEN_CASE(KwDyn);
        UINX_TOKEN_CASE(KwAsm);
        UINX_TOKEN_CASE(KwRef);
        UINX_TOKEN_CASE(KwMutRef);
        UINX_TOKEN_CASE(KwPtr);
        UINX_TOKEN_CASE(KwMutPtr);
        UINX_TOKEN_CASE(KwBorrow);
        UINX_TOKEN_CASE(KwDeref);
        UINX_TOKEN_CASE(KwNew);
        UINX_TOKEN_CASE(KwScope);
        UINX_TOKEN_CASE(KwPass);
        UINX_TOKEN_CASE(LParen);
        UINX_TOKEN_CASE(RParen);
        UINX_TOKEN_CASE(LBrace);
        UINX_TOKEN_CASE(RBrace);
        UINX_TOKEN_CASE(LBracket);
        UINX_TOKEN_CASE(RBracket);
        UINX_TOKEN_CASE(Comma);
        UINX_TOKEN_CASE(Dot);
        UINX_TOKEN_CASE(Colon);
        UINX_TOKEN_CASE(Semicolon);
        UINX_TOKEN_CASE(ColonColon);
        UINX_TOKEN_CASE(Arrow);
        UINX_TOKEN_CASE(FatArrow);
        UINX_TOKEN_CASE(Plus);
        UINX_TOKEN_CASE(Minus);
        UINX_TOKEN_CASE(Star);
        UINX_TOKEN_CASE(Slash);
        UINX_TOKEN_CASE(Percent);
        UINX_TOKEN_CASE(Amp);
        UINX_TOKEN_CASE(Pipe);
        UINX_TOKEN_CASE(Caret);
        UINX_TOKEN_CASE(Bang);
        UINX_TOKEN_CASE(Tilde);
        UINX_TOKEN_CASE(Eq);
        UINX_TOKEN_CASE(EqEq);
        UINX_TOKEN_CASE(BangEq);
        UINX_TOKEN_CASE(Less);
        UINX_TOKEN_CASE(LessEq);
        UINX_TOKEN_CASE(Greater);
        UINX_TOKEN_CASE(GreaterEq);
        UINX_TOKEN_CASE(AndAnd);
        UINX_TOKEN_CASE(OrOr);
        UINX_TOKEN_CASE(PlusEq);
        UINX_TOKEN_CASE(MinusEq);
        UINX_TOKEN_CASE(StarEq);
        UINX_TOKEN_CASE(SlashEq);
        UINX_TOKEN_CASE(AmpMut);
        UINX_TOKEN_CASE(Question);
    }
#undef UINX_TOKEN_CASE
    return "unknown";
}

Lexer::Lexer(std::string file, std::string_view source, Diagnostics& diagnostics)
    : file_(std::move(file)), src_(source), diags_(diagnostics) {
    loc_.file = file_;
}

char Lexer::peek(std::size_t n) const {
    return eof(n) ? '\0' : src_[pos_ + n];
}

bool Lexer::eof(std::size_t n) const {
    return pos_ + n >= src_.size();
}

char Lexer::advance() {
    if (eof())
        return '\0';
    const char c = src_[pos_++];
    ++loc_.offset;
    if (c == '\n') {
        ++loc_.line;
        loc_.column = 1;
    } else {
        ++loc_.column;
    }
    return c;
}

bool Lexer::match(char c) {
    if (peek() != c)
        return false;
    advance();
    return true;
}

Token Lexer::make(TokenKind kind, std::size_t start, SourceLoc begin, std::string text) {
    if (text.empty() && pos_ > start)
        text = std::string(src_.substr(start, pos_ - start));
    return {kind, std::move(text), {std::move(begin), loc_}};
}

void Lexer::skip_horizontal_space() {
    while (peek() == ' ' || peek() == '\t' || peek() == '\r')
        advance();
}

bool Lexer::skip_comment() {
    if (peek() == '#' || (peek() == '/' && peek(1) == '/')) {
        if (peek() == '/') {
            advance();
            advance();
        } else {
            advance();
        }
        while (!eof() && peek() != '\n')
            advance();
        return true;
    }

    if (peek() != '/' || peek(1) != '*')
        return false;
    const SourceLoc begin = loc_;
    advance();
    advance();
    int depth = 1;
    while (!eof() && depth != 0) {
        if (peek() == '/' && peek(1) == '*') {
            advance();
            advance();
            ++depth;
        } else if (peek() == '*' && peek(1) == '/') {
            advance();
            advance();
            --depth;
        } else {
            advance();
        }
    }
    if (depth != 0)
        diags_.error({begin, loc_}, "E0001", "unterminated block comment");
    return true;
}

bool Lexer::consume_blank_or_comment_line() {
    const std::size_t saved_pos = pos_;
    const SourceLoc saved_loc = loc_;

    while (peek() == ' ' || peek() == '\t' || peek() == '\r')
        advance();
    if (peek() == '#' || (peek() == '/' && peek(1) == '/'))
        skip_comment();

    if (peek() == '\n') {
        advance();
        line_start_ = true;
        return true;
    }
    if (eof())
        return true;

    pos_ = saved_pos;
    loc_ = saved_loc;
    return false;
}

void Lexer::emit_indentation(std::vector<Token>& out) {
    if (!line_start_ || delimiter_depth_ != 0 || eof())
        return;
    if (consume_blank_or_comment_line())
        return;

    const SourceLoc begin = loc_;
    std::size_t width = 0;
    while (peek() == ' ' || peek() == '\t') {
        if (peek() == '\t') {
            diags_.error({loc_, loc_}, "E0006", "tabs are not allowed for indentation; use spaces");
            width += 4;
        } else {
            ++width;
        }
        advance();
    }

    if (width > indent_stack_.back()) {
        indent_stack_.push_back(width);
        out.push_back({TokenKind::Indent, "", {begin, loc_}});
    } else if (width < indent_stack_.back()) {
        while (indent_stack_.size() > 1 && width < indent_stack_.back()) {
            indent_stack_.pop_back();
            out.push_back({TokenKind::Dedent, "", {begin, loc_}});
        }
        if (width != indent_stack_.back()) {
            diags_.error({begin, loc_}, "E0007", "inconsistent indentation level");
        }
    }
    line_start_ = false;
}

void Lexer::emit_newline(std::vector<Token>& out, SourceLoc begin) {
    if (delimiter_depth_ != 0)
        return;
    if (!out.empty() && out.back().kind != TokenKind::Newline &&
        out.back().kind != TokenKind::Indent && out.back().kind != TokenKind::Dedent) {
        out.push_back({TokenKind::Newline, "\n", {std::move(begin), loc_}});
    }
}

Token Lexer::identifier_or_keyword() {
    const std::size_t start = pos_;
    const SourceLoc begin = loc_;
    advance();
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
        advance();
    std::string text(src_.substr(start, pos_ - start));

    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"fn", TokenKind::KwFn},           {"func", TokenKind::KwFn},
        {"let", TokenKind::KwLet},         {"val", TokenKind::KwLet},
        {"var", TokenKind::KwVar},         {"mut", TokenKind::KwMut},
        {"return", TokenKind::KwReturn},   {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},       {"elif", TokenKind::KwElse},
        {"while", TokenKind::KwWhile},     {"for", TokenKind::KwFor},
        {"in", TokenKind::KwIn},           {"struct", TokenKind::KwStruct},
        {"enum", TokenKind::KwEnum},       {"trait", TokenKind::KwTrait},
        {"impl", TokenKind::KwImpl},       {"extend", TokenKind::KwImpl},
        {"with", TokenKind::KwWith},       {"pub", TokenKind::KwPub},
        {"public", TokenKind::KwPub},      {"unsafe", TokenKind::KwUnsafe},
        {"async", TokenKind::KwAsync},     {"await", TokenKind::KwAwait},
        {"extern", TokenKind::KwExtern},   {"const", TokenKind::KwConst},
        {"static", TokenKind::KwStatic},   {"move", TokenKind::KwMove},
        {"where", TokenKind::KwWhere},     {"as", TokenKind::KwAs},
        {"true", TokenKind::KwTrue},       {"false", TokenKind::KwFalse},
        {"need", TokenKind::KwNeed},       {"dontneed", TokenKind::KwDontNeed},
        {"no_std", TokenKind::KwDontNeed}, {"use", TokenKind::KwUse},
        {"mod", TokenKind::KwMod},         {"self", TokenKind::KwSelf},
        {"Self", TokenKind::KwSelfTy},     {"dyn", TokenKind::KwDyn},
        {"asm", TokenKind::KwAsm},         {"ref", TokenKind::KwRef},
        {"mutref", TokenKind::KwMutRef},   {"ptr", TokenKind::KwPtr},
        {"mutptr", TokenKind::KwMutPtr},   {"borrow", TokenKind::KwBorrow},
        {"deref", TokenKind::KwDeref},     {"new", TokenKind::KwNew},
        {"scope", TokenKind::KwScope},     {"pass", TokenKind::KwPass},
    };

    if (text == "and")
        return make(TokenKind::AndAnd, start, begin, "&&");
    if (text == "or")
        return make(TokenKind::OrOr, start, begin, "||");
    if (text == "not")
        return make(TokenKind::Bang, start, begin, "!");

    const auto it = keywords.find(text);
    return make(
        it == keywords.end() ? TokenKind::Identifier : it->second, start, begin, std::move(text));
}

Token Lexer::number() {
    const std::size_t start = pos_;
    const SourceLoc begin = loc_;
    bool is_float = false;

    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B' ||
                          peek(1) == 'o' || peek(1) == 'O')) {
        advance();
        advance();
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
            advance();
        return make(TokenKind::Integer, start, begin);
    }

    while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')
        advance();
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        is_float = true;
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')
            advance();
    }
    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        advance();
        if (peek() == '+' || peek() == '-')
            advance();
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')
            advance();
    }
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
        advance();
    return make(is_float ? TokenKind::Float : TokenKind::Integer, start, begin);
}

Token Lexer::string_literal() {
    const SourceLoc begin = loc_;
    advance();
    std::string value;
    while (!eof() && peek() != '"') {
        char c = advance();
        if (c != '\\') {
            value += c;
            continue;
        }
        if (eof())
            break;
        const char escaped = advance();
        switch (escaped) {
            case 'n':
                value += '\n';
                break;
            case 'r':
                value += '\r';
                break;
            case 't':
                value += '\t';
                break;
            case '\\':
                value += '\\';
                break;
            case '"':
                value += '"';
                break;
            case '0':
                value += '\0';
                break;
            default:
                value += escaped;
                break;
        }
    }
    if (!match('"'))
        diags_.error({begin, loc_}, "E0002", "unterminated string literal");
    return {TokenKind::String, std::move(value), {begin, loc_}};
}

Token Lexer::char_literal() {
    const std::size_t start = pos_;
    const SourceLoc begin = loc_;
    advance();
    std::string value;
    if (eof() || peek() == '\'') {
        diags_.error({begin, loc_}, "E0003", "empty character literal");
    } else {
        char c = advance();
        if (c == '\\') {
            const char escaped = advance();
            switch (escaped) {
                case 'n':
                    value += '\n';
                    break;
                case 't':
                    value += '\t';
                    break;
                default:
                    value += escaped;
                    break;
            }
        } else {
            value += c;
        }
    }
    if (!match('\''))
        diags_.error({begin, loc_}, "E0004", "unterminated character literal");
    return make(TokenKind::Char, start, begin, std::move(value));
}

std::vector<Token> Lexer::lex() {
    std::vector<Token> out;

    while (!eof()) {
        if (line_start_ && delimiter_depth_ == 0) {
            const auto before = pos_;
            emit_indentation(out);
            if (pos_ != before && line_start_)
                continue;
            if (eof())
                break;
        }

        skip_horizontal_space();
        if (skip_comment())
            continue;
        if (eof())
            break;

        if (peek() == '\n') {
            const SourceLoc begin = loc_;
            advance();
            emit_newline(out, begin);
            line_start_ = true;
            continue;
        }

        line_start_ = false;
        const std::size_t start = pos_;
        const SourceLoc begin = loc_;
        const char c = peek();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            out.push_back(identifier_or_keyword());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            out.push_back(number());
            continue;
        }
        if (c == '"') {
            out.push_back(string_literal());
            continue;
        }
        if (c == '\'') {
            out.push_back(char_literal());
            continue;
        }

        advance();
        TokenKind kind = TokenKind::End;
        switch (c) {
            case '(':
                kind = TokenKind::LParen;
                ++delimiter_depth_;
                break;
            case ')':
                kind = TokenKind::RParen;
                delimiter_depth_ = std::max(0, delimiter_depth_ - 1);
                break;
            case '{':
                kind = TokenKind::LBrace;
                ++delimiter_depth_;
                break;
            case '}':
                kind = TokenKind::RBrace;
                delimiter_depth_ = std::max(0, delimiter_depth_ - 1);
                break;
            case '[':
                kind = TokenKind::LBracket;
                ++delimiter_depth_;
                break;
            case ']':
                kind = TokenKind::RBracket;
                delimiter_depth_ = std::max(0, delimiter_depth_ - 1);
                break;
            case ',':
                kind = TokenKind::Comma;
                break;
            case '.':
                kind = TokenKind::Dot;
                break;
            case ';':
                kind = TokenKind::Semicolon;
                break;
            case '?':
                kind = TokenKind::Question;
                break;
            case '~':
                kind = TokenKind::Tilde;
                break;
            case '%':
                kind = TokenKind::Percent;
                break;
            case '^':
                kind = TokenKind::Caret;
                break;
            case ':':
                kind = match(':') ? TokenKind::ColonColon : TokenKind::Colon;
                break;
            case '-':
                kind = match('>') ? TokenKind::Arrow
                                  : (match('=') ? TokenKind::MinusEq : TokenKind::Minus);
                break;
            case '=':
                kind = match('=') ? TokenKind::EqEq
                                  : (match('>') ? TokenKind::FatArrow : TokenKind::Eq);
                break;
            case '+':
                kind = match('=') ? TokenKind::PlusEq : TokenKind::Plus;
                break;
            case '*':
                kind = match('=') ? TokenKind::StarEq : TokenKind::Star;
                break;
            case '/':
                kind = match('=') ? TokenKind::SlashEq : TokenKind::Slash;
                break;
            case '&':
                if (match('&')) {
                    kind = TokenKind::AndAnd;
                } else if (src_.substr(pos_, 3) == "mut" &&
                           (pos_ + 3 == src_.size() ||
                            !std::isalnum(static_cast<unsigned char>(src_[pos_ + 3])))) {
                    advance();
                    advance();
                    advance();
                    kind = TokenKind::AmpMut;
                } else {
                    kind = TokenKind::Amp;
                }
                break;
            case '|':
                kind = match('|') ? TokenKind::OrOr : TokenKind::Pipe;
                break;
            case '!':
                kind = match('=') ? TokenKind::BangEq : TokenKind::Bang;
                break;
            case '<':
                kind = match('=') ? TokenKind::LessEq : TokenKind::Less;
                break;
            case '>':
                kind = match('=') ? TokenKind::GreaterEq : TokenKind::Greater;
                break;
            default:
                diags_.error(
                    {begin, loc_}, "E0005", std::string("unexpected character '") + c + "'");
                continue;
        }
        out.push_back(make(kind, start, begin));
    }

    if (!out.empty() && out.back().kind != TokenKind::Newline &&
        out.back().kind != TokenKind::Dedent && out.back().kind != TokenKind::Indent) {
        out.push_back({TokenKind::Newline, "\n", {loc_, loc_}});
    }
    while (indent_stack_.size() > 1) {
        indent_stack_.pop_back();
        out.push_back({TokenKind::Dedent, "", {loc_, loc_}});
    }
    out.push_back({TokenKind::End, "", {loc_, loc_}});
    return out;
}

} // namespace uinx
