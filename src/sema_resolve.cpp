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

hir::SymbolId NameResolver::add(hir::Module& out,
                                hir::SymbolKind kind,
                                std::string name,
                                SourceRange range,
                                std::optional<hir::SymbolId> parent) {
    hir::SymbolId id = static_cast<hir::SymbolId>(out.symbols.size());
    out.symbols.push_back({id, kind, std::move(name), std::move(range), parent});
    return id;
}
hir::Module NameResolver::resolve(const ast::Module& module) {
    hir::Module out;
    out.ast = &module;
    for (const auto& item : module.items) {
        std::visit(
            [&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, ast::FunctionDecl> ||
                              std::is_same_v<T, ast::StructDecl> ||
                              std::is_same_v<T, ast::TraitDecl> ||
                              std::is_same_v<T, ast::GlobalDecl>) {
                    hir::SymbolKind k = hir::SymbolKind::Function;
                    if constexpr (std::is_same_v<T, ast::StructDecl>)
                        k = hir::SymbolKind::Struct;
                    else if constexpr (std::is_same_v<T, ast::TraitDecl>)
                        k = hir::SymbolKind::Trait;
                    else if constexpr (std::is_same_v<T, ast::GlobalDecl>)
                        k = hir::SymbolKind::Global;
                    if (out.globals.contains(x.name)) {
                        if constexpr (std::is_same_v<T, ast::FunctionDecl>) {
                            if (!x.is_extern)
                                diags_.error(
                                    x.range, "E0200", "duplicate global symbol '" + x.name + "'");
                        } else
                            diags_.error(
                                x.range, "E0200", "duplicate global symbol '" + x.name + "'");
                    } else
                        out.globals[x.name] = add(out, k, x.name, x.range);
                }
            },
            item);
    }
    for (const auto& item : module.items)
        if (auto f = std::get_if<ast::FunctionDecl>(&item))
            resolve_function(out, *f);
        else if (auto im = std::get_if<ast::ImplDecl>(&item))
            for (const auto& m : im->methods)
                resolve_function(out, m);
    return out;
}
void NameResolver::resolve_function(hir::Module& out, const ast::FunctionDecl& fn) {
    std::optional<hir::SymbolId> owner;
    auto gi = out.globals.find(fn.name);
    if (gi != out.globals.end())
        owner = gi->second;
    std::vector<std::unordered_map<std::string, hir::SymbolId>> scopes(1);
    for (const auto& g : fn.generics) {
        auto id = add(out, hir::SymbolKind::Generic, g.name, g.range, owner);
        if (scopes.back().contains(g.name))
            diags_.error(g.range, "E0201", "duplicate generic parameter '" + g.name + "'");
        else
            scopes.back()[g.name] = id;
    }
    for (const auto& p : fn.params) {
        auto id = add(out, hir::SymbolKind::Parameter, p.name, p.range, owner);
        if (scopes.back().contains(p.name))
            diags_.error(p.range, "E0202", "duplicate parameter '" + p.name + "'");
        else
            scopes.back()[p.name] = id;
        out.params[&p] = id;
    }
    if (fn.body)
        resolve_block(out, *fn.body, scopes, owner);
}
void NameResolver::resolve_block(
    hir::Module& out,
    const ast::BlockStmt& block,
    std::vector<std::unordered_map<std::string, hir::SymbolId>>& scopes,
    std::optional<hir::SymbolId> owner) {
    scopes.emplace_back();
    for (const auto& sp : block.stmts) {
        const auto& s = *sp;
        if (auto l = dynamic_cast<const ast::LetStmt*>(&s)) {
            if (l->init)
                resolve_expr(out, *l->init, scopes);
            auto id = add(out, hir::SymbolKind::Local, l->name, l->range, owner);
            if (scopes.back().contains(l->name))
                diags_.error(
                    l->range, "E0203", "binding '" + l->name + "' already exists in this scope");
            else
                scopes.back()[l->name] = id;
            out.bindings[l] = id;
        } else if (auto a = dynamic_cast<const ast::AssignStmt*>(&s)) {
            if (a->op == "=" && a->target->kind == ast::ExprKind::Name) {
                const auto& name = static_cast<const ast::NameExpr&>(*a->target).name;
                bool resolved = out.globals.contains(name);
                for (auto it = scopes.rbegin(); !resolved && it != scopes.rend(); ++it)
                    resolved = it->contains(name);
                if (!resolved) {
                    resolve_expr(out, *a->value, scopes);
                    auto id = add(out, hir::SymbolKind::Local, name, a->target->range, owner);
                    scopes.back()[name] = id;
                    out.assignment_bindings[a] = id;
                    out.expr_resolution[a->target.get()] = id;
                    continue;
                }
            }
            resolve_expr(out, *a->target, scopes);
            resolve_expr(out, *a->value, scopes);
        } else if (auto e = dynamic_cast<const ast::ExprStmt*>(&s)) {
            resolve_expr(out, *e->expr, scopes);
        } else if (auto r = dynamic_cast<const ast::ReturnStmt*>(&s)) {
            if (r->value)
                resolve_expr(out, *r->value, scopes);
        } else if (auto b = dynamic_cast<const ast::BlockStmt*>(&s)) {
            resolve_block(out, *b, scopes, owner);
        } else if (auto i = dynamic_cast<const ast::IfStmt*>(&s)) {
            resolve_expr(out, *i->condition, scopes);
            resolve_block(out, *i->then_block, scopes, owner);
            if (i->else_block)
                resolve_block(out, *i->else_block, scopes, owner);
        } else if (auto w = dynamic_cast<const ast::WhileStmt*>(&s)) {
            resolve_expr(out, *w->condition, scopes);
            resolve_block(out, *w->body, scopes, owner);
        } else if (auto f = dynamic_cast<const ast::ForStmt*>(&s)) {
            resolve_expr(out, *f->begin, scopes);
            resolve_expr(out, *f->end, scopes);
            scopes.emplace_back();
            auto id = add(out, hir::SymbolKind::Local, f->name, f->range, owner);
            scopes.back()[f->name] = id;
            out.for_bindings[f] = id;
            resolve_block(out, *f->body, scopes, owner);
            scopes.pop_back();
        } else if (auto loop = dynamic_cast<const ast::LoopStmt*>(&s)) {
            resolve_block(out, *loop->body, scopes, owner);
        } else if (auto u = dynamic_cast<const ast::UnsafeStmt*>(&s)) {
            resolve_block(out, *u->body, scopes, owner);
        }
    }
    scopes.pop_back();
}
void NameResolver::resolve_expr(
    hir::Module& out,
    const ast::Expr& e,
    std::vector<std::unordered_map<std::string, hir::SymbolId>>& scopes) {
    switch (e.kind) {
        case ast::ExprKind::Name: {
            auto& n = static_cast<const ast::NameExpr&>(e);
            for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                auto f = it->find(n.name);
                if (f != it->end()) {
                    out.expr_resolution[&e] = f->second;
                    return;
                }
            }
            auto g = out.globals.find(n.name);
            if (g != out.globals.end()) {
                out.expr_resolution[&e] = g->second;
                return;
            }
            if (builtin_function(n.name))
                return;
            diags_.error(e.range, "E0204", "unresolved name '" + n.name + "'");
            break;
        }
        case ast::ExprKind::StructLiteral: {
            auto& st = static_cast<const ast::StructLiteralExpr&>(e);
            auto g = out.globals.find(st.name);
            if (g == out.globals.end())
                diags_.error(e.range, "E0206", "unknown struct '" + st.name + "'");
            else
                out.expr_resolution[&e] = g->second;
            for (auto& f : st.fields)
                resolve_expr(out, *f.value, scopes);
            break;
        }
        case ast::ExprKind::Unary:
            resolve_expr(out, *static_cast<const ast::UnaryExpr&>(e).operand, scopes);
            break;
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(e);
            resolve_expr(out, *b.lhs, scopes);
            resolve_expr(out, *b.rhs, scopes);
            break;
        }
        case ast::ExprKind::Borrow:
            resolve_expr(out, *static_cast<const ast::BorrowExpr&>(e).target, scopes);
            break;
        case ast::ExprKind::Call: {
            auto& c = static_cast<const ast::CallExpr&>(e);
            resolve_expr(out, *c.callee, scopes);
            for (auto& a : c.args)
                resolve_expr(out, *a, scopes);
            break;
        }
        case ast::ExprKind::Member:
            resolve_expr(out, *static_cast<const ast::MemberExpr&>(e).base, scopes);
            break;
        case ast::ExprKind::Index: {
            auto& i = static_cast<const ast::IndexExpr&>(e);
            resolve_expr(out, *i.base, scopes);
            resolve_expr(out, *i.index, scopes);
            break;
        }
        case ast::ExprKind::Await:
            resolve_expr(out, *static_cast<const ast::AwaitExpr&>(e).value, scopes);
            break;
        case ast::ExprKind::Cast:
            resolve_expr(out, *static_cast<const ast::CastExpr&>(e).value, scopes);
            break;
        case ast::ExprKind::Asm: {
            auto& a = static_cast<const ast::AsmExpr&>(e);
            for (auto& o : a.operands)
                if (o.value)
                    resolve_expr(out, *o.value, scopes);
            for (auto& o : a.operands)
                if (!o.out_name.empty()) {
                    bool found = false;
                    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
                        if (auto f = it->find(o.out_name); f != it->end()) {
                            found = true;
                            break;
                        }
                    if (!found)
                        diags_.error(
                            o.range, "E0205", "unknown asm output binding '" + o.out_name + "'");
                }
            break;
        }
        default:
            break;
    }
}

bool TraitSolver::satisfies(const Type& type, std::string_view trait) const {
    return satisfies(type, trait, {});
}

bool TraitSolver::satisfies(
    const Type& type,
    std::string_view trait,
    const std::unordered_map<std::string, std::vector<std::string>>& generic_bounds) const {
    if (trait == "Copy")
        return type.is_copy();
    if (trait != "Send" && trait != "Sync")
        return false;

    std::function<bool(const Type&, const Type&, std::unordered_map<std::string, Type>&)>
        unify_pattern;
    unify_pattern = [&](const Type& pattern,
                        const Type& actual,
                        std::unordered_map<std::string, Type>& substitutions) -> bool {
        if (pattern.kind == TypeKind::Generic) {
            auto [it, inserted] = substitutions.emplace(pattern.name, actual);
            return inserted || it->second == actual;
        }
        if (pattern.kind != actual.kind || pattern.name != actual.name ||
            pattern.mut != actual.mut || pattern.args.size() != actual.args.size())
            return false;
        if (static_cast<bool>(pattern.pointee) != static_cast<bool>(actual.pointee))
            return false;
        if (pattern.pointee && !unify_pattern(*pattern.pointee, *actual.pointee, substitutions))
            return false;
        for (std::size_t i = 0; i < pattern.args.size(); ++i)
            if (!unify_pattern(pattern.args[i], actual.args[i], substitutions))
                return false;
        return true;
    };

    std::function<bool(const Type&, std::string_view, std::size_t)> solve;
    solve = [&](const Type& current, std::string_view current_trait, std::size_t depth) -> bool {
        if (depth > 64)
            return false;

        for (const auto& impl : model_.impls)
            if (impl.trait == current_trait && impl.for_type == current)
                return true;

        for (const auto& marker : model_.markers) {
            if (marker.trait != current_trait)
                continue;
            std::unordered_map<std::string, Type> substitutions;
            if (!unify_pattern(marker.type, current, substitutions))
                continue;
            bool valid = true;
            for (const auto& [generic, bounds] : marker.bounds) {
                auto actual = substitutions.find(generic);
                if (actual == substitutions.end()) {
                    valid = false;
                    break;
                }
                for (const auto& bound : bounds) {
                    if (!solve(actual->second, bound, depth + 1)) {
                        valid = false;
                        break;
                    }
                }
                if (!valid)
                    break;
            }
            if (valid)
                return true;
        }

        if (current_trait != "Send" && current_trait != "Sync")
            return false;

        std::unordered_set<std::string> visiting;
        std::function<bool(const Type&,
                           std::string_view,
                           std::unordered_set<std::string>&,
                           std::unordered_set<std::string>)>
            auto_trait;
        std::unordered_set<std::string> assumed;
        for (const auto& [generic, bounds] : generic_bounds)
            for (const auto& bound : bounds)
                assumed.insert(bound + ":" + generic);
        auto_trait = [&](const Type& value,
                         std::string_view value_trait,
                         std::unordered_set<std::string>& active,
                         std::unordered_set<std::string> assumptions) -> bool {
            const std::string key = std::string(value_trait) + ":" + value.str();
            if (!active.insert(key).second)
                return true;
            const auto finish = [&](bool result) {
                active.erase(key);
                return result;
            };

            if (value.kind == TypeKind::RawPtr)
                return finish(false);
            if (value.kind == TypeKind::Ref && value.pointee) {
                if (value_trait == "Send" && value.mut)
                    return finish(
                        auto_trait(*value.pointee, "Send", active, std::move(assumptions)));
                return finish(auto_trait(*value.pointee, "Sync", active, std::move(assumptions)));
            }
            if (value.kind == TypeKind::Generic)
                return finish(assumptions.contains(std::string(value_trait) + ":" + value.name));
            if (value.kind == TypeKind::Str)
                return finish(true);
            if (value.kind != TypeKind::Named)
                return finish(value.is_copy());

            auto structure = model_.structs.find(value.name);
            if (structure == model_.structs.end())
                return finish(false);
            for (const auto& [generic, bounds] : structure->second.bounds)
                if (std::find(bounds.begin(), bounds.end(), value_trait) != bounds.end())
                    assumptions.insert(std::string(value_trait) + ":" + generic);

            std::unordered_map<std::string, Type> substitutions;
            for (std::size_t i = 0;
                 i < structure->second.generic_names.size() && i < value.args.size();
                 ++i)
                substitutions[structure->second.generic_names[i]] = value.args[i];
            for (const auto& [field_name, field_type] : structure->second.fields) {
                (void)field_name;
                if (!auto_trait(substitute_type(field_type, substitutions),
                                value_trait,
                                active,
                                assumptions))
                    return finish(false);
            }
            return finish(true);
        };
        return auto_trait(type, trait, visiting, assumed);
    };
    return solve(type, trait, 0);
}
} // namespace uinx
