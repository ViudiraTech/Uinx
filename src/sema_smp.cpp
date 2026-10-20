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

    std::function<void(const ast::Expr&, std::unordered_set<std::string>&)> visit_expr;
    std::function<void(const ast::Stmt&, std::unordered_set<std::string>&)> visit_stmt;
    visit_expr = [&](const ast::Expr& expr, std::unordered_set<std::string>& out) {
        switch (expr.kind) {
            case ast::ExprKind::Unary:
                visit_expr(*static_cast<const ast::UnaryExpr&>(expr).operand, out);
                break;
            case ast::ExprKind::Binary: {
                const auto& b = static_cast<const ast::BinaryExpr&>(expr);
                visit_expr(*b.lhs, out);
                visit_expr(*b.rhs, out);
                break;
            }
            case ast::ExprKind::Borrow:
                visit_expr(*static_cast<const ast::BorrowExpr&>(expr).target, out);
                break;
            case ast::ExprKind::Call: {
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
                    visit_expr(*member.base, out);
                }
                for (const auto& arg : call.args)
                    visit_expr(*arg, out);
                break;
            }
            case ast::ExprKind::Member:
                visit_expr(*static_cast<const ast::MemberExpr&>(expr).base, out);
                break;
            case ast::ExprKind::Index: {
                const auto& i = static_cast<const ast::IndexExpr&>(expr);
                visit_expr(*i.base, out);
                visit_expr(*i.index, out);
                break;
            }
            case ast::ExprKind::Await:
                visit_expr(*static_cast<const ast::AwaitExpr&>(expr).value, out);
                break;
            case ast::ExprKind::Cast:
                visit_expr(*static_cast<const ast::CastExpr&>(expr).value, out);
                break;
            case ast::ExprKind::StructLiteral:
                for (const auto& field : static_cast<const ast::StructLiteralExpr&>(expr).fields)
                    visit_expr(*field.value, out);
                break;
            case ast::ExprKind::Asm:
                for (const auto& operand : static_cast<const ast::AsmExpr&>(expr).operands)
                    if (operand.value)
                        visit_expr(*operand.value, out);
                break;
            default:
                break;
        }
    };
    visit_stmt = [&](const ast::Stmt& stmt, std::unordered_set<std::string>& out) {
        if (const auto* let_stmt = dynamic_cast<const ast::LetStmt*>(&stmt)) {
            if (let_stmt->init)
                visit_expr(*let_stmt->init, out);
        } else if (const auto* assign_stmt = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
            visit_expr(*assign_stmt->target, out);
            visit_expr(*assign_stmt->value, out);
        } else if (const auto* expr_stmt = dynamic_cast<const ast::ExprStmt*>(&stmt))
            visit_expr(*expr_stmt->expr, out);
        else if (const auto* return_stmt = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
            if (return_stmt->value)
                visit_expr(*return_stmt->value, out);
        } else if (const auto* if_stmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
            visit_expr(*if_stmt->condition, out);
            for (const auto& child : if_stmt->then_block->stmts)
                visit_stmt(*child, out);
            if (if_stmt->else_block)
                for (const auto& child : if_stmt->else_block->stmts)
                    visit_stmt(*child, out);
        } else if (const auto* while_stmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
            visit_expr(*while_stmt->condition, out);
            for (const auto& child : while_stmt->body->stmts)
                visit_stmt(*child, out);
        } else if (const auto* for_stmt = dynamic_cast<const ast::ForStmt*>(&stmt)) {
            visit_expr(*for_stmt->begin, out);
            visit_expr(*for_stmt->end, out);
            for (const auto& child : for_stmt->body->stmts)
                visit_stmt(*child, out);
        } else if (const auto* loop_stmt = dynamic_cast<const ast::LoopStmt*>(&stmt)) {
            for (const auto& child : loop_stmt->body->stmts)
                visit_stmt(*child, out);
        } else if (const auto* block_stmt = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
            for (const auto& child : block_stmt->stmts)
                visit_stmt(*child, out);
        } else if (const auto* unsafe_stmt = dynamic_cast<const ast::UnsafeStmt*>(&stmt)) {
            for (const auto& child : unsafe_stmt->body->stmts)
                visit_stmt(*child, out);
        }
    };

    for (const auto& [key, sig] : model_.functions) {
        if (!sig.decl || !sig.decl->body)
            continue;
        auto& callees = edges[key];
        for (const auto& stmt : sig.decl->body->stmts)
            visit_stmt(*stmt, callees);
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

    std::unordered_set<std::string> reported;
    std::function<void(const ast::Expr&)> check_expr;
    std::function<void(const ast::Stmt&)> check_stmt;
    check_expr = [&](const ast::Expr& expr) {
        if (expr.kind == ast::ExprKind::Name) {
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
            if (member.base->kind == ast::ExprKind::Name) {
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
        switch (expr.kind) {
            case ast::ExprKind::Unary:
                check_expr(*static_cast<const ast::UnaryExpr&>(expr).operand);
                break;
            case ast::ExprKind::Binary: {
                const auto& binary = static_cast<const ast::BinaryExpr&>(expr);
                check_expr(*binary.lhs);
                check_expr(*binary.rhs);
                break;
            }
            case ast::ExprKind::Borrow:
                check_expr(*static_cast<const ast::BorrowExpr&>(expr).target);
                break;
            case ast::ExprKind::Call: {
                const auto& call = static_cast<const ast::CallExpr&>(expr);
                if (call.callee->kind == ast::ExprKind::Member)
                    check_expr(*static_cast<const ast::MemberExpr&>(*call.callee).base);
                for (const auto& arg : call.args)
                    check_expr(*arg);
                break;
            }
            case ast::ExprKind::Index: {
                const auto& index = static_cast<const ast::IndexExpr&>(expr);
                check_expr(*index.base);
                check_expr(*index.index);
                break;
            }
            case ast::ExprKind::Await:
                check_expr(*static_cast<const ast::AwaitExpr&>(expr).value);
                break;
            case ast::ExprKind::Cast:
                check_expr(*static_cast<const ast::CastExpr&>(expr).value);
                break;
            case ast::ExprKind::StructLiteral:
                for (const auto& field : static_cast<const ast::StructLiteralExpr&>(expr).fields)
                    check_expr(*field.value);
                break;
            case ast::ExprKind::Asm:
                for (const auto& operand : static_cast<const ast::AsmExpr&>(expr).operands)
                    if (operand.value)
                        check_expr(*operand.value);
                break;
            default:
                break;
        }
    };
    check_stmt = [&](const ast::Stmt& stmt) {
        if (const auto* let_stmt = dynamic_cast<const ast::LetStmt*>(&stmt)) {
            if (let_stmt->init)
                check_expr(*let_stmt->init);
        } else if (const auto* assign_stmt = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
            check_expr(*assign_stmt->target);
            check_expr(*assign_stmt->value);
        } else if (const auto* expr_stmt = dynamic_cast<const ast::ExprStmt*>(&stmt))
            check_expr(*expr_stmt->expr);
        else if (const auto* return_stmt = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
            if (return_stmt->value)
                check_expr(*return_stmt->value);
        } else if (const auto* if_stmt = dynamic_cast<const ast::IfStmt*>(&stmt)) {
            check_expr(*if_stmt->condition);
            for (const auto& child : if_stmt->then_block->stmts)
                check_stmt(*child);
            if (if_stmt->else_block)
                for (const auto& child : if_stmt->else_block->stmts)
                    check_stmt(*child);
        } else if (const auto* while_stmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
            check_expr(*while_stmt->condition);
            for (const auto& child : while_stmt->body->stmts)
                check_stmt(*child);
        } else if (const auto* for_stmt = dynamic_cast<const ast::ForStmt*>(&stmt)) {
            check_expr(*for_stmt->begin);
            check_expr(*for_stmt->end);
            for (const auto& child : for_stmt->body->stmts)
                check_stmt(*child);
        } else if (const auto* loop_stmt = dynamic_cast<const ast::LoopStmt*>(&stmt)) {
            for (const auto& child : loop_stmt->body->stmts)
                check_stmt(*child);
        } else if (const auto* block_stmt = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
            for (const auto& child : block_stmt->stmts)
                check_stmt(*child);
        } else if (const auto* unsafe_stmt = dynamic_cast<const ast::UnsafeStmt*>(&stmt)) {
            for (const auto& child : unsafe_stmt->body->stmts)
                check_stmt(*child);
        }
    };

    for (const auto& [key, signature] : model_.functions) {
        if (!signature.is_concurrent || !signature.decl || !signature.decl->body)
            continue;
        for (const auto& statement : signature.decl->body->stmts)
            check_stmt(*statement);
    }
}
} // namespace uinx
