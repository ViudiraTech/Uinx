// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "sema_internal.hpp"
#include "uinx/sema.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace uinx {
using namespace sema_detail;

void TypeChecker::check_function(const ast::FunctionDecl& fn, const FunctionSig& sig) {
    if (!fn.body)
        return;
    FnContext c;
    c.sig = &sig;
    c.unsafe_context = fn.is_unsafe;
    c.scopes.emplace_back();
    c.mutable_scopes.emplace_back();
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        c.scopes.back()[fn.params[i].name] = sig.params[i];
        if (fn.params[i].mut)
            c.mutable_scopes.back().insert(fn.params[i].name);
    }
    check_block(*fn.body, c);
}
void TypeChecker::check_block(const ast::BlockStmt& b, FnContext& c) {
    c.scopes.emplace_back();
    c.mutable_scopes.emplace_back();
    for (const auto& s : b.stmts)
        check_stmt(*s, c);
    c.mutable_scopes.pop_back();
    c.scopes.pop_back();
}
void TypeChecker::check_stmt(const ast::Stmt& s, FnContext& c) {
    if (auto l = dynamic_cast<const ast::LetStmt*>(&s)) {
        Type init = l->init ? check_expr(*l->init, c) : Type{};
        Type ty = l->type
                      ? type_from_ast(*l->type,
                                      std::unordered_set<std::string>(c.sig->generic_names.begin(),
                                                                      c.sig->generic_names.end()))
                      : init;
        if (l->type && !can_coerce_expr(l->init.get(), init, ty))
            diags_.error(l->range,
                         "E0301",
                         "initializer has type '" + init.str() + "' but binding expects '" +
                             ty.str() + "'");
        model_.binding_types[l] = ty;
        c.scopes.back()[l->name] = ty;
        if (l->mut)
            c.mutable_scopes.back().insert(l->name);
        return;
    }
    if (auto a = dynamic_cast<const ast::AssignStmt*>(&s)) {
        if (auto binding = model_.hir.assignment_bindings.find(a);
            binding != model_.hir.assignment_bindings.end()) {
            Type rhs = check_expr(*a->value, c);
            model_.assignment_binding_types[a] = rhs;
            model_.expr_types[a->target.get()] = rhs;
            const auto& name = static_cast<const ast::NameExpr&>(*a->target).name;
            c.scopes.back()[name] = rhs;
            c.mutable_scopes.back().insert(name);
            return;
        }
        Type lhs = check_expr(*a->target, c, false), rhs = check_expr(*a->value, c);
        if (!is_mutable_place(*a->target, c))
            diags_.error(a->target->range, "E0333", "assignment requires a mutable place");
        if (!can_coerce_expr(a->value.get(), rhs, lhs))
            diags_.error(
                a->range, "E0334", "cannot assign '" + rhs.str() + "' to '" + lhs.str() + "'");
        if (a->op != "=") {
            const bool bitwise = a->op == "&=" || a->op == "|=" || a->op == "^=" ||
                                 a->op == "<<=" || a->op == ">>=" || a->op == "%=";
            if (bitwise ? (!lhs.is_integer() || !rhs.is_integer())
                        : (!lhs.is_numeric() || !rhs.is_numeric()))
                diags_.error(a->range,
                             "E0335",
                             "compound assignment operator '" + a->op +
                                 "' is not valid for these operands");
        }
        return;
    }
    if (auto e = dynamic_cast<const ast::ExprStmt*>(&s)) {
        check_expr(*e->expr, c);
        return;
    }
    if (auto r = dynamic_cast<const ast::ReturnStmt*>(&s)) {
        Type got = r->value ? check_expr(*r->value, c) : Type::builtin("unit");
        if (!can_coerce_expr(r->value.get(), got, c.sig->result))
            diags_.error(r->range,
                         "E0302",
                         "return type '" + got.str() + "' does not match '" + c.sig->result.str() +
                             "'");
        return;
    }
    if (auto b = dynamic_cast<const ast::BlockStmt*>(&s)) {
        check_block(*b, c);
        return;
    }
    if (auto i = dynamic_cast<const ast::IfStmt*>(&s)) {
        Type t = check_expr(*i->condition, c);
        if (t.kind != TypeKind::Bool)
            diags_.error(
                i->condition->range, "E0303", "if condition must be bool, found '" + t.str() + "'");
        check_block(*i->then_block, c);
        if (i->else_block)
            check_block(*i->else_block, c);
        return;
    }
    if (auto w = dynamic_cast<const ast::WhileStmt*>(&s)) {
        Type t = check_expr(*w->condition, c);
        if (t.kind != TypeKind::Bool)
            diags_.error(w->condition->range, "E0304", "while condition must be bool");
        ++c.loop_depth;
        check_block(*w->body, c);
        --c.loop_depth;
        return;
    }
    if (auto f = dynamic_cast<const ast::ForStmt*>(&s)) {
        Type begin = check_expr(*f->begin, c), end = check_expr(*f->end, c);
        if (!begin.is_integer() || !end.is_integer() || begin != end)
            diags_.error(f->range, "E0356", "for ranges require matching integer endpoint types");
        c.scopes.emplace_back();
        c.mutable_scopes.emplace_back();
        c.scopes.back()[f->name] = begin;
        ++c.loop_depth;
        check_block(*f->body, c);
        --c.loop_depth;
        c.mutable_scopes.pop_back();
        c.scopes.pop_back();
        return;
    }
    if (auto l = dynamic_cast<const ast::LoopStmt*>(&s)) {
        ++c.loop_depth;
        check_block(*l->body, c);
        --c.loop_depth;
        return;
    }
    if (dynamic_cast<const ast::BreakStmt*>(&s) || dynamic_cast<const ast::ContinueStmt*>(&s)) {
        if (c.loop_depth == 0)
            diags_.error(s.range, "E0357", "break/continue is only valid inside a loop");
        return;
    }
    if (auto u = dynamic_cast<const ast::UnsafeStmt*>(&s)) {
        bool old = c.unsafe_context;
        c.unsafe_context = true;
        check_block(*u->body, c);
        c.unsafe_context = old;
        return;
    }
}
Type TypeChecker::lookup_local(std::string_view n, const FnContext& c, const SourceRange& r) {
    for (auto it = c.scopes.rbegin(); it != c.scopes.rend(); ++it) {
        auto f = it->find(std::string(n));
        if (f != it->end())
            return f->second;
    }
    auto g = model_.globals.find(std::string(n));
    if (g != model_.globals.end())
        return g->second.type;
    diags_.error(r, "E0305", "name '" + std::string(n) + "' is not a value in this scope");
    return {};
}
bool TypeChecker::is_mutable_binding(std::string_view n, const FnContext& c) const {
    for (auto it = c.mutable_scopes.rbegin(); it != c.mutable_scopes.rend(); ++it)
        if (it->contains(std::string(n)))
            return true;
    auto g = model_.globals.find(std::string(n));
    return g != model_.globals.end() && g->second.is_mut && !g->second.is_const;
}
bool TypeChecker::is_mutable_place(const ast::Expr& e, const FnContext& c) {
    if (e.kind == ast::ExprKind::Name)
        return is_mutable_binding(static_cast<const ast::NameExpr&>(e).name, c);
    if (e.kind == ast::ExprKind::Member) {
        const auto& m = static_cast<const ast::MemberExpr&>(e);
        Type base{};
        if (auto it = model_.expr_types.find(m.base.get()); it != model_.expr_types.end())
            base = it->second;
        if (base.kind == TypeKind::Ref)
            return base.mut;
        return is_mutable_place(*m.base, c);
    }
    if (e.kind == ast::ExprKind::Index) {
        const auto& i = static_cast<const ast::IndexExpr&>(e);
        Type base{};
        if (auto it = model_.expr_types.find(i.base.get()); it != model_.expr_types.end())
            base = it->second;
        if (base.kind == TypeKind::Ref)
            return base.mut;
        if (base.kind == TypeKind::Named && base.name == "SliceMut")
            return is_mutable_place(*i.base, c);
        return false;
    }
    if (e.kind == ast::ExprKind::Unary) {
        const auto& u = static_cast<const ast::UnaryExpr&>(e);
        if (u.op == "*") {
            Type p{};
            if (auto it = model_.expr_types.find(u.operand.get()); it != model_.expr_types.end())
                p = it->second;
            return p.kind == TypeKind::Ref ? p.mut : (p.kind == TypeKind::RawPtr ? p.mut : false);
        }
    }
    return false;
}
bool TypeChecker::unify_generic(const Type& f,
                                const Type& a,
                                std::unordered_map<std::string, Type>& s) {
    if (f.kind == TypeKind::Generic) {
        auto it = s.find(f.name);
        if (it == s.end()) {
            s[f.name] = a;
            return true;
        }
        return it->second == a;
    }
    if (f.kind != a.kind)
        return false;
    if (f.pointee && a.pointee)
        return f.mut == a.mut && unify_generic(*f.pointee, *a.pointee, s);
    if (f.args.size() != a.args.size())
        return false;
    for (std::size_t i = 0; i < f.args.size(); ++i)
        if (!unify_generic(f.args[i], a.args[i], s))
            return false;
    return f.name == a.name || f.name.empty() || a.name.empty();
}
} // namespace uinx
