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
} // namespace uinx::sema_detail
