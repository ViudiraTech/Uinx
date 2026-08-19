// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/parser.hpp"

#include <algorithm>

namespace uinx::ast {

std::string TypeRef::str() const {
    std::string out;
    if (!prefixes.empty()) {
        for (const auto& prefix : prefixes) {
            if (prefix == "&")
                out += "ref ";
            else if (prefix == "&mut")
                out += "mutref ";
            else if (prefix == "*const")
                out += "ptr ";
            else if (prefix == "*mut")
                out += "mutptr ";
            else
                out += prefix + " ";
        }
    } else {
        if (is_ref)
            out += is_mut_ref ? "mutref " : "ref ";
        if (is_raw_ptr)
            out += raw_mut ? "mutptr " : "ptr ";
    }

    out += name;
    if (!args.empty()) {
        out += '[';
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0)
                out += ", ";
            out += args[i].str();
        }
        out += ']';
    }
    if (nullable)
        out += '?';
    return out;
}

} // namespace uinx::ast

namespace uinx {

Parser::Parser(std::vector<Token> tokens, Diagnostics& diagnostics)
    : toks_(std::move(tokens)), diags_(diagnostics) {
}

const Token& Parser::peek(std::size_t n) const {
    const auto index = std::min(pos_ + n, toks_.size() - 1);
    return toks_[index];
}

bool Parser::at(TokenKind kind) const {
    return peek().kind == kind;
}

bool Parser::consume(TokenKind kind) {
    if (!at(kind))
        return false;
    ++pos_;
    return true;
}

const Token& Parser::advance() {
    const Token& token = peek();
    if (pos_ < toks_.size() - 1)
        ++pos_;
    return token;
}

Token Parser::expect(TokenKind kind, std::string_view what) {
    if (at(kind))
        return advance();
    const Token token = peek();
    diags_.error(token.range,
                 "E0100",
                 "expected " + std::string(what) + ", found " +
                     std::string(token_name(token.kind)));
    return token;
}

Token Parser::expect_name(std::string_view what) {
    if (at(TokenKind::Identifier) || at(TokenKind::KwPtr))
        return advance();
    const Token token = peek();
    diags_.error(token.range,
                 "E0100",
                 "expected " + std::string(what) + ", found " +
                     std::string(token_name(token.kind)));
    return token;
}

void Parser::skip_newlines() {
    while (consume(TokenKind::Newline)) {
    }
}

void Parser::finish_line() {
    if (consume(TokenKind::Semicolon)) {
        skip_newlines();
        return;
    }
    if (consume(TokenKind::Newline)) {
        skip_newlines();
        return;
    }
    if (at(TokenKind::Dedent) || at(TokenKind::RBrace) || at(TokenKind::End))
        return;
    diags_.error(peek().range, "E0111", "expected end of line");
}

void Parser::synchronize_item() {
    while (!at(TokenKind::End)) {
        if (consume(TokenKind::Newline) || consume(TokenKind::Semicolon))
            return;
        if (at(TokenKind::Dedent))
            return;
        if (at(TokenKind::KwFn) || at(TokenKind::KwStruct) || at(TokenKind::KwTrait) ||
            at(TokenKind::KwImpl) || at(TokenKind::KwPub) || at(TokenKind::KwNeed) ||
            at(TokenKind::KwDontNeed)) {
            return;
        }
        advance();
    }
}

Parser::Suite Parser::begin_suite() {
    if (consume(TokenKind::LBrace))
        return {true};
    expect(TokenKind::Colon, "':' before an indented block");
    expect(TokenKind::Newline, "a newline after ':'");
    skip_newlines();
    expect(TokenKind::Indent, "an indented block");
    skip_newlines();
    return {false};
}

bool Parser::suite_done(const Suite& suite) {
    skip_newlines();
    return suite.braces ? at(TokenKind::RBrace) || at(TokenKind::End)
                        : at(TokenKind::Dedent) || at(TokenKind::End);
}

void Parser::end_suite(const Suite& suite) {
    if (suite.braces)
        expect(TokenKind::RBrace, "'}'");
    else
        expect(TokenKind::Dedent, "end of indented block");
    skip_newlines();
}

std::string Parser::parse_requirement_name() {
    if (at(TokenKind::Identifier)) {
        std::string name = advance().text;
        while (consume(TokenKind::Dot)) {
            name += '.';
            name += expect(TokenKind::Identifier, "component name after '.'").text;
        }
        return name;
    }
    if (at(TokenKind::String))
        return advance().text;
    diags_.error(peek().range, "E0112", "need/dontneed requires a component name");
    return {};
}

void Parser::parse_requirement(ast::Module& module, bool disabled) {
    const Token directive = advance();
    std::string name;
    if (directive.text == "no_std")
        name = "std";
    else
        name = parse_requirement_name();

    if (!name.empty()) {
        auto& current = disabled ? module.dontneeds : module.needs;
        auto& opposite = disabled ? module.needs : module.dontneeds;
        if (std::find(opposite.begin(), opposite.end(), name) != opposite.end()) {
            diags_.error(directive.range,
                         "E0113",
                         "component '" + name + "' cannot be both needed and disabled");
        }
        if (std::find(current.begin(), current.end(), name) == current.end())
            current.push_back(name);
        if (disabled && name == "std")
            module.no_std = true;
    }
    finish_line();
}

void Parser::parse_smp_mode(ast::Module& module) {
    const Token keyword = advance();
    if (consume(TokenKind::KwAuto))
        module.smp_mode = ast::SmpMode::Auto;
    else if (consume(TokenKind::KwManual))
        module.smp_mode = ast::SmpMode::Manual;
    else if (consume(TokenKind::KwStrict))
        module.smp_mode = ast::SmpMode::Strict;
    else
        diags_.error(peek().range, "E0114", "smp expects auto, manual, or strict");
    (void)keyword;
    finish_line();
}

ast::TypeRef Parser::parse_type() {
    ast::TypeRef type;
    type.range.begin = peek().range.begin;

    for (;;) {
        if (consume(TokenKind::KwRef)) {
            type.prefixes.push_back("&");
            continue;
        }
        if (consume(TokenKind::KwMutRef)) {
            type.prefixes.push_back("&mut");
            continue;
        }
        if (consume(TokenKind::KwPtr)) {
            type.prefixes.push_back("*const");
            continue;
        }
        if (consume(TokenKind::KwMutPtr)) {
            type.prefixes.push_back("*mut");
            continue;
        }
        if (consume(TokenKind::AmpMut)) {
            type.prefixes.push_back("&mut");
            continue;
        }
        if (consume(TokenKind::Amp)) {
            type.prefixes.push_back(consume(TokenKind::KwMut) ? "&mut" : "&");
            continue;
        }
        if (consume(TokenKind::Star)) {
            if (consume(TokenKind::KwMut))
                type.prefixes.push_back("*mut");
            else {
                expect(TokenKind::KwConst, "const or mut after '*'");
                type.prefixes.push_back("*const");
            }
            continue;
        }
        break;
    }

    Token name;
    if (at(TokenKind::Identifier) || at(TokenKind::KwSelfTy)) {
        name = advance();
        type.name = name.text;
    } else {
        name = expect(TokenKind::Identifier, "type name");
        type.name = name.text.empty() ? "<error>" : name.text;
    }

    if (at(TokenKind::LBracket)) {
        type.args = parse_type_arguments(TokenKind::LBracket, TokenKind::RBracket);
    } else if (at(TokenKind::Less)) {
        type.args = parse_type_arguments(TokenKind::Less, TokenKind::Greater);
    }

    type.nullable = consume(TokenKind::Question);
    type.range.end = peek().range.begin;
    return type;
}

std::vector<ast::TypeRef> Parser::parse_type_arguments(TokenKind open, TokenKind close) {
    std::vector<ast::TypeRef> args;
    expect(open, "generic argument list");
    if (!at(close)) {
        for (;;) {
            args.push_back(parse_type());
            if (!consume(TokenKind::Comma) || at(close))
                break;
        }
    }
    expect(close, "end of generic argument list");
    return args;
}

std::vector<ast::GenericParam> Parser::parse_generics() {
    TokenKind close;
    if (consume(TokenKind::LBracket))
        close = TokenKind::RBracket;
    else if (consume(TokenKind::Less))
        close = TokenKind::Greater;
    else
        return {};

    std::vector<ast::GenericParam> params;
    while (!at(close) && !at(TokenKind::End)) {
        ast::GenericParam param;
        const Token name = expect(TokenKind::Identifier, "generic parameter");
        param.name = name.text;
        param.range.begin = name.range.begin;
        if (consume(TokenKind::Colon)) {
            do {
                param.bounds.push_back(expect(TokenKind::Identifier, "trait bound").text);
            } while (consume(TokenKind::Plus));
        }
        param.range.end = peek().range.begin;
        params.push_back(std::move(param));
        if (!consume(TokenKind::Comma))
            break;
    }
    expect(close, "end of generic parameter list");
    return params;
}

ast::Param Parser::parse_parameter() {
    ast::Param param;
    param.range.begin = peek().range.begin;

    if (at(TokenKind::Amp) || at(TokenKind::AmpMut)) {
        const bool mut_self = at(TokenKind::AmpMut);
        advance();
        if (!mut_self)
            consume(TokenKind::KwMut);
        const Token self = expect(TokenKind::KwSelf, "self after reference receiver");
        param.name = self.text;
        param.type.name = "Self";
        param.type.prefixes.push_back(mut_self ? "&mut" : "&");
        param.mut = mut_self;
    } else if (at(TokenKind::KwSelf)) {
        param.name = advance().text;
        if (consume(TokenKind::Colon)) {
            param.type = parse_type();
            if (param.type.name == "Self" && !param.type.prefixes.empty() &&
                param.type.prefixes.front() == "&mut") {
                param.mut = true;
            }
        } else {
            param.type.name = "Self";
        }
    } else {
        param.mut = consume(TokenKind::KwMut) || consume(TokenKind::KwVar);
        param.name = expect_name("parameter name").text;
        expect(TokenKind::Colon, "':' after parameter name");
        param.type = parse_type();
    }

    param.range.end = peek().range.begin;
    return param;
}

ast::FunctionDecl Parser::parse_function(
    bool pub, bool unsafe_, bool async_, bool extern_, bool concurrent_, std::string abi) {
    ast::FunctionDecl function;
    function.is_pub = pub;
    function.is_unsafe = unsafe_;
    function.is_async = async_;
    function.is_extern = extern_;
    function.is_concurrent = concurrent_;
    function.abi = std::move(abi);
    function.range.begin = peek().range.begin;

    expect(TokenKind::KwFn, "func");
    function.name = expect(TokenKind::Identifier, "function name").text;
    function.generics = parse_generics();
    expect(TokenKind::LParen, "'('");
    if (!at(TokenKind::RParen)) {
        do {
            function.params.push_back(parse_parameter());
        } while (consume(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "')'");
    if (consume(TokenKind::Arrow))
        function.return_type = parse_type();
    else
        function.return_type.name = "unit";

    if (extern_ && (at(TokenKind::Newline) || at(TokenKind::Semicolon) || at(TokenKind::End))) {
        finish_line();
        function.body.reset();
    } else {
        function.body = parse_block();
    }
    function.range.end = peek().range.begin;
    return function;
}

ast::StructDecl Parser::parse_struct(bool pub) {
    ast::StructDecl structure;
    structure.is_pub = pub;
    structure.range.begin = peek().range.begin;
    expect(TokenKind::KwStruct, "struct");
    structure.name = expect(TokenKind::Identifier, "struct name").text;
    structure.generics = parse_generics();

    const Suite suite = begin_suite();
    while (!suite_done(suite)) {
        ast::FieldDecl field;
        field.range.begin = peek().range.begin;
        field.is_pub = consume(TokenKind::KwPub);
        field.is_shared = consume(TokenKind::KwShared);
        field.name = expect_name("field name").text;
        expect(TokenKind::Colon, "':' after field name");
        field.type = parse_type();
        field.range.end = peek().range.begin;
        structure.fields.push_back(std::move(field));

        if (suite.braces) {
            if (!consume(TokenKind::Comma))
                consume(TokenKind::Semicolon);
        } else {
            finish_line();
        }
    }
    end_suite(suite);
    structure.range.end = peek().range.begin;
    return structure;
}

ast::TraitDecl Parser::parse_trait(bool pub) {
    ast::TraitDecl trait;
    trait.is_pub = pub;
    trait.range.begin = peek().range.begin;
    expect(TokenKind::KwTrait, "trait");
    trait.name = expect(TokenKind::Identifier, "trait name").text;
    trait.generics = parse_generics();

    const Suite suite = begin_suite();
    while (!suite_done(suite)) {
        ast::TraitMethod method;
        method.range.begin = peek().range.begin;
        method.is_unsafe = consume(TokenKind::KwUnsafe);
        expect(TokenKind::KwFn, "func");
        method.name = expect(TokenKind::Identifier, "method name").text;
        method.params = {};
        expect(TokenKind::LParen, "'('");
        if (!at(TokenKind::RParen)) {
            do {
                method.params.push_back(parse_parameter());
            } while (consume(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "')'");
        if (consume(TokenKind::Arrow))
            method.return_type = parse_type();
        else
            method.return_type.name = "unit";
        method.range.end = peek().range.begin;
        trait.methods.push_back(std::move(method));

        if (suite.braces)
            expect(TokenKind::Semicolon, "';'");
        else
            finish_line();
    }
    end_suite(suite);
    trait.range.end = peek().range.begin;
    return trait;
}

ast::ImplDecl Parser::parse_impl() {
    ast::ImplDecl impl;
    impl.range.begin = peek().range.begin;
    const Token keyword = expect(TokenKind::KwImpl, "extend");
    const bool new_style = keyword.text == "extend";

    if (new_style) {
        impl.for_type = parse_type();
        if (consume(TokenKind::KwWith)) {
            impl.trait_name = expect(TokenKind::Identifier, "trait name after 'with'").text;
        }
    } else {
        ast::TypeRef first = parse_type();
        if (consume(TokenKind::KwFor)) {
            impl.trait_name = first.name;
            impl.for_type = parse_type();
        } else {
            impl.for_type = std::move(first);
        }
    }

    const Suite suite = begin_suite();
    while (!suite_done(suite)) {
        if (consume(TokenKind::KwPass)) {
            finish_line();
            continue;
        }
        bool pub_ = false;
        bool unsafe_ = false;
        bool async_ = false;
        bool concurrent_ = false;
        while (at(TokenKind::KwPub) || at(TokenKind::KwUnsafe) || at(TokenKind::KwAsync) ||
               at(TokenKind::KwConcurrent)) {
            if (consume(TokenKind::KwPub))
                pub_ = true;
            else if (consume(TokenKind::KwUnsafe))
                unsafe_ = true;
            else if (consume(TokenKind::KwAsync))
                async_ = true;
            else if (consume(TokenKind::KwConcurrent))
                concurrent_ = true;
        }
        impl.methods.push_back(parse_function(pub_, unsafe_, async_, false, concurrent_));
    }
    end_suite(suite);
    impl.range.end = peek().range.begin;
    return impl;
}

ast::GlobalDecl
Parser::parse_global(bool pub, bool is_const, bool is_static, bool is_shared, bool is_percpu) {
    ast::GlobalDecl global;
    global.is_pub = pub;
    global.is_const = is_const;
    global.is_shared = is_shared;
    global.is_percpu = is_percpu;
    global.range.begin = peek().range.begin;

    if (is_const) {
        expect(TokenKind::KwConst, "const");
        global.is_mut = false;
    } else {
        if (is_static)
            expect(TokenKind::KwStatic, "static");
        if (consume(TokenKind::KwVar))
            global.is_mut = true;
        else if (consume(TokenKind::KwLet))
            global.is_mut = false;
        else {
            diags_.error(peek().range, "E0115", "global declaration requires var or val");
            global.is_mut = true;
        }
    }

    global.name = expect(TokenKind::Identifier, "global name").text;
    expect(TokenKind::Colon, "':' after global name");
    global.type = parse_type();
    expect(TokenKind::Eq, "'=' in global declaration");
    global.init = parse_expr();
    global.range.end = peek().range.begin;
    finish_line();
    return global;
}

ast::Module Parser::parse_module(std::string file) {
    ast::Module module;
    module.file = std::move(file);
    skip_newlines();

    while (!at(TokenKind::End)) {
        if (at(TokenKind::KwNeed)) {
            parse_requirement(module, false);
            continue;
        }
        if (at(TokenKind::KwDontNeed)) {
            parse_requirement(module, true);
            continue;
        }
        if (at(TokenKind::KwSmp)) {
            parse_smp_mode(module);
            continue;
        }

        bool pub = false;
        bool unsafe_ = false;
        bool async_ = false;
        bool extern_ = false;
        bool concurrent_ = false;
        bool shared_ = false;
        bool percpu_ = false;
        std::string abi = "Uinx";
        bool progressed = true;
        while (progressed) {
            progressed = false;
            if (consume(TokenKind::KwPub)) {
                pub = true;
                progressed = true;
            } else if (consume(TokenKind::KwUnsafe)) {
                unsafe_ = true;
                progressed = true;
            } else if (consume(TokenKind::KwAsync)) {
                async_ = true;
                progressed = true;
            } else if (consume(TokenKind::KwExtern)) {
                extern_ = true;
                abi = "C";
                if (at(TokenKind::String))
                    abi = advance().text;
                progressed = true;
            } else if (consume(TokenKind::KwConcurrent)) {
                concurrent_ = true;
                progressed = true;
            } else if (consume(TokenKind::KwShared)) {
                shared_ = true;
                progressed = true;
            } else if (consume(TokenKind::KwPerCpu)) {
                percpu_ = true;
                progressed = true;
            }
        }

        if (at(TokenKind::KwFn)) {
            module.items.emplace_back(
                parse_function(pub, unsafe_, async_, extern_, concurrent_, std::move(abi)));
        } else if (at(TokenKind::KwConst)) {
            module.items.emplace_back(parse_global(pub, true, false, shared_, percpu_));
        } else if (at(TokenKind::KwStatic)) {
            module.items.emplace_back(parse_global(pub, false, true, shared_, percpu_));
        } else if ((shared_ || percpu_) && (at(TokenKind::KwVar) || at(TokenKind::KwLet))) {
            module.items.emplace_back(parse_global(pub, false, false, shared_, percpu_));
        } else if (extern_) {
            diags_.error(peek().range, "E0101", "extern currently requires a function declaration");
            synchronize_item();
        } else if (at(TokenKind::KwStruct)) {
            module.items.emplace_back(parse_struct(pub));
        } else if (at(TokenKind::KwTrait)) {
            module.items.emplace_back(parse_trait(pub));
        } else if (at(TokenKind::KwImpl)) {
            module.items.emplace_back(parse_impl());
        } else {
            diags_.error(peek().range, "E0102", "expected a top-level declaration");
            synchronize_item();
        }
        skip_newlines();
    }

    return module;
}

std::unique_ptr<ast::BlockStmt> Parser::parse_block() {
    auto block = std::make_unique<ast::BlockStmt>();
    block->range.begin = peek().range.begin;
    const Suite suite = begin_suite();
    while (!suite_done(suite))
        block->stmts.push_back(parse_stmt());
    end_suite(suite);
    block->range.end = peek().range.begin;
    return block;
}

std::unique_ptr<ast::IfStmt> Parser::parse_if_statement(Token keyword) {
    auto statement = std::make_unique<ast::IfStmt>();
    statement->range.begin = keyword.range.begin;
    statement->condition = parse_expr();
    statement->then_block = parse_block();

    if (at(TokenKind::KwElse)) {
        const Token else_token = advance();
        if (else_token.text == "elif" || consume(TokenKind::KwIf)) {
            auto nested_block = std::make_unique<ast::BlockStmt>();
            auto nested = parse_if_statement(else_token);
            nested_block->range = nested->range;
            nested_block->stmts.push_back(std::move(nested));
            statement->else_block = std::move(nested_block);
        } else {
            statement->else_block = parse_block();
        }
    }
    statement->range.end = peek().range.begin;
    return statement;
}

ast::StmtPtr Parser::parse_stmt() {
    skip_newlines();

    if (at(TokenKind::KwLet) || at(TokenKind::KwVar)) {
        const Token keyword = advance();
        auto statement = std::make_unique<ast::LetStmt>();
        statement->range.begin = keyword.range.begin;
        if (keyword.kind == TokenKind::KwVar)
            statement->mut = true;
        else if (keyword.text == "let")
            statement->mut = consume(TokenKind::KwMut);
        statement->name = expect(TokenKind::Identifier, "binding name").text;
        if (consume(TokenKind::Colon))
            statement->type = parse_type();
        if (consume(TokenKind::Eq))
            statement->init = parse_expr();
        else
            diags_.error(peek().range, "E0103", "bindings require an initializer");
        statement->range.end = peek().range.begin;
        finish_line();
        return statement;
    }

    if (at(TokenKind::KwReturn)) {
        auto statement = std::make_unique<ast::ReturnStmt>();
        statement->range.begin = advance().range.begin;
        if (!at(TokenKind::Newline) && !at(TokenKind::Semicolon) && !at(TokenKind::Dedent) &&
            !at(TokenKind::RBrace)) {
            statement->value = parse_expr();
        }
        statement->range.end = peek().range.begin;
        finish_line();
        return statement;
    }

    if (at(TokenKind::KwIf)) {
        const Token keyword = advance();
        return parse_if_statement(keyword);
    }

    if (at(TokenKind::KwWhile)) {
        auto statement = std::make_unique<ast::WhileStmt>();
        statement->range.begin = advance().range.begin;
        statement->condition = parse_expr();
        statement->body = parse_block();
        statement->range.end = peek().range.begin;
        return statement;
    }

    if (at(TokenKind::KwFor)) {
        auto statement = std::make_unique<ast::ForStmt>();
        statement->range.begin = advance().range.begin;
        statement->name = expect(TokenKind::Identifier, "loop variable").text;
        expect(TokenKind::KwIn, "in");
        statement->begin = parse_expr(0);
        if (consume(TokenKind::DotDotEq))
            statement->inclusive = true;
        else
            expect(TokenKind::DotDot, "'..' or '..=' in for range");
        statement->end = parse_expr(0);
        statement->body = parse_block();
        statement->range.end = statement->body->range.end;
        return statement;
    }

    if (at(TokenKind::KwLoop)) {
        auto statement = std::make_unique<ast::LoopStmt>();
        statement->range.begin = advance().range.begin;
        statement->body = parse_block();
        statement->range.end = statement->body->range.end;
        return statement;
    }

    if (at(TokenKind::KwBreak)) {
        auto statement = std::make_unique<ast::BreakStmt>();
        statement->range = advance().range;
        finish_line();
        return statement;
    }

    if (at(TokenKind::KwContinue)) {
        auto statement = std::make_unique<ast::ContinueStmt>();
        statement->range = advance().range;
        finish_line();
        return statement;
    }

    if (at(TokenKind::KwFence) || at(TokenKind::KwCompilerFence)) {
        auto statement = std::make_unique<ast::FenceStmt>();
        statement->range.begin = peek().range.begin;
        statement->compiler_only = advance().kind == TokenKind::KwCompilerFence;
        const Token order = expect(TokenKind::Identifier, "memory order");
        statement->order = order.text;
        if (statement->order != "acquire" && statement->order != "release" &&
            statement->order != "acq_rel" && statement->order != "seq_cst") {
            diags_.error(
                order.range, "E0115", "fence order must be acquire, release, acq_rel, or seq_cst");
        }
        statement->range.end = order.range.end;
        finish_line();
        return statement;
    }

    if (at(TokenKind::KwUnsafe)) {
        auto statement = std::make_unique<ast::UnsafeStmt>();
        statement->range.begin = advance().range.begin;
        statement->body = parse_block();
        statement->range.end = statement->body->range.end;
        return statement;
    }

    if (at(TokenKind::LBrace))
        return parse_block();

    if (at(TokenKind::KwScope)) {
        advance();
        return parse_block();
    }

    if (at(TokenKind::KwPass)) {
        const Token token = advance();
        auto statement = std::make_unique<ast::BlockStmt>();
        statement->range = token.range;
        finish_line();
        return statement;
    }

    auto expression = parse_expr();
    if (at(TokenKind::Eq) || at(TokenKind::PlusEq) || at(TokenKind::MinusEq) ||
        at(TokenKind::StarEq) || at(TokenKind::SlashEq) || at(TokenKind::PercentEq) ||
        at(TokenKind::AmpEq) || at(TokenKind::PipeEq) || at(TokenKind::CaretEq) ||
        at(TokenKind::ShiftLeftEq) || at(TokenKind::ShiftRightEq)) {
        const Token op = advance();
        auto value = parse_expr();
        auto statement =
            std::make_unique<ast::AssignStmt>(std::move(expression), op.text, std::move(value));
        statement->range = {statement->target->range.begin, statement->value->range.end};
        finish_line();
        return statement;
    }

    auto statement = std::make_unique<ast::ExprStmt>(std::move(expression));
    statement->range = statement->expr->range;
    finish_line();
    return statement;
}

int Parser::precedence(TokenKind kind) const {
    switch (kind) {
        case TokenKind::OrOr:
            return 1;
        case TokenKind::AndAnd:
            return 2;
        case TokenKind::Pipe:
            return 3;
        case TokenKind::Caret:
            return 4;
        case TokenKind::Amp:
            return 5;
        case TokenKind::EqEq:
        case TokenKind::BangEq:
            return 6;
        case TokenKind::Less:
        case TokenKind::LessEq:
        case TokenKind::Greater:
        case TokenKind::GreaterEq:
            return 7;
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
            return 8;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 9;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            return 10;
        default:
            return -1;
    }
}

ast::ExprPtr Parser::parse_expr(int min_precedence) {
    auto lhs = parse_postfix(parse_prefix());
    for (;;) {
        const int current_precedence = precedence(peek().kind);
        if (current_precedence < min_precedence)
            break;
        const std::string op = peek().text;
        advance();
        auto rhs = parse_expr(current_precedence + 1);
        auto binary = std::make_unique<ast::BinaryExpr>(op, std::move(lhs), std::move(rhs));
        binary->range = {binary->lhs->range.begin, binary->rhs->range.end};
        lhs = std::move(binary);
    }

    if (consume(TokenKind::KwAs)) {
        auto type = parse_type();
        auto cast = std::make_unique<ast::CastExpr>(std::move(lhs), std::move(type));
        cast->range = {cast->value->range.begin, peek().range.begin};
        lhs = std::move(cast);
    }
    return lhs;
}

ast::ExprPtr Parser::parse_new() {
    const Token keyword = advance();
    const Token name = expect(TokenKind::Identifier, "type name after 'new'");
    auto expression = std::make_unique<ast::StructLiteralExpr>(name.text);
    expression->range.begin = keyword.range.begin;

    if (at(TokenKind::LBracket)) {
        expression->generic_args = parse_type_arguments(TokenKind::LBracket, TokenKind::RBracket);
    } else if (at(TokenKind::Less)) {
        expression->generic_args = parse_type_arguments(TokenKind::Less, TokenKind::Greater);
    }

    expect(TokenKind::LParen, "'(' after struct type");
    if (!at(TokenKind::RParen)) {
        for (;;) {
            ast::FieldInit field;
            field.range.begin = peek().range.begin;
            field.name = expect_name("field name").text;
            expect(TokenKind::Eq, "'=' after field name");
            field.value = parse_expr();
            field.range.end = field.value->range.end;
            expression->fields.push_back(std::move(field));
            if (!consume(TokenKind::Comma) || at(TokenKind::RParen))
                break;
        }
    }
    const Token close = expect(TokenKind::RParen, "')'");
    expression->range.end = close.range.end;
    return expression;
}

ast::ExprPtr Parser::parse_prefix() {
    const Token token = peek();

    if (at(TokenKind::Integer)) {
        advance();
        auto expression = std::make_unique<ast::IntegerExpr>(token.text);
        expression->range = token.range;
        return expression;
    }
    if (at(TokenKind::Float)) {
        advance();
        auto expression = std::make_unique<ast::FloatExpr>(token.text);
        expression->range = token.range;
        return expression;
    }
    if (at(TokenKind::String)) {
        advance();
        auto expression = std::make_unique<ast::StringExpr>(token.text);
        expression->range = token.range;
        return expression;
    }
    if (at(TokenKind::Char)) {
        advance();
        auto expression = std::make_unique<ast::CharExpr>(token.text);
        expression->range = token.range;
        return expression;
    }
    if (at(TokenKind::KwTrue) || at(TokenKind::KwFalse)) {
        advance();
        auto expression = std::make_unique<ast::BoolExpr>(token.kind == TokenKind::KwTrue);
        expression->range = token.range;
        return expression;
    }
    if (at(TokenKind::KwNew))
        return parse_new();

    if (at(TokenKind::Identifier) || at(TokenKind::KwSelf) || at(TokenKind::KwPtr)) {
        advance();
        if (token.kind == TokenKind::Identifier || token.kind == TokenKind::KwPtr) {
            bool struct_literal = at(TokenKind::LBrace);
            if (!struct_literal && at(TokenKind::Less)) {
                std::size_t scan = pos_;
                int depth = 0;
                for (; scan < toks_.size(); ++scan) {
                    if (toks_[scan].kind == TokenKind::Less)
                        ++depth;
                    else if (toks_[scan].kind == TokenKind::Greater) {
                        if (--depth == 0) {
                            struct_literal = scan + 1 < toks_.size() &&
                                             toks_[scan + 1].kind == TokenKind::LBrace;
                            break;
                        }
                    } else if (depth == 0) {
                        break;
                    }
                }
            }
            if (struct_literal) {
                auto expression = std::make_unique<ast::StructLiteralExpr>(token.text);
                expression->range.begin = token.range.begin;
                if (at(TokenKind::Less)) {
                    expression->generic_args =
                        parse_type_arguments(TokenKind::Less, TokenKind::Greater);
                }
                expect(TokenKind::LBrace, "'{'");
                while (!at(TokenKind::RBrace) && !at(TokenKind::End)) {
                    ast::FieldInit field;
                    field.range.begin = peek().range.begin;
                    field.name = expect_name("field name").text;
                    expect(TokenKind::Colon, "':'");
                    field.value = parse_expr();
                    field.range.end = field.value->range.end;
                    expression->fields.push_back(std::move(field));
                    if (!consume(TokenKind::Comma))
                        break;
                }
                const Token close = expect(TokenKind::RBrace, "'}'");
                expression->range.end = close.range.end;
                return expression;
            }
        }
        auto expression = std::make_unique<ast::NameExpr>(token.text);
        expression->range = token.range;
        return expression;
    }

    if (at(TokenKind::KwAsm))
        return parse_asm();

    if (consume(TokenKind::LParen)) {
        auto expression = parse_expr();
        expect(TokenKind::RParen, "')'");
        return expression;
    }

    if (at(TokenKind::KwBorrow)) {
        const SourceLoc begin = advance().range.begin;
        const bool mut = consume(TokenKind::KwMut);
        auto target = parse_postfix(parse_prefix());
        auto expression = std::make_unique<ast::BorrowExpr>(std::move(target), mut);
        expression->range = {begin, expression->target->range.end};
        return expression;
    }

    if (at(TokenKind::Amp) || at(TokenKind::AmpMut)) {
        const bool mut = at(TokenKind::AmpMut);
        const SourceLoc begin = advance().range.begin;
        auto target = parse_postfix(parse_prefix());
        auto expression = std::make_unique<ast::BorrowExpr>(std::move(target), mut);
        expression->range = {begin, expression->target->range.end};
        return expression;
    }

    if (at(TokenKind::KwAwait)) {
        const SourceLoc begin = advance().range.begin;
        auto value = parse_postfix(parse_prefix());
        auto expression = std::make_unique<ast::AwaitExpr>(std::move(value));
        expression->range = {begin, expression->value->range.end};
        return expression;
    }

    if (at(TokenKind::KwDeref)) {
        const Token op = advance();
        auto operand = parse_postfix(parse_prefix());
        auto expression = std::make_unique<ast::UnaryExpr>("*", std::move(operand));
        expression->range = {op.range.begin, expression->operand->range.end};
        return expression;
    }

    if (at(TokenKind::Minus) || at(TokenKind::Bang) || at(TokenKind::Tilde) ||
        at(TokenKind::Star)) {
        const Token op = advance();
        auto operand = parse_postfix(parse_prefix());
        auto expression = std::make_unique<ast::UnaryExpr>(op.text, std::move(operand));
        expression->range = {op.range.begin, expression->operand->range.end};
        return expression;
    }

    diags_.error(token.range, "E0105", "expected expression");
    advance();
    auto error = std::make_unique<ast::IntegerExpr>("0");
    error->range = token.range;
    return error;
}

ast::ExprPtr Parser::parse_postfix(ast::ExprPtr base) {
    for (;;) {
        if ((at(TokenKind::Less) || at(TokenKind::LBracket)) && base->kind == ast::ExprKind::Name) {
            const TokenKind open = peek().kind;
            const TokenKind close =
                open == TokenKind::Less ? TokenKind::Greater : TokenKind::RBracket;
            std::size_t scan = pos_;
            int depth = 0;
            bool generic_call = false;
            for (; scan < toks_.size(); ++scan) {
                if (open == TokenKind::Less &&
                    (toks_[scan].kind == TokenKind::Newline ||
                     toks_[scan].kind == TokenKind::Colon ||
                     toks_[scan].kind == TokenKind::Dedent || toks_[scan].kind == TokenKind::End))
                    break;
                if (toks_[scan].kind == open) {
                    ++depth;
                } else if (toks_[scan].kind == close) {
                    --depth;
                    if (depth == 0) {
                        generic_call =
                            scan + 1 < toks_.size() && toks_[scan + 1].kind == TokenKind::LParen;
                        break;
                    }
                }
            }
            if (generic_call) {
                auto generic_args = parse_type_arguments(open, close);
                auto call = std::make_unique<ast::CallExpr>(std::move(base));
                call->generic_args = std::move(generic_args);
                expect(TokenKind::LParen, "'('");
                if (!at(TokenKind::RParen)) {
                    for (;;) {
                        call->args.push_back(parse_expr());
                        if (!consume(TokenKind::Comma) || at(TokenKind::RParen))
                            break;
                    }
                }
                const Token close_paren = expect(TokenKind::RParen, "')'");
                call->range = {call->callee->range.begin, close_paren.range.end};
                base = std::move(call);
                continue;
            }
        }

        if (consume(TokenKind::LParen)) {
            auto call = std::make_unique<ast::CallExpr>(std::move(base));
            if (!at(TokenKind::RParen)) {
                for (;;) {
                    call->args.push_back(parse_expr());
                    if (!consume(TokenKind::Comma) || at(TokenKind::RParen))
                        break;
                }
            }
            const Token close = expect(TokenKind::RParen, "')'");
            call->range = {call->callee->range.begin, close.range.end};
            base = std::move(call);
            continue;
        }

        if (consume(TokenKind::Dot)) {
            const Token name = expect_name("member name");
            auto member = std::make_unique<ast::MemberExpr>(std::move(base), name.text);
            member->range = {member->base->range.begin, name.range.end};
            base = std::move(member);
            continue;
        }

        if (consume(TokenKind::LBracket)) {
            auto index = parse_expr();
            const Token close = expect(TokenKind::RBracket, "']'");
            auto expression = std::make_unique<ast::IndexExpr>(std::move(base), std::move(index));
            expression->range = {expression->base->range.begin, close.range.end};
            base = std::move(expression);
            continue;
        }
        break;
    }
    return base;
}

ast::ExprPtr Parser::parse_asm() {
    const Token keyword = advance();
    expect(TokenKind::LParen, "'('");
    const Token text = expect(TokenKind::String, "assembly template");
    auto assembly = std::make_unique<ast::AsmExpr>(text.text);
    assembly->range.begin = keyword.range.begin;

    while (consume(TokenKind::Comma)) {
        if (at(TokenKind::Identifier) && peek().text == "volatile") {
            advance();
            assembly->is_volatile = true;
            continue;
        }

        Token kind;
        if (at(TokenKind::KwIn))
            kind = advance();
        else
            kind = expect(TokenKind::Identifier, "asm operand kind");

        if (kind.text == "clobber") {
            expect(TokenKind::LParen, "'('");
            const Token constraint = expect(TokenKind::String, "clobber name");
            const Token close = expect(TokenKind::RParen, "')'");
            ast::AsmOperand operand;
            operand.kind = ast::AsmOperand::Kind::Clobber;
            operand.constraint = constraint.text;
            operand.range = {kind.range.begin, close.range.end};
            assembly->operands.push_back(std::move(operand));
            continue;
        }

        expect(TokenKind::LParen, "'('");
        const Token constraint = expect(TokenKind::String, "register constraint");
        expect(TokenKind::Comma, "','");
        ast::AsmOperand operand;
        operand.constraint = constraint.text;
        operand.range.begin = kind.range.begin;
        if (kind.text == "in") {
            operand.kind = ast::AsmOperand::Kind::In;
            operand.value = parse_expr();
        } else if (kind.text == "out" || kind.text == "inout") {
            operand.kind =
                kind.text == "out" ? ast::AsmOperand::Kind::Out : ast::AsmOperand::Kind::InOut;
            operand.out_name = expect(TokenKind::Identifier, "output variable").text;
        } else {
            diags_.error(kind.range, "E0110", "asm operand must be in, out, inout, or clobber");
        }
        const Token close = expect(TokenKind::RParen, "')'");
        operand.range.end = close.range.end;
        assembly->operands.push_back(std::move(operand));
    }

    const Token close = expect(TokenKind::RParen, "')'");
    assembly->range.end = close.range.end;
    return assembly;
}

} // namespace uinx
