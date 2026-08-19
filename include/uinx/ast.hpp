// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/common.hpp"

#include <memory>

namespace uinx::ast {
struct TypeRef {
    SourceRange range{};
    std::string name;
    std::vector<TypeRef> args;
    std::vector<std::string> prefixes;
    bool is_ref{false};
    bool is_mut_ref{false};
    bool is_raw_ptr{false};
    bool raw_mut{false};
    bool nullable{false};
    std::string str() const;
};

enum class ExprKind {
    Integer,
    Float,
    String,
    Bool,
    Char,
    Name,
    StructLiteral,
    Unary,
    Binary,
    Call,
    Member,
    Index,
    Borrow,
    Await,
    Cast,
    Asm
};
struct Expr {
    ExprKind kind;
    SourceRange range{};
    explicit Expr(ExprKind k) : kind(k) {
    }
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;
struct IntegerExpr : Expr {
    std::string value;
    explicit IntegerExpr(std::string v) : Expr(ExprKind::Integer), value(std::move(v)) {
    }
};
struct FloatExpr : Expr {
    std::string value;
    explicit FloatExpr(std::string v) : Expr(ExprKind::Float), value(std::move(v)) {
    }
};
struct StringExpr : Expr {
    std::string value;
    explicit StringExpr(std::string v) : Expr(ExprKind::String), value(std::move(v)) {
    }
};
struct CharExpr : Expr {
    std::string value;
    explicit CharExpr(std::string v) : Expr(ExprKind::Char), value(std::move(v)) {
    }
};
struct BoolExpr : Expr {
    bool value;
    explicit BoolExpr(bool v) : Expr(ExprKind::Bool), value(v) {
    }
};
struct NameExpr : Expr {
    std::string name;
    explicit NameExpr(std::string n) : Expr(ExprKind::Name), name(std::move(n)) {
    }
};
struct FieldInit {
    std::string name;
    ExprPtr value;
    SourceRange range{};
};
struct StructLiteralExpr : Expr {
    std::string name;
    std::vector<TypeRef> generic_args;
    std::vector<FieldInit> fields;
    explicit StructLiteralExpr(std::string n) : Expr(ExprKind::StructLiteral), name(std::move(n)) {
    }
};
struct UnaryExpr : Expr {
    std::string op;
    ExprPtr operand;
    UnaryExpr(std::string o, ExprPtr e)
        : Expr(ExprKind::Unary), op(std::move(o)), operand(std::move(e)) {
    }
};
struct BinaryExpr : Expr {
    std::string op;
    ExprPtr lhs, rhs;
    BinaryExpr(std::string o, ExprPtr a, ExprPtr b)
        : Expr(ExprKind::Binary), op(std::move(o)), lhs(std::move(a)), rhs(std::move(b)) {
    }
};
struct BorrowExpr : Expr {
    ExprPtr target;
    bool mut;
    BorrowExpr(ExprPtr e, bool m) : Expr(ExprKind::Borrow), target(std::move(e)), mut(m) {
    }
};
struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<TypeRef> generic_args;
    std::vector<ExprPtr> args;
    explicit CallExpr(ExprPtr c) : Expr(ExprKind::Call), callee(std::move(c)) {
    }
};
struct MemberExpr : Expr {
    ExprPtr base;
    std::string member;
    MemberExpr(ExprPtr b, std::string m)
        : Expr(ExprKind::Member), base(std::move(b)), member(std::move(m)) {
    }
};
struct IndexExpr : Expr {
    ExprPtr base, index;
    IndexExpr(ExprPtr b, ExprPtr i)
        : Expr(ExprKind::Index), base(std::move(b)), index(std::move(i)) {
    }
};
struct AwaitExpr : Expr {
    ExprPtr value;
    explicit AwaitExpr(ExprPtr v) : Expr(ExprKind::Await), value(std::move(v)) {
    }
};
struct CastExpr : Expr {
    ExprPtr value;
    TypeRef to;
    CastExpr(ExprPtr v, TypeRef t) : Expr(ExprKind::Cast), value(std::move(v)), to(std::move(t)) {
    }
};
struct AsmOperand {
    enum class Kind { In, Out, InOut, Clobber };
    Kind kind;
    std::string constraint;
    ExprPtr value;
    std::string out_name;
    SourceRange range{};
};
struct AsmExpr : Expr {
    std::string assembly;
    std::vector<AsmOperand> operands;
    bool is_volatile{false};
    explicit AsmExpr(std::string a) : Expr(ExprKind::Asm), assembly(std::move(a)) {
    }
};

enum class StmtKind { Let, Assign, Expr, Return, Block, If, While, Unsafe };
struct Stmt {
    StmtKind kind;
    SourceRange range{};
    explicit Stmt(StmtKind k) : kind(k) {
    }
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;
struct LetStmt : Stmt {
    std::string name;
    bool mut{false};
    std::optional<TypeRef> type;
    ExprPtr init;
    LetStmt() : Stmt(StmtKind::Let) {
    }
};
struct AssignStmt : Stmt {
    ExprPtr target;
    std::string op;
    ExprPtr value;
    AssignStmt(ExprPtr t, std::string o, ExprPtr v)
        : Stmt(StmtKind::Assign), target(std::move(t)), op(std::move(o)), value(std::move(v)) {
    }
};
struct ExprStmt : Stmt {
    ExprPtr expr;
    explicit ExprStmt(ExprPtr e) : Stmt(StmtKind::Expr), expr(std::move(e)) {
    }
};
struct ReturnStmt : Stmt {
    ExprPtr value;
    ReturnStmt() : Stmt(StmtKind::Return) {
    }
};
struct BlockStmt : Stmt {
    std::vector<StmtPtr> stmts;
    BlockStmt() : Stmt(StmtKind::Block) {
    }
};
struct IfStmt : Stmt {
    ExprPtr condition;
    std::unique_ptr<BlockStmt> then_block;
    std::unique_ptr<BlockStmt> else_block;
    IfStmt() : Stmt(StmtKind::If) {
    }
};
struct WhileStmt : Stmt {
    ExprPtr condition;
    std::unique_ptr<BlockStmt> body;
    WhileStmt() : Stmt(StmtKind::While) {
    }
};
struct UnsafeStmt : Stmt {
    std::unique_ptr<BlockStmt> body;
    UnsafeStmt() : Stmt(StmtKind::Unsafe) {
    }
};

struct Param {
    std::string name;
    TypeRef type;
    bool mut{false};
    SourceRange range{};
};
struct GenericParam {
    std::string name;
    std::vector<std::string> bounds;
    SourceRange range{};
};
struct FunctionDecl {
    SourceRange range{};
    std::string name;
    std::vector<GenericParam> generics;
    std::vector<Param> params;
    TypeRef return_type;
    std::unique_ptr<BlockStmt> body;
    bool is_pub{false}, is_unsafe{false}, is_async{false}, is_extern{false};
    std::string abi{"Uinx"};
};
struct FieldDecl {
    std::string name;
    TypeRef type;
    bool is_pub{false};
    SourceRange range{};
};
struct StructDecl {
    SourceRange range{};
    std::string name;
    std::vector<GenericParam> generics;
    std::vector<FieldDecl> fields;
    bool is_pub{false};
};
struct TraitMethod {
    std::string name;
    std::vector<Param> params;
    TypeRef return_type;
    bool is_unsafe{false};
    SourceRange range{};
};
struct TraitDecl {
    SourceRange range{};
    std::string name;
    std::vector<GenericParam> generics;
    std::vector<TraitMethod> methods;
    bool is_pub{false};
};
struct ImplDecl {
    SourceRange range{};
    std::optional<std::string> trait_name;
    TypeRef for_type;
    std::vector<FunctionDecl> methods;
};
using Item = std::variant<FunctionDecl, StructDecl, TraitDecl, ImplDecl>;
struct Module {
    std::string file;
    bool no_std{false};
    std::vector<std::string> needs;
    std::vector<std::string> dontneeds;
    std::vector<Item> items;
};
} // namespace uinx::ast
