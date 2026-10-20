// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "borrow_internal.hpp"
#include "uinx/borrow.hpp"

#include <algorithm>
#include <sstream>

namespace uinx {
using namespace borrow_detail;

void BorrowChecker::merge_states(State& out, const State& left, const State& right) const {
    if (!left.reachable && !right.reachable) {
        out = left;
        out.reachable = false;
        return;
    }
    if (!left.reachable) {
        out = right;
        return;
    }
    if (!right.reachable) {
        out = left;
        return;
    }

    out = left;
    out.reachable = true;
    out.moved.insert(right.moved.begin(), right.moved.end());
    for (const auto& loan : right.borrows) {
        if (std::none_of(out.borrows.begin(), out.borrows.end(), [&](const Borrow& existing) {
                return same_borrow(existing, loan);
            })) {
            out.borrows.push_back(loan);
        }
    }
    for (const auto& [place, origins] : right.ref_origins) {
        auto& merged = out.ref_origins[place];
        for (const auto& origin : origins) {
            if (std::none_of(merged.begin(), merged.end(), [&](const RefOrigin& existing) {
                    return same_origin(existing, origin);
                })) {
                merged.push_back(origin);
            }
        }
    }
}

void BorrowChecker::merge_into(std::optional<State>& accumulator, const State& state) const {
    if (!state.reachable)
        return;
    if (!accumulator) {
        accumulator = state;
        return;
    }
    State merged;
    merge_states(merged, *accumulator, state);
    accumulator = std::move(merged);
}

void BorrowChecker::truncate_scopes(State& state, std::size_t depth) {
    while (state.scopes.size() > depth) {
        std::unordered_set<std::string> locals;
        for (const auto& [name, type] : state.scopes.back()) {
            (void)type;
            locals.insert(name);
        }

        for (const auto& [borrower, origins] : state.ref_origins) {
            if (locals.contains(root_of(borrower)))
                continue;
            for (const auto& origin : origins) {
                if (locals.contains(root_of(origin.place))) {
                    report_error(origin.range,
                                 "E0405",
                                 "reference stored in '" + display_place(borrower) +
                                     "' outlives local place '" + display_place(origin.place) +
                                     "'");
                }
            }
        }

        state.scopes.pop_back();
        state.borrows.erase(std::remove_if(state.borrows.begin(),
                                           state.borrows.end(),
                                           [&](const Borrow& loan) {
                                               return locals.contains(root_of(loan.borrower));
                                           }),
                            state.borrows.end());
        for (auto it = state.moved.begin(); it != state.moved.end();) {
            if (locals.contains(root_of(*it)))
                it = state.moved.erase(it);
            else
                ++it;
        }
        for (auto it = state.ref_origins.begin(); it != state.ref_origins.end();) {
            if (locals.contains(root_of(it->first)))
                it = state.ref_origins.erase(it);
            else
                ++it;
        }
    }
}

void BorrowChecker::inspect_expr(const ast::Expr& expr, State& state, Access access) {
    if (!state.reachable)
        return;
    if (auto place = place_of(expr)) {
        access_place(*place, expr.range, access, state);
        return;
    }

    switch (expr.kind) {
        case ast::ExprKind::StructLiteral:
            for (const auto& field : static_cast<const ast::StructLiteralExpr&>(expr).fields)
                inspect_expr(*field.value, state, Access::Move);
            break;
        case ast::ExprKind::Unary: {
            const auto& unary = static_cast<const ast::UnaryExpr&>(expr);
            inspect_expr(*unary.operand,
                         state,
                         unary.op == "*" ? Access::Read
                                         : (unary.op == "move" ? Access::Move : access));
            break;
        }
        case ast::ExprKind::Binary: {
            const auto& binary = static_cast<const ast::BinaryExpr&>(expr);
            inspect_expr(*binary.lhs, state, Access::Move);
            inspect_expr(*binary.rhs, state, Access::Move);
            break;
        }
        case ast::ExprKind::Borrow: {
            const auto& borrow = static_cast<const ast::BorrowExpr&>(expr);
            if (auto place = place_of(*borrow.target)) {
                begin_borrow(*place, "<temporary>", borrow.mut, expr.range, state);
            } else {
                const auto origins = reference_origins(*borrow.target, state);
                if (!origins.empty()) {
                    for (const auto& origin : origins)
                        begin_borrow(origin.place, "<temporary>", borrow.mut, expr.range, state);
                } else {
                    inspect_expr(*borrow.target, state, Access::Read);
                }
            }
            break;
        }
        case ast::ExprKind::Call: {
            const auto& call = static_cast<const ast::CallExpr&>(expr);
            if (call.callee->kind == ast::ExprKind::Member) {
                const auto& member = static_cast<const ast::MemberExpr&>(*call.callee);
                if (is_two_phase_method_receiver(*member.base, call)) {
                    inspect_call_with_two_phase(call, state);
                    break;
                }
                Type receiver{};
                if (const auto type = model_.expr_types.find(member.base.get());
                    type != model_.expr_types.end()) {
                    receiver = type->second;
                }
                Type owner = receiver;
                if (owner.kind == TypeKind::Ref && owner.pointee)
                    owner = *owner.pointee;
                const auto method = model_.functions.find(owner.name + "::" + member.member);
                if (method != model_.functions.end() && !method->second.params.empty()) {
                    Access receiver_access = Access::Move;
                    const Type& self_type = method->second.params.front();
                    if (self_type.kind == TypeKind::Ref)
                        receiver_access = self_type.mut ? Access::BorrowMut : Access::BorrowShared;
                    inspect_expr(*member.base, state, receiver_access);
                } else {
                    inspect_expr(*member.base, state, Access::Move);
                }
            }
            // Two-phase for function args with `borrow mut` reborrows
            // (e.g. `replace(r, vec![r.len()])`): reserve all `borrow mut`
            // args as shared-like, inspect remaining args, then activate.
            // Detect `borrow mut <place>` args directly.
            std::vector<std::string> reserved_places;
            std::vector<std::string> reservation_ids;
            for (std::size_t i = 0; i < call.args.size(); ++i) {
                const auto& arg = call.args[i];
                if (arg->kind == ast::ExprKind::Borrow) {
                    const auto& b = static_cast<const ast::BorrowExpr&>(*arg);
                    if (b.mut) {
                        if (auto place = place_of(*b.target)) {
                            const std::string rid =
                                "<twophase:arg:" + std::to_string(call.range.begin.line) + ":" +
                                std::to_string(call.range.begin.column) + ":" + std::to_string(i) +
                                ">";
                            begin_two_phase_reservation(*place, rid, call.range, state);
                            reserved_places.push_back(*place);
                            reservation_ids.push_back(rid);
                        }
                    }
                }
            }
            if (!reserved_places.empty()) {
                for (const auto& arg : call.args) {
                    // Skip already-reserved `borrow mut` args (reservation
                    // covers them); inspect others while reserved.
                    if (arg->kind == ast::ExprKind::Borrow &&
                        static_cast<const ast::BorrowExpr&>(*arg).mut)
                        continue;
                    const auto type = model_.expr_types.find(arg.get());
                    Access arg_access = Access::Move;
                    if (type != model_.expr_types.end() && type->second.kind == TypeKind::Ref &&
                        !type->second.mut) {
                        arg_access = Access::Read;
                    }
                    inspect_expr(*arg, state, arg_access);
                }
                for (const auto& rid : reservation_ids)
                    activate_two_phase(rid, call.range, state);
                for (const auto& rid : reservation_ids)
                    kill_borrower(rid, state);
                break;
            }
            for (const auto& arg : call.args) {
                const auto type = model_.expr_types.find(arg.get());
                Access arg_access = Access::Move;
                if (type != model_.expr_types.end() && type->second.kind == TypeKind::Ref &&
                    !type->second.mut) {
                    arg_access = Access::Read;
                }
                inspect_expr(*arg, state, arg_access);
            }
            break;
        }
        case ast::ExprKind::Index: {
            const auto& indexed = static_cast<const ast::IndexExpr&>(expr);
            inspect_expr(*indexed.base, state, Access::Read);
            inspect_expr(*indexed.index, state, Access::Move);
            break;
        }
        case ast::ExprKind::Await:
            if (state.async_context) {
                for (const auto& loan : state.borrows) {
                    if (loan.borrower != "<temporary>" && is_stack_owned(loan.place, state)) {
                        report_error(expr.range,
                                     "E0407",
                                     "safe reference to stack-owned place '" +
                                         display_place(loan.place) +
                                         "' cannot remain live across await");
                    }
                }
            }
            inspect_expr(*static_cast<const ast::AwaitExpr&>(expr).value, state, Access::Move);
            break;
        case ast::ExprKind::Cast:
            inspect_expr(*static_cast<const ast::CastExpr&>(expr).value, state, Access::Move);
            break;
        case ast::ExprKind::Asm:
            for (const auto& operand : static_cast<const ast::AsmExpr&>(expr).operands) {
                if (operand.value)
                    inspect_expr(*operand.value, state, Access::Read);
                if (!operand.out_name.empty())
                    access_place(operand.out_name, operand.range, Access::Write, state);
            }
            break;
        default:
            break;
    }
}
} // namespace uinx
