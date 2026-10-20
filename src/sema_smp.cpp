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

void TypeChecker::validate_concurrency_boundaries() {
    TraitSolver solver(model_);
    for (const auto& [key, signature] : model_.functions) {
        (void)key;
        if (!signature.is_concurrent || !signature.decl)
            continue;
        // `extern` declarations are FFI trust boundaries without bodies: the
        // `unsafe` block at the Uinx call site is the audit point, and raw
        // pointers are the ABI mechanism itself (e.g. `uinx_atomic_*`
        // intrinsics lowered directly to LLVM atomics). Enforcing `Send` on
        // their parameters would make compare-exchange/fetch-add unusable
        // from exactly the concurrent paths they exist for, while checking
        // nothing about an actual transfer. Uinx-level callers stay checked.
        if (signature.is_extern)
            continue;
        for (std::size_t i = 0; i < signature.params.size(); ++i) {
            const Type& parameter = signature.params[i];
            if (solver.satisfies(parameter, "Send", signature.bounds))
                continue;

            const SourceRange range = i < signature.decl->params.size()
                                          ? signature.decl->params[i].range
                                          : signature.decl->range;
            diags_.error(range,
                         "E0362",
                         "concurrent function parameter " + std::to_string(i + 1) + " of type '" +
                             parameter.str() + "' is not Send; use an owning Send type, a shared " +
                             "reference whose pointee is Sync, or add the required generic bound");
        }
    }
}

void TypeChecker::propagate_concurrency(ast::SmpMode mode) {
    std::unordered_map<std::string, std::unordered_set<std::string>> edges;

    std::function<void(const ast::Expr&, std::unordered_set<std::string>&)> collect_callees;
    collect_callees = [&](const ast::Expr& expr, std::unordered_set<std::string>& out) {
        if (expr.kind == ast::ExprKind::Call) {
            const auto& call = static_cast<const ast::CallExpr&>(expr);
            if (call.callee->kind == ast::ExprKind::Name) {
                const auto& name = static_cast<const ast::NameExpr&>(*call.callee).name;
                if (model_.functions.contains(name))
                    out.insert(name);
            } else if (call.callee->kind == ast::ExprKind::Member) {
                const auto& member = static_cast<const ast::MemberExpr&>(*call.callee);
                auto type_it = model_.expr_types.find(member.base.get());
                if (type_it != model_.expr_types.end()) {
                    Type owner = type_it->second;
                    if (owner.kind == TypeKind::Ref && owner.pointee)
                        owner = *owner.pointee;
                    const std::string key = owner.name + "::" + member.member;
                    if (model_.functions.contains(key))
                        out.insert(key);
                }
            }
        }
        for_each_child_expr(expr, [&](const ast::Expr& child) { collect_callees(child, out); });
    };

    for (const auto& [key, sig] : model_.functions) {
        if (!sig.decl || !sig.decl->body)
            continue;
        auto& callees = edges[key];
        for (const auto& stmt : sig.decl->body->stmts)
            walk_stmt_exprs(*stmt, [&](const ast::Expr& expr) { collect_callees(expr, callees); });
    }

    std::vector<std::string> work;
    for (const auto& [key, sig] : model_.functions)
        if (sig.is_concurrent)
            work.push_back(key);
    std::unordered_set<std::string> seen;
    while (!work.empty()) {
        std::string key = std::move(work.back());
        work.pop_back();
        if (!seen.insert(key).second)
            continue;
        auto fit = model_.functions.find(key);
        if (fit != model_.functions.end())
            fit->second.is_concurrent = true;
        auto edge = edges.find(key);
        if (edge == edges.end())
            continue;
        for (const auto& callee : edge->second)
            work.push_back(callee);
    }

    // All SMP modes are explicit-shared now: `auto`, `manual` and `strict`
    // differ only in ordering of explicitly atomic accesses, never in
    // promotion. See docs/MEMORY_MODEL.md.
    (void)mode;
    // Industrial-grade explicit-shared model (Rust + C++20 + LKMM fusion):
    // `smp auto/manual/strict` no longer implicitly promotes mutable globals.
    // Only explicitly `shared` globals/fields lower to LLVM atomics. Any
    // concurrent access to a mutable non-shared, non-percpu global is E0363.
    // Ordering: auto/manual => acquire/release/acq_rel for explicit atomics,
    // strict => seq_cst. See docs/MEMORY_MODEL.md.
    // Note: `shared bool` is rejected at definition (use AtomicBool over u8)
    // because LLVM atomics must be byte-sized (i1 is not valid).
    const auto atomic_compatible = [](const Type& type) {
        return type.is_integer() || type.kind == TypeKind::RawPtr;
    };

    // A local or parameter that shadows a global is not a global access.
    const auto is_local = [&](const ast::Expr& name_expr) {
        const auto resolved = model_.hir.expr_resolution.find(&name_expr);
        if (resolved == model_.hir.expr_resolution.end() ||
            resolved->second >= model_.hir.symbols.size())
            return false;
        const auto kind = model_.hir.symbols[resolved->second].kind;
        return kind == hir::SymbolKind::Local || kind == hir::SymbolKind::Parameter;
    };

    std::unordered_set<std::string> reported;
    std::function<void(const ast::Expr&)> check_expr;
    check_expr = [&](const ast::Expr& expr) {
        if (expr.kind == ast::ExprKind::Name) {
            if (is_local(expr))
                return;
            const auto& name = static_cast<const ast::NameExpr&>(expr).name;
            auto global = model_.globals.find(name);
            if (global != model_.globals.end() && global->second.is_mut &&
                !global->second.is_percpu && !global->second.is_shared) {
                const std::string key = std::to_string(expr.range.begin.line) + ":" +
                                        std::to_string(expr.range.begin.column) + ":" + name;
                if (reported.insert(key).second) {
                    if (atomic_compatible(global->second.type)) {
                        diags_.error(expr.range,
                                     "E0363",
                                     "concurrent access to mutable global '" + name +
                                         "' requires explicit `shared`, a lock, or `percpu` "
                                         "storage; smp auto no longer promotes implicitly "
                                         "(Rust+C++20+LKMM explicit-shared model)");
                    } else {
                        diags_.error(expr.range,
                                     "E0363",
                                     "concurrent access to aggregate global '" + name +
                                         "' cannot be strengthened as one atomic object; mark "
                                         "atomic fields shared, use a lock, or use percpu "
                                         "storage");
                    }
                }
            }
            return;
        }
        if (expr.kind == ast::ExprKind::Member) {
            const auto& member = static_cast<const ast::MemberExpr&>(expr);
            if (member.base->kind == ast::ExprKind::Name && !is_local(*member.base)) {
                const auto& base_name = static_cast<const ast::NameExpr&>(*member.base).name;
                auto global = model_.globals.find(base_name);
                if (global != model_.globals.end() && global->second.is_mut &&
                    !global->second.is_percpu && global->second.type.kind == TypeKind::Named) {
                    auto structure = model_.structs.find(global->second.type.name);
                    if (structure != model_.structs.end()) {
                        auto field = structure->second.fields.find(member.member);
                        if (field != structure->second.fields.end()) {
                            if (structure->second.shared_fields.contains(member.member))
                                return;
                            const std::string key = std::to_string(expr.range.begin.line) + ":" +
                                                    std::to_string(expr.range.begin.column) + ":" +
                                                    base_name + "." + member.member;
                            if (reported.insert(key).second) {
                                diags_.error(
                                    expr.range,
                                    "E0363",
                                    "concurrent access to field '" + base_name + "." +
                                        member.member +
                                        "' requires explicit `shared` field, a lock, or "
                                        "`percpu` storage; smp auto no longer promotes implicitly");
                            }
                            return;
                        }
                    }
                    // Aggregate global itself accessed concurrently without per-field
                    // sharing: reject.
                    const std::string key = std::to_string(expr.range.begin.line) + ":" +
                                            std::to_string(expr.range.begin.column) + ":" +
                                            base_name;
                    if (reported.insert(key).second) {
                        diags_.error(expr.range,
                                     "E0363",
                                     "concurrent access to aggregate global '" + base_name +
                                         "' cannot be strengthened as one atomic object; mark "
                                         "atomic fields shared, use a lock, or use percpu "
                                         "storage");
                    }
                    return;
                }
            }
            check_expr(*member.base);
            return;
        }
        for_each_child_expr(expr, check_expr);
    };

    for (const auto& [key, signature] : model_.functions) {
        if (!signature.is_concurrent || !signature.decl || !signature.decl->body)
            continue;
        for (const auto& statement : signature.decl->body->stmts)
            walk_stmt_exprs(*statement, check_expr);
    }
}
} // namespace uinx
