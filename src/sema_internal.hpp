// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Internal helpers shared by the translation units split out of sema.cpp.

#pragma once

#include "uinx/sema.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace uinx::sema_detail {
inline Type replace_self(Type type, const Type& owner) {
    if (type.kind == TypeKind::Named && type.name == "Self")
        return owner;
    if (type.pointee)
        type.pointee = std::make_shared<Type>(replace_self(*type.pointee, owner));
    for (auto& arg : type.args)
        arg = replace_self(std::move(arg), owner);
    return type;
}

inline bool integer_literal(const ast::Expr* expr) {
    if (!expr)
        return false;
    if (expr->kind == ast::ExprKind::Integer)
        return true;
    if (expr->kind == ast::ExprKind::Unary) {
        const auto& unary = static_cast<const ast::UnaryExpr&>(*expr);
        return unary.op == "-" && unary.operand && unary.operand->kind == ast::ExprKind::Integer;
    }
    return false;
}

inline bool can_coerce_expr(const ast::Expr* expr, const Type& from, const Type& to) {
    if (can_coerce(from, to))
        return true;
    if (expr && expr->kind == ast::ExprKind::String && from.kind == TypeKind::Ref && from.pointee &&
        from.pointee->kind == TypeKind::Str && to.kind == TypeKind::Str)
        return true;
    return integer_literal(expr) && from.is_integer() && to.is_integer();
}

inline bool static_initializer_expr(const ast::Expr& expr) {
    if (expr.kind == ast::ExprKind::Integer || expr.kind == ast::ExprKind::Float ||
        expr.kind == ast::ExprKind::Bool || expr.kind == ast::ExprKind::Char)
        return true;
    if (expr.kind == ast::ExprKind::StructLiteral) {
        const auto& structure = static_cast<const ast::StructLiteralExpr&>(expr);
        return std::all_of(
            structure.fields.begin(), structure.fields.end(), [](const ast::FieldInit& field) {
                if (!field.value)
                    return false;
                if (field.value->kind == ast::ExprKind::Integer)
                    return static_cast<const ast::IntegerExpr&>(*field.value).value == "0";
                if (field.value->kind == ast::ExprKind::Bool)
                    return !static_cast<const ast::BoolExpr&>(*field.value).value;
                return false;
            });
    }
    return false;
}

inline bool builtin_function(std::string_view name) {
    static const std::unordered_set<std::string_view> names{
        "print", "println", "eprint", "eprintln", "input", "str", "int", "float", "bool", "len"};
    return names.contains(name);
}

// Calls `f` on every direct child expression of `expr`. A method call visits the
// receiver (`callee` base) and the arguments; a plain call only its arguments.
inline void for_each_child_expr(const ast::Expr& expr,
                                const std::function<void(const ast::Expr&)>& f) {
    switch (expr.kind) {
        case ast::ExprKind::Unary:
            f(*static_cast<const ast::UnaryExpr&>(expr).operand);
            break;
        case ast::ExprKind::Binary: {
            const auto& binary = static_cast<const ast::BinaryExpr&>(expr);
            f(*binary.lhs);
            f(*binary.rhs);
            break;
        }
        case ast::ExprKind::Borrow:
            f(*static_cast<const ast::BorrowExpr&>(expr).target);
            break;
        case ast::ExprKind::Call: {
            const auto& call = static_cast<const ast::CallExpr&>(expr);
            if (call.callee->kind == ast::ExprKind::Member)
                f(*static_cast<const ast::MemberExpr&>(*call.callee).base);
            for (const auto& arg : call.args)
                f(*arg);
            break;
        }
        case ast::ExprKind::Member:
            f(*static_cast<const ast::MemberExpr&>(expr).base);
            break;
        case ast::ExprKind::Index: {
            const auto& index = static_cast<const ast::IndexExpr&>(expr);
            f(*index.base);
            f(*index.index);
            break;
        }
        case ast::ExprKind::Await:
            f(*static_cast<const ast::AwaitExpr&>(expr).value);
            break;
        case ast::ExprKind::Cast:
            f(*static_cast<const ast::CastExpr&>(expr).value);
            break;
        case ast::ExprKind::StructLiteral:
            for (const auto& field : static_cast<const ast::StructLiteralExpr&>(expr).fields)
                f(*field.value);
            break;
        case ast::ExprKind::Asm:
            for (const auto& operand : static_cast<const ast::AsmExpr&>(expr).operands)
                if (operand.value)
                    f(*operand.value);
            break;
        default:
            break;
    }
}

// Calls `f` on every statement-level expression reachable from `stmt`,
// descending into nested blocks.
inline void walk_stmt_exprs(const ast::Stmt& stmt, const std::function<void(const ast::Expr&)>& f) {
    const auto block = [&](const ast::BlockStmt* body) {
        if (body)
            for (const auto& child : body->stmts)
                walk_stmt_exprs(*child, f);
    };
    if (const auto* let_stmt = dynamic_cast<const ast::LetStmt*>(&stmt)) {
        if (let_stmt->init)
            f(*let_stmt->init);
    } else if (const auto* assign_stmt = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
        f(*assign_stmt->target);
        f(*assign_stmt->value);
    } else if (const auto* expr_stmt = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
        f(*expr_stmt->expr);
    } else if (const auto* return_stmt = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
        if (return_stmt->value)
            f(*return_stmt->value);
    } else if (const auto* if_stmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
        f(*if_stmt->condition);
        block(if_stmt->then_block.get());
        block(if_stmt->else_block.get());
    } else if (const auto* while_stmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
        f(*while_stmt->condition);
        block(while_stmt->body.get());
    } else if (const auto* for_stmt = dynamic_cast<const ast::ForStmt*>(&stmt)) {
        f(*for_stmt->begin);
        f(*for_stmt->end);
        block(for_stmt->body.get());
    } else if (const auto* loop_stmt = dynamic_cast<const ast::LoopStmt*>(&stmt)) {
        block(loop_stmt->body.get());
    } else if (const auto* block_stmt = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
        block(block_stmt);
    } else if (const auto* unsafe_stmt = dynamic_cast<const ast::UnsafeStmt*>(&stmt)) {
        block(unsafe_stmt->body.get());
    }
}
} // namespace uinx::sema_detail
