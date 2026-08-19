// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once

#include "uinx/ast.hpp"
#include "uinx/diagnostic.hpp"
#include "uinx/token.hpp"

namespace uinx {

class Parser {
  public:
    Parser(std::vector<Token> tokens, Diagnostics& diagnostics);
    ast::Module parse_module(std::string file);

  private:
    struct Suite {
        bool braces{false};
    };

    const Token& peek(std::size_t n = 0) const;
    bool at(TokenKind kind) const;
    bool consume(TokenKind kind);
    const Token& advance();
    Token expect(TokenKind kind, std::string_view what);
    Token expect_name(std::string_view what);
    void skip_newlines();
    void finish_line();
    void synchronize_item();

    Suite begin_suite();
    bool suite_done(const Suite& suite);
    void end_suite(const Suite& suite);

    std::string parse_requirement_name();
    void parse_requirement(ast::Module& module, bool disabled);
    void parse_smp_mode(ast::Module& module);
    ast::TypeRef parse_type();
    std::vector<ast::GenericParam> parse_generics();
    void parse_where_clause(std::vector<ast::GenericParam>& params);
    std::vector<ast::TypeRef> parse_type_arguments(TokenKind open, TokenKind close);

    ast::FunctionDecl parse_function(bool pub,
                                     bool unsafe_,
                                     bool async_,
                                     bool extern_,
                                     bool concurrent_,
                                     std::string abi = {"Uinx"});
    ast::StructDecl parse_struct(bool pub);
    ast::TraitDecl parse_trait(bool pub);
    ast::ImplDecl parse_impl();
    ast::GlobalDecl
    parse_global(bool pub, bool is_const, bool is_static, bool is_shared, bool is_percpu);
    ast::Param parse_parameter();

    std::unique_ptr<ast::BlockStmt> parse_block();
    ast::StmtPtr parse_stmt();
    std::unique_ptr<ast::IfStmt> parse_if_statement(Token keyword);

    ast::ExprPtr parse_expr(int min_precedence = 0);
    ast::ExprPtr parse_prefix();
    ast::ExprPtr parse_postfix(ast::ExprPtr base);
    ast::ExprPtr parse_asm();
    ast::ExprPtr parse_new();
    int precedence(TokenKind kind) const;

    std::vector<Token> toks_;
    Diagnostics& diags_;
    std::size_t pos_{0};
};

} // namespace uinx
