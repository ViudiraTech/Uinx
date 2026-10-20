// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "borrow_internal.hpp"
#include "uinx/borrow.hpp"

#include <algorithm>
#include <sstream>

namespace uinx {
using namespace borrow_detail;

void BorrowChecker::set_reference_origins(const std::string& place,
                                          std::vector<RefOrigin> origins,
                                          State& state) {
    for (auto it = state.ref_origins.begin(); it != state.ref_origins.end();) {
        if (it->first == place || is_strict_child(place, it->first))
            it = state.ref_origins.erase(it);
        else
            ++it;
    }
    if (origins.empty())
        return;

    std::vector<RefOrigin> unique;
    for (auto& origin : origins) {
        const bool duplicate = std::any_of(unique.begin(), unique.end(), [&](const RefOrigin& x) {
            return same_origin(x, origin);
        });
        if (!duplicate)
            unique.push_back(std::move(origin));
    }
    state.ref_origins[place] = std::move(unique);
}

void BorrowChecker::promote_reference_value(const std::string& borrower,
                                            const std::vector<RefOrigin>& origins,
                                            State& state) {
    if (origins.empty())
        return;

    state.borrows.erase(
        std::remove_if(state.borrows.begin(),
                       state.borrows.end(),
                       [&](const Borrow& loan) {
                           if (loan.borrower != "<temporary>")
                               return false;
                           return std::any_of(
                               origins.begin(), origins.end(), [&](const RefOrigin& o) {
                                   return overlaps(loan.place, o.place);
                               });
                       }),
        state.borrows.end());

    for (const auto& origin : origins)
        begin_borrow(origin.place, borrower, origin.mut, origin.range, state);
}

void BorrowChecker::bind_reference_expression(const std::string& destination,
                                              const ast::Expr& expression,
                                              State& state) {
    if (expression.kind == ast::ExprKind::StructLiteral) {
        const auto& literal = static_cast<const ast::StructLiteralExpr&>(expression);
        for (const auto& field : literal.fields)
            bind_reference_expression(destination + "." + field.name, *field.value, state);
        return;
    }

    if (const auto source = place_of(expression)) {
        std::vector<std::pair<std::string, std::vector<RefOrigin>>> mapped;
        for (const auto& [stored_place, origins] : state.ref_origins) {
            if (stored_place == *source || is_strict_child(*source, stored_place)) {
                const std::string suffix = stored_place.substr(source->size());
                mapped.push_back({destination + suffix, origins});
            }
        }
        if (!mapped.empty()) {
            const Type source_type = type_of_place(*source, state);
            const bool transfer = !is_copy_type(source_type);
            if (transfer) {
                kill_borrowers_under(*source, state);
                for (auto it = state.ref_origins.begin(); it != state.ref_origins.end();) {
                    if (it->first == *source || is_strict_child(*source, it->first))
                        it = state.ref_origins.erase(it);
                    else
                        ++it;
                }
            }
            for (auto& [target, origins] : mapped) {
                promote_reference_value(target, origins, state);
                set_reference_origins(target, std::move(origins), state);
            }
            return;
        }
    }

    auto origins = reference_origins(expression, state);
    if (!origins.empty()) {
        promote_reference_value(destination, origins, state);
        set_reference_origins(destination, std::move(origins), state);
    }
}

void BorrowChecker::access_place(const std::string& place,
                                 const SourceRange& range,
                                 Access access,
                                 State& state) {
    if (access == Access::Move) {
        const Type type = type_of_place(place, state);
        if (is_copy_type(type))
            access = Access::Read;
    }

    if (access == Access::Write) {
        if (has_moved_ancestor(place, state)) {
            report_error(range,
                         "E0400",
                         "cannot initialize field/place '" + display_place(place) +
                             "' because an enclosing value was moved");
            return;
        }
    } else if (unavailable(place, state)) {
        report_error(range, "E0400", "use of moved value/place '" + display_place(place) + "'");
        return;
    }

    auto is_active_mut = [](const Borrow& loan) {
        if (loan.kind == BorrowKind::TwoPhaseReserved)
            return false; // reservation acts as shared
        return loan.mut;
    };
    auto is_active_exclusive = [](const Borrow& loan) {
        if (loan.kind == BorrowKind::TwoPhaseReserved)
            return false;
        return loan.kind == BorrowKind::Mut || loan.kind == BorrowKind::TwoPhaseActivated;
    };

    if (access == Access::Read) {
        for (const auto& loan : state.borrows) {
            if (places_conflict(place, loan.place) && is_active_mut(loan)) {
                report_error(range,
                             "E0406",
                             "cannot read '" + display_place(place) +
                                 "' while mutable borrow of '" + display_place(loan.place) +
                                 "' is active");
            }
        }
        return;
    }

    if (access == Access::BorrowShared) {
        for (const auto& loan : state.borrows) {
            if (places_conflict(place, loan.place) && is_active_mut(loan)) {
                report_error(range,
                             "E0401",
                             "cannot immutably borrow '" + display_place(place) +
                                 "' while mutable borrow of '" + display_place(loan.place) +
                                 "' is active");
            }
        }
        return;
    }

    if (access == Access::TwoPhaseReserve) {
        // Reservation phase: only conflicts with active exclusive borrows.
        for (const auto& loan : state.borrows) {
            if (places_conflict(place, loan.place) && is_active_exclusive(loan)) {
                report_error(range,
                             "E0402",
                             "cannot reserve mutably '" + display_place(place) +
                                 "' while exclusive borrow of '" + display_place(loan.place) +
                                 "' is active");
            }
        }
        return;
    }

    if (access == Access::BorrowMut) {
        for (const auto& loan : state.borrows) {
            if (places_conflict(place, loan.place)) {
                // A two-phase reservation of the same place by the same
                // statement is upgraded at activation, not a conflict here.
                // Direct overlapping live loans are still errors.
                report_error(range,
                             "E0402",
                             "cannot mutably borrow '" + display_place(place) +
                                 "' while borrow of '" + display_place(loan.place) + "' is active");
            }
        }
        return;
    }

    if (access == Access::Write || access == Access::Move) {
        for (const auto& loan : state.borrows) {
            if (places_conflict(place, loan.place)) {
                report_error(range,
                             "E0403",
                             "cannot " + std::string(access == Access::Move ? "move" : "write") +
                                 " '" + display_place(place) + "' while it is borrowed");
            }
        }
    }

    if (access == Access::Move)
        state.moved.insert(place);
}

void BorrowChecker::begin_borrow(const std::string& place,
                                 std::string borrower,
                                 bool mut,
                                 const SourceRange& range,
                                 State& state) {
    access_place(place, range, mut ? Access::BorrowMut : Access::BorrowShared, state);
    Borrow loan;
    loan.place = place;
    loan.borrower = std::move(borrower);
    loan.mut = mut;
    loan.kind = mut ? BorrowKind::Mut : BorrowKind::Shared;
    loan.origin = range;
    loan.reservation = range;
    loan.activated = mut;
    if (std::none_of(state.borrows.begin(), state.borrows.end(), [&](const Borrow& existing) {
            return same_borrow(existing, loan);
        })) {
        state.borrows.push_back(std::move(loan));
    }
}

void BorrowChecker::begin_two_phase_reservation(const std::string& place,
                                                std::string borrower,
                                                const SourceRange& reservation,
                                                State& state) {
    access_place(place, reservation, Access::TwoPhaseReserve, state);
    Borrow loan;
    loan.place = place;
    loan.borrower = std::move(borrower);
    loan.mut = true;
    loan.kind = BorrowKind::TwoPhaseReserved;
    loan.origin = reservation;
    loan.reservation = reservation;
    loan.activated = false;
    if (std::none_of(state.borrows.begin(), state.borrows.end(), [&](const Borrow& existing) {
            return same_borrow(existing, loan);
        })) {
        state.borrows.push_back(std::move(loan));
    }
}

void BorrowChecker::activate_two_phase(const std::string& borrower,
                                       const SourceRange& activation,
                                       State& state) {
    for (auto& loan : state.borrows) {
        if (loan.borrower == borrower && loan.kind == BorrowKind::TwoPhaseReserved) {
            // Activation: now acts as exclusive. Check against live shared.
            for (const auto& other : state.borrows) {
                if (&other == &loan)
                    continue;
                if (places_conflict(loan.place, other.place)) {
                    report_error(activation,
                                 "E0402",
                                 "cannot activate mutable borrow of '" + display_place(loan.place) +
                                     "' while borrow of '" + display_place(other.place) +
                                     "' is active");
                }
            }
            loan.kind = BorrowKind::TwoPhaseActivated;
            loan.origin = activation;
            loan.activated = true;
        }
    }
}

bool BorrowChecker::is_two_phase_method_receiver(const ast::Expr& base,
                                                 const ast::CallExpr& call) const {
    if (call.callee->kind != ast::ExprKind::Member)
        return false;
    const auto& member = static_cast<const ast::MemberExpr&>(*call.callee);
    Type receiver{};
    if (const auto type = model_.expr_types.find(&base); type != model_.expr_types.end())
        receiver = type->second;
    Type owner = receiver;
    if (owner.kind == TypeKind::Ref && owner.pointee)
        owner = *owner.pointee;
    const auto method = model_.functions.find(owner.name + "::" + member.member);
    if (method == model_.functions.end() || method->second.params.empty())
        return false;
    const Type& self_type = method->second.params.front();
    return self_type.kind == TypeKind::Ref && self_type.mut;
}

void BorrowChecker::inspect_call_with_two_phase(const ast::CallExpr& call, State& state) {
    // Two-phase protocol for `receiver.mut_method(args...)`:
    // reserve receiver (shared-like), inspect args, then activate.
    const auto& member = static_cast<const ast::MemberExpr&>(*call.callee);
    const auto receiver_place = place_of(*member.base);
    const std::string reservation_id = "<twophase:" + std::to_string(call.range.begin.line) + ":" +
                                       std::to_string(call.range.begin.column) + ">";
    bool reserved = false;
    if (receiver_place) {
        begin_two_phase_reservation(*receiver_place, reservation_id, call.range, state);
        reserved = true;
    } else {
        inspect_expr(*member.base, state, Access::BorrowShared);
    }
    // Args are inspected while the receiver reservation is shared-like, so
    // `v.push(v.len())` is accepted: `v.len()` shared-borrows while reserved.
    for (const auto& arg : call.args) {
        const auto type = model_.expr_types.find(arg.get());
        Access arg_access = Access::Move;
        if (type != model_.expr_types.end() && type->second.kind == TypeKind::Ref &&
            !type->second.mut) {
            arg_access = Access::Read;
        }
        inspect_expr(*arg, state, arg_access);
    }
    if (reserved) {
        activate_two_phase(reservation_id, call.range, state);
        // Call-duration exclusive borrow ends with the call (unless the
        // return value carries provenance, handled via reference_origins).
        kill_borrower(reservation_id, state);
    }
    // Receiver provenance: method may return borrow of receiver.
    // Keep existing reference_origins handling via caller.
}
} // namespace uinx
