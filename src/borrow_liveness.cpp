// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "borrow_internal.hpp"
#include "uinx/borrow.hpp"

#include <algorithm>
#include <sstream>

namespace uinx {
using namespace borrow_detail;

void BorrowChecker::collect_expr_uses(const ast::Expr& expr,
                                      std::size_t index,
                                      std::unordered_map<std::string, std::size_t>& out) const {
    switch (expr.kind) {
        case ast::ExprKind::Name:
            if (const auto place = place_of(expr))
                out[root_of(*place)] = index;
            break;
        case ast::ExprKind::StructLiteral:
            for (const auto& field : static_cast<const ast::StructLiteralExpr&>(expr).fields)
                collect_expr_uses(*field.value, index, out);
            break;
        case ast::ExprKind::Unary:
            collect_expr_uses(*static_cast<const ast::UnaryExpr&>(expr).operand, index, out);
            break;
        case ast::ExprKind::Binary: {
            const auto& binary = static_cast<const ast::BinaryExpr&>(expr);
            collect_expr_uses(*binary.lhs, index, out);
            collect_expr_uses(*binary.rhs, index, out);
            break;
        }
        case ast::ExprKind::Borrow:
            collect_expr_uses(*static_cast<const ast::BorrowExpr&>(expr).target, index, out);
            break;
        case ast::ExprKind::Call: {
            const auto& call = static_cast<const ast::CallExpr&>(expr);
            collect_expr_uses(*call.callee, index, out);
            for (const auto& arg : call.args)
                collect_expr_uses(*arg, index, out);
            break;
        }
        case ast::ExprKind::Member:
            collect_expr_uses(*static_cast<const ast::MemberExpr&>(expr).base, index, out);
            break;
        case ast::ExprKind::Index: {
            const auto& indexed = static_cast<const ast::IndexExpr&>(expr);
            collect_expr_uses(*indexed.base, index, out);
            collect_expr_uses(*indexed.index, index, out);
            break;
        }
        case ast::ExprKind::Await:
            collect_expr_uses(*static_cast<const ast::AwaitExpr&>(expr).value, index, out);
            break;
        case ast::ExprKind::Cast:
            collect_expr_uses(*static_cast<const ast::CastExpr&>(expr).value, index, out);
            break;
        case ast::ExprKind::Asm:
            for (const auto& operand : static_cast<const ast::AsmExpr&>(expr).operands) {
                if (operand.value)
                    collect_expr_uses(*operand.value, index, out);
                if (!operand.out_name.empty())
                    out[operand.out_name] = index;
            }
            break;
        default:
            break;
    }
}

BorrowChecker::LiveSet BorrowChecker::expression_uses(const ast::Expr& expr) const {
    std::unordered_map<std::string, std::size_t> uses;
    collect_expr_uses(expr, 0, uses);
    LiveSet result;
    for (const auto& [name, index] : uses) {
        (void)index;
        result.insert(name);
    }
    return result;
}

void BorrowChecker::collect_place_uses(const ast::Expr& expr, LivePlaceSet& out) const {
    if (auto place = place_of(expr)) {
        out.insert(*place);
        // Parent places are also considered used (shallow prefixes).
        std::string cur = *place;
        while (true) {
            const auto dot = cur.rfind('.');
            if (dot == std::string::npos)
                break;
            cur = cur.substr(0, dot);
            out.insert(cur);
        }
    }
    switch (expr.kind) {
        case ast::ExprKind::StructLiteral:
            for (const auto& field : static_cast<const ast::StructLiteralExpr&>(expr).fields)
                collect_place_uses(*field.value, out);
            break;
        case ast::ExprKind::Unary:
            collect_place_uses(*static_cast<const ast::UnaryExpr&>(expr).operand, out);
            break;
        case ast::ExprKind::Binary: {
            const auto& binary = static_cast<const ast::BinaryExpr&>(expr);
            collect_place_uses(*binary.lhs, out);
            collect_place_uses(*binary.rhs, out);
            break;
        }
        case ast::ExprKind::Borrow:
            collect_place_uses(*static_cast<const ast::BorrowExpr&>(expr).target, out);
            break;
        case ast::ExprKind::Call: {
            const auto& call = static_cast<const ast::CallExpr&>(expr);
            collect_place_uses(*call.callee, out);
            for (const auto& arg : call.args)
                collect_place_uses(*arg, out);
            break;
        }
        case ast::ExprKind::Member:
            collect_place_uses(*static_cast<const ast::MemberExpr&>(expr).base, out);
            break;
        case ast::ExprKind::Index: {
            const auto& indexed = static_cast<const ast::IndexExpr&>(expr);
            collect_place_uses(*indexed.base, out);
            collect_place_uses(*indexed.index, out);
            break;
        }
        case ast::ExprKind::Await:
            collect_place_uses(*static_cast<const ast::AwaitExpr&>(expr).value, out);
            break;
        case ast::ExprKind::Cast:
            collect_place_uses(*static_cast<const ast::CastExpr&>(expr).value, out);
            break;
        case ast::ExprKind::Asm:
            for (const auto& operand : static_cast<const ast::AsmExpr&>(expr).operands) {
                if (operand.value)
                    collect_place_uses(*operand.value, out);
                if (!operand.out_name.empty())
                    out.insert(operand.out_name);
            }
            break;
        default:
            break;
    }
}

BorrowChecker::LivePlaceSet BorrowChecker::expression_place_uses(const ast::Expr& expr) const {
    LivePlaceSet out;
    collect_place_uses(expr, out);
    return out;
}

BorrowChecker::LiveSet BorrowChecker::liveness_block(const ast::BlockStmt& block,
                                                     const LiveSet& live_after,
                                                     const LiveSet& break_live,
                                                     const LiveSet& continue_live) {
    LiveSet live = live_after;
    for (auto it = block.stmts.rbegin(); it != block.stmts.rend(); ++it)
        live = liveness_stmt(**it, live, break_live, continue_live);
    return live;
}

BorrowChecker::LiveSet BorrowChecker::liveness_stmt(const ast::Stmt& stmt,
                                                    const LiveSet& live_after,
                                                    const LiveSet& break_live,
                                                    const LiveSet& continue_live) {
    live_after_[&stmt] = live_after;
    LiveSet before = live_after;
    auto add = [&](const LiveSet& values) { before.insert(values.begin(), values.end()); };

    if (const auto* let = dynamic_cast<const ast::LetStmt*>(&stmt)) {
        before.erase(binding_key(*let));
        if (let->init)
            add(expression_uses(*let->init));
    } else if (const auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
        const auto target = place_of(*assign->target);
        const bool whole_binding = target && *target == root_of(*target);
        if (assign->op == "=" && whole_binding)
            before.erase(*target);
        else
            add(expression_uses(*assign->target));
        add(expression_uses(*assign->value));
    } else if (const auto* expr = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
        add(expression_uses(*expr->expr));
    } else if (const auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
        before.clear();
        if (ret->value)
            add(expression_uses(*ret->value));
    } else if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
        before = liveness_block(*block, live_after, break_live, continue_live);
    } else if (const auto* branch = dynamic_cast<const ast::IfStmt*>(&stmt)) {
        const LiveSet left =
            liveness_block(*branch->then_block, live_after, break_live, continue_live);
        const LiveSet right =
            branch->else_block
                ? liveness_block(*branch->else_block, live_after, break_live, continue_live)
                : live_after;
        before = left;
        before.insert(right.begin(), right.end());
        add(expression_uses(*branch->condition));
    } else if (const auto* while_stmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
        LiveSet header = live_after;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            const LiveSet body = liveness_block(*while_stmt->body, header, live_after, header);
            LiveSet next = live_after;
            next.insert(body.begin(), body.end());
            const LiveSet condition = expression_uses(*while_stmt->condition);
            next.insert(condition.begin(), condition.end());
            if (next == header) {
                converged = true;
                break;
            }
            header = std::move(next);
        }
        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow liveness analysis did not converge; compilation stopped "
                         "conservatively");
        before = std::move(header);
    } else if (const auto* for_stmt = dynamic_cast<const ast::ForStmt*>(&stmt)) {
        LiveSet header = live_after;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            LiveSet body = liveness_block(*for_stmt->body, header, live_after, header);
            body.erase(for_binding_key(*for_stmt));
            LiveSet next = live_after;
            next.insert(body.begin(), body.end());
            if (next == header) {
                converged = true;
                break;
            }
            header = std::move(next);
        }
        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow liveness analysis did not converge; compilation stopped "
                         "conservatively");
        before = std::move(header);
        before.erase(for_binding_key(*for_stmt));
        add(expression_uses(*for_stmt->begin));
        add(expression_uses(*for_stmt->end));
    } else if (const auto* loop_stmt = dynamic_cast<const ast::LoopStmt*>(&stmt)) {
        LiveSet header = live_after;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            LiveSet next = liveness_block(*loop_stmt->body, header, live_after, header);
            if (next == header) {
                converged = true;
                break;
            }
            header = std::move(next);
        }
        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow liveness analysis did not converge; compilation stopped "
                         "conservatively");
        before = std::move(header);
    } else if (dynamic_cast<const ast::BreakStmt*>(&stmt)) {
        before = break_live;
    } else if (dynamic_cast<const ast::ContinueStmt*>(&stmt)) {
        before = continue_live;
    } else if (const auto* unsafe = dynamic_cast<const ast::UnsafeStmt*>(&stmt)) {
        before = liveness_block(*unsafe->body, live_after, break_live, continue_live);
    }

    live_before_[&stmt] = before;
    return before;
}

void BorrowChecker::prepare_liveness(const ast::FunctionDecl& fn) {
    live_before_.clear();
    live_after_.clear();
    live_places_before_.clear();
    live_places_after_.clear();
    if (!fn.body)
        return;
    const LiveSet empty;
    (void)liveness_block(*fn.body, empty, empty, empty);
    // Polonius-style second dimension: precise place liveness. Mirror the
    // root pass but with full `a#1.b` strings so sibling-field loans expire
    // independently and two-phase reservations are tracked precisely.
    const LivePlaceSet empty_places;
    (void)liveness_place_block(*fn.body, empty_places, empty_places, empty_places);
}

BorrowChecker::LivePlaceSet BorrowChecker::liveness_place_block(const ast::BlockStmt& block,
                                                                const LivePlaceSet& live_after,
                                                                const LivePlaceSet& break_live,
                                                                const LivePlaceSet& continue_live) {
    LivePlaceSet live = live_after;
    for (auto it = block.stmts.rbegin(); it != block.stmts.rend(); ++it)
        live = liveness_place_stmt(**it, live, break_live, continue_live);
    return live;
}

BorrowChecker::LivePlaceSet BorrowChecker::liveness_place_stmt(const ast::Stmt& stmt,
                                                               const LivePlaceSet& live_after,
                                                               const LivePlaceSet& break_live,
                                                               const LivePlaceSet& continue_live) {
    live_places_after_[&stmt] = live_after;
    LivePlaceSet before = live_after;
    auto add = [&](const LivePlaceSet& values) { before.insert(values.begin(), values.end()); };

    if (const auto* let = dynamic_cast<const ast::LetStmt*>(&stmt)) {
        // Kill the defined place and all its children (strong update on
        // whole-binding `=`), keep parent liveness for field updates.
        std::vector<std::string> to_erase;
        const std::string key = binding_key(*let);
        for (const auto& p : before) {
            if (p == key ||
                (p.size() > key.size() && p.substr(0, key.size()) == key && p[key.size()] == '.'))
                to_erase.push_back(p);
        }
        for (const auto& p : to_erase)
            before.erase(p);
        if (let->init)
            add(expression_place_uses(*let->init));
    } else if (const auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
        const auto target = place_of(*assign->target);
        const bool whole_binding = target && *target == root_of(*target);
        if (assign->op == "=" && whole_binding && target) {
            std::vector<std::string> to_erase;
            for (const auto& p : before) {
                if (p == *target ||
                    (p.size() > target->size() && p.substr(0, target->size()) == *target &&
                     (*target).size() < p.size() && p[target->size()] == '.'))
                    to_erase.push_back(p);
            }
            for (const auto& p : to_erase)
                before.erase(p);
        } else {
            if (assign->target)
                add(expression_place_uses(*assign->target));
        }
        add(expression_place_uses(*assign->value));
    } else if (const auto* expr = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
        add(expression_place_uses(*expr->expr));
    } else if (const auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
        before.clear();
        if (ret->value)
            add(expression_place_uses(*ret->value));
    } else if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
        before = liveness_place_block(*block, live_after, break_live, continue_live);
    } else if (const auto* branch = dynamic_cast<const ast::IfStmt*>(&stmt)) {
        const LivePlaceSet left =
            liveness_place_block(*branch->then_block, live_after, break_live, continue_live);
        const LivePlaceSet right =
            branch->else_block
                ? liveness_place_block(*branch->else_block, live_after, break_live, continue_live)
                : live_after;
        before = left;
        before.insert(right.begin(), right.end());
        add(expression_place_uses(*branch->condition));
    } else if (const auto* while_stmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
        LivePlaceSet header = live_after;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            const LivePlaceSet body =
                liveness_place_block(*while_stmt->body, header, live_after, header);
            LivePlaceSet next = live_after;
            next.insert(body.begin(), body.end());
            const LivePlaceSet condition = expression_place_uses(*while_stmt->condition);
            next.insert(condition.begin(), condition.end());
            if (next == header) {
                converged = true;
                break;
            }
            header = std::move(next);
        }
        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow liveness analysis did not converge; compilation stopped "
                         "conservatively");
        before = std::move(header);
    } else if (const auto* for_stmt = dynamic_cast<const ast::ForStmt*>(&stmt)) {
        LivePlaceSet header = live_after;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            LivePlaceSet body = liveness_place_block(*for_stmt->body, header, live_after, header);
            // Kill loop variable places in body liveness.
            const std::string loop_key = for_binding_key(*for_stmt);
            std::vector<std::string> to_erase;
            for (const auto& p : body) {
                if (p == loop_key || root_of(p) == loop_key)
                    to_erase.push_back(p);
            }
            for (const auto& p : to_erase)
                body.erase(p);
            LivePlaceSet next = live_after;
            next.insert(body.begin(), body.end());
            if (next == header) {
                converged = true;
                break;
            }
            header = std::move(next);
        }
        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow liveness analysis did not converge; compilation stopped "
                         "conservatively");
        before = std::move(header);
        {
            const std::string loop_key = for_binding_key(*for_stmt);
            std::vector<std::string> to_erase;
            for (const auto& p : before) {
                if (p == loop_key || root_of(p) == loop_key)
                    to_erase.push_back(p);
            }
            for (const auto& p : to_erase)
                before.erase(p);
        }
        add(expression_place_uses(*for_stmt->begin));
        add(expression_place_uses(*for_stmt->end));
    } else if (const auto* loop_stmt = dynamic_cast<const ast::LoopStmt*>(&stmt)) {
        LivePlaceSet header = live_after;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            LivePlaceSet next = liveness_place_block(*loop_stmt->body, header, live_after, header);
            if (next == header) {
                converged = true;
                break;
            }
            header = std::move(next);
        }
        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow liveness analysis did not converge; compilation stopped "
                         "conservatively");
        before = std::move(header);
    } else if (dynamic_cast<const ast::BreakStmt*>(&stmt)) {
        before = break_live;
    } else if (dynamic_cast<const ast::ContinueStmt*>(&stmt)) {
        before = continue_live;
    } else if (const auto* unsafe = dynamic_cast<const ast::UnsafeStmt*>(&stmt)) {
        before = liveness_place_block(*unsafe->body, live_after, break_live, continue_live);
    }

    live_places_before_[&stmt] = before;
    return before;
}

void BorrowChecker::expire_dead_loans(const ast::Stmt& stmt, State& state) {
    const auto live_it = live_before_.find(&stmt);
    const auto live_places_it = live_places_before_.find(&stmt);
    const LiveSet empty_roots;
    const LivePlaceSet empty_places;
    const LiveSet& live_roots = live_it == live_before_.end() ? empty_roots : live_it->second;
    const LivePlaceSet& live_places =
        live_places_it == live_places_before_.end() ? empty_places : live_places_it->second;
    state.borrows.erase(std::remove_if(state.borrows.begin(),
                                       state.borrows.end(),
                                       [&](const Borrow& loan) {
                                           if (loan.borrower == "<temporary>")
                                               return true;
                                           // Two-phase reservations with `<twophase:>` borrower
                                           // are statement-local; keep until activation.
                                           if (loan.borrower.starts_with("<twophase:"))
                                               return false;
                                           return !loan_live_at(loan, live_roots, live_places);
                                       }),
                        state.borrows.end());
}
} // namespace uinx
