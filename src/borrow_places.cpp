// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "borrow_internal.hpp"
#include "uinx/borrow.hpp"

#include <algorithm>
#include <sstream>

namespace uinx {
using namespace borrow_detail;

void BorrowChecker::report_error(const SourceRange& range, std::string code, std::string message) {
    std::ostringstream key;
    key << code << '@' << range.begin.line << ':' << range.begin.column << '-' << range.end.line
        << ':' << range.end.column << ':' << message;
    if (emitted_errors_.insert(key.str()).second)
        diags_.error(range, std::move(code), std::move(message));
}

bool BorrowChecker::overlaps(std::string_view a, std::string_view b) const {
    return places_conflict(a, b);
}

bool BorrowChecker::places_disjoint(std::string_view a, std::string_view b) const {
    // Conservative external provenance always conflicts.
    if (a.starts_with("<external:") || b.starts_with("<external:"))
        return false;
    if (a == b)
        return false;
    const auto pa = split_place(a);
    const auto pb = split_place(b);
    const std::size_t common = std::min(pa.size(), pb.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (pa[i] != pb[i]) {
            // Divergence at the same struct level with two distinct named
            // fields is disjoint (Rust places_conflict: sibling fields).
            // Any divergence involving `[*]` is conservative overlap because
            // dynamic indexes may alias.
            if (pa[i] == "[*]" || pb[i] == "[*]")
                return false;
            return true;
        }
    }
    // One is a strict prefix of the other => parent/child overlap.
    return false;
}

bool BorrowChecker::places_conflict(std::string_view a, std::string_view b) const {
    return !places_disjoint(a, b);
}

bool BorrowChecker::loan_live_at(const Borrow& loan,
                                 const LiveSet& live_roots,
                                 const LivePlaceSet& live_places) const {
    if (loan.borrower == "<temporary>")
        return true;
    const std::string borrower_root = root_of(loan.borrower);
    if (model_.globals.contains(borrower_root))
        return true;
    // Polonius-style: a loan is live only if its borrower may be used later.
    // Check both root liveness (NLL) and precise place liveness. If either
    // indicates a future use, the loan stays live.
    if (live_roots.contains(borrower_root))
        return true;
    if (live_places.contains(loan.borrower))
        return true;
    // Subset edges: if any live place overlaps the borrower (e.g. a field of
    // the borrower is live), the loan stays live.
    for (const auto& live : live_places) {
        if (places_conflict(live, loan.borrower))
            return true;
    }
    return false;
}

bool BorrowChecker::same_origin(const RefOrigin& a, const RefOrigin& b) const {
    return a.place == b.place && a.mut == b.mut && a.range.begin.line == b.range.begin.line &&
           a.range.begin.column == b.range.begin.column;
}

bool BorrowChecker::same_borrow(const Borrow& a, const Borrow& b) const {
    return a.place == b.place && a.borrower == b.borrower && a.mut == b.mut && a.kind == b.kind &&
           a.origin.begin.line == b.origin.begin.line &&
           a.origin.begin.column == b.origin.begin.column;
}

std::optional<std::string> BorrowChecker::place_of(const ast::Expr& expr) const {
    if (expr.kind == ast::ExprKind::Name) {
        const auto& name = static_cast<const ast::NameExpr&>(expr).name;
        const auto resolved = model_.hir.expr_resolution.find(&expr);
        if (resolved != model_.hir.expr_resolution.end() &&
            resolved->second < model_.hir.symbols.size()) {
            const auto& symbol = model_.hir.symbols[resolved->second];
            if (symbol.kind == hir::SymbolKind::Local || symbol.kind == hir::SymbolKind::Parameter)
                return symbol_key(name, resolved->second);
        }
        return name;
    }
    if (expr.kind == ast::ExprKind::Member) {
        const auto& member = static_cast<const ast::MemberExpr&>(expr);
        auto base = place_of(*member.base);
        if (base)
            return *base + "." + member.member;
    }
    if (expr.kind == ast::ExprKind::Index) {
        const auto& index = static_cast<const ast::IndexExpr&>(expr);
        auto base = place_of(*index.base);
        if (base)
            return *base + ".[*]";
    }
    return std::nullopt;
}

std::string BorrowChecker::binding_key(const ast::LetStmt& binding) const {
    const auto found = model_.hir.bindings.find(&binding);
    return found == model_.hir.bindings.end() ? binding.name
                                              : symbol_key(binding.name, found->second);
}

std::string BorrowChecker::parameter_key(const ast::Param& parameter) const {
    const auto found = model_.hir.params.find(&parameter);
    return found == model_.hir.params.end() ? parameter.name
                                            : symbol_key(parameter.name, found->second);
}

std::string BorrowChecker::for_binding_key(const ast::ForStmt& loop) const {
    const auto found = model_.hir.for_bindings.find(&loop);
    return found == model_.hir.for_bindings.end() ? loop.name
                                                  : symbol_key(loop.name, found->second);
}

std::vector<BorrowChecker::RefOrigin> BorrowChecker::reference_origins(const ast::Expr& expr,
                                                                       const State& state) const {
    std::vector<RefOrigin> result;
    auto append = [&](std::vector<RefOrigin> more) {
        for (auto& origin : more) {
            const bool duplicate =
                std::any_of(result.begin(), result.end(), [&](const RefOrigin& x) {
                    return same_origin(x, origin);
                });
            if (!duplicate)
                result.push_back(std::move(origin));
        }
    };

    if (expr.kind == ast::ExprKind::Borrow) {
        const auto& borrow = static_cast<const ast::BorrowExpr&>(expr);
        if (auto place = place_of(*borrow.target)) {
            result.push_back({*place, expr.range, borrow.mut, {}});
            return result;
        }
        result = reference_origins(*borrow.target, state);
        for (auto& origin : result)
            origin.mut = borrow.mut;
        if (const auto parent = reborrow_parent(*borrow.target)) {
            // Reference parameters have no recorded origin; track the reborrow
            // against an external place so the parent is still frozen.
            if (result.empty())
                result.push_back({"<external:" + *parent + ">", expr.range, borrow.mut, {}});
            for (auto& origin : result) {
                origin.range = expr.range;
                origin.via.insert(origin.via.begin(), *parent);
            }
        }
        return result;
    }

    if (auto place = place_of(expr)) {
        for (const auto& [stored_place, origins] : state.ref_origins) {
            if (*place == stored_place || is_strict_child(*place, stored_place) ||
                is_strict_child(stored_place, *place)) {
                append(origins);
            }
        }
        if (!result.empty())
            return result;
    }

    switch (expr.kind) {
        case ast::ExprKind::StructLiteral:
            for (const auto& field : static_cast<const ast::StructLiteralExpr&>(expr).fields)
                append(reference_origins(*field.value, state));
            break;
        case ast::ExprKind::Unary:
            append(reference_origins(*static_cast<const ast::UnaryExpr&>(expr).operand, state));
            break;
        case ast::ExprKind::Call: {
            bool result_can_contain_reference = false;
            if (const auto type = model_.expr_types.find(&expr); type != model_.expr_types.end()) {
                std::unordered_set<std::string> visiting;
                result_can_contain_reference = contains_reference(type->second, visiting);
            }
            if (result_can_contain_reference) {
                const auto& call = static_cast<const ast::CallExpr&>(expr);
                if (call.callee->kind == ast::ExprKind::Member) {
                    const auto& member = static_cast<const ast::MemberExpr&>(*call.callee);
                    auto receiver_origins = reference_origins(*member.base, state);
                    if (!receiver_origins.empty()) {
                        append(std::move(receiver_origins));
                    } else if (const auto receiver_place = place_of(*member.base)) {
                        Type receiver_type{};
                        if (const auto found = model_.expr_types.find(member.base.get());
                            found != model_.expr_types.end())
                            receiver_type = found->second;

                        Type owner = receiver_type;
                        if (owner.kind == TypeKind::Ref && owner.pointee)
                            owner = *owner.pointee;
                        const auto method =
                            model_.functions.find(owner.name + "::" + member.member);
                        if (method != model_.functions.end() && !method->second.params.empty() &&
                            method->second.params.front().kind == TypeKind::Ref) {
                            if (receiver_type.kind == TypeKind::Ref) {
                                // A reference parameter can point outside the current frame.  Keep
                                // that provenance external inside this function; the caller maps
                                // it back to its concrete argument when the call itself is bound.
                                result.push_back({"<external:" + *receiver_place + ">",
                                                  expr.range,
                                                  method->second.params.front().mut,
                                                  {}});
                            } else {
                                result.push_back({*receiver_place,
                                                  expr.range,
                                                  method->second.params.front().mut,
                                                  {}});
                            }
                        }
                    }
                }
                for (const auto& arg : call.args)
                    append(reference_origins(*arg, state));
            }
            break;
        }
        case ast::ExprKind::Member:
            append(reference_origins(*static_cast<const ast::MemberExpr&>(expr).base, state));
            break;
        case ast::ExprKind::Index:
            append(reference_origins(*static_cast<const ast::IndexExpr&>(expr).base, state));
            break;
        case ast::ExprKind::Cast:
            append(reference_origins(*static_cast<const ast::CastExpr&>(expr).value, state));
            break;
        case ast::ExprKind::Await:
            append(reference_origins(*static_cast<const ast::AwaitExpr&>(expr).value, state));
            break;
        default:
            break;
    }
    return result;
}

bool BorrowChecker::contains_reference(const Type& type,
                                       std::unordered_set<std::string>& visiting) const {
    if (type.kind == TypeKind::Ref)
        return true;
    if (type.kind == TypeKind::Generic)
        return true;
    if (type.kind != TypeKind::Named)
        return false;
    if (!visiting.insert(type.name).second)
        return false;

    const auto structure = model_.structs.find(type.name);
    if (structure == model_.structs.end()) {
        visiting.erase(type.name);
        return false;
    }

    std::unordered_map<std::string, Type> substitutions;
    for (std::size_t i = 0; i < structure->second.generic_names.size() && i < type.args.size();
         ++i) {
        substitutions[structure->second.generic_names[i]] = type.args[i];
    }

    for (const auto& [name, field] : structure->second.fields) {
        (void)name;
        if (contains_reference(substitute_type(field, substitutions), visiting)) {
            visiting.erase(type.name);
            return true;
        }
    }
    visiting.erase(type.name);
    return false;
}

Type BorrowChecker::type_of_place(std::string_view place, const State& state) const {
    const std::string root = root_of(place);
    Type type;
    bool found = false;
    for (auto it = state.scopes.rbegin(); it != state.scopes.rend(); ++it) {
        const auto binding = it->find(root);
        if (binding != it->end()) {
            type = binding->second;
            found = true;
            break;
        }
    }
    if (!found) {
        const auto global = model_.globals.find(root);
        if (global != model_.globals.end()) {
            type = global->second.type;
            found = true;
        }
    }
    if (!found)
        return {};

    std::size_t pos = root.size();
    while (pos < place.size() && place[pos] == '.') {
        ++pos;
        const auto next = place.find('.', pos);
        const std::string field(
            place.substr(pos, next == std::string_view::npos ? place.size() - pos : next - pos));
        if (type.kind == TypeKind::Ref && type.pointee)
            type = *type.pointee;
        if (field == "[*]") {
            if (type.kind == TypeKind::Named && (type.name == "Slice" || type.name == "SliceMut") &&
                !type.args.empty()) {
                type = type.args[0];
            } else {
                return {};
            }
        } else {
            const auto structure = model_.structs.find(type.name);
            if (structure == model_.structs.end())
                return {};
            const auto member = structure->second.fields.find(field);
            if (member == structure->second.fields.end())
                return {};
            std::unordered_map<std::string, Type> substitutions;
            for (std::size_t i = 0;
                 i < structure->second.generic_names.size() && i < type.args.size();
                 ++i) {
                substitutions[structure->second.generic_names[i]] = type.args[i];
            }
            type = substitute_type(member->second, substitutions);
        }
        if (next == std::string_view::npos)
            break;
        pos = next;
    }
    return type;
}

bool BorrowChecker::unavailable(std::string_view place, const State& state) const {
    return std::any_of(state.moved.begin(), state.moved.end(), [&](const std::string& moved) {
        return overlaps(moved, place);
    });
}

bool BorrowChecker::has_moved_ancestor(std::string_view place, const State& state) const {
    return std::any_of(state.moved.begin(), state.moved.end(), [&](const std::string& moved) {
        return moved != place && is_strict_child(moved, place);
    });
}

bool BorrowChecker::is_stack_owned(std::string_view place, const State& state) const {
    const std::string root = root_of(place);
    for (const auto& scope : state.scopes) {
        const auto binding = scope.find(root);
        if (binding != scope.end()) {
            // `ref T`/`mutref T` parameters and locals are stack bindings, but a field/index
            // reached through them belongs to the referent, not to the reference slot itself.
            // Borrowing the reference slot (`borrow r`) still correctly counts as stack-owned.
            if (binding->second.kind == TypeKind::Ref && is_strict_child(root, place))
                return false;
            return true;
        }
    }
    return false;
}

bool BorrowChecker::is_copy_type(const Type& type) const {
    if (type.kind == TypeKind::Generic && copy_generics_.contains(type.name))
        return true;
    if (type.is_copy())
        return true;
    TraitSolver solver(model_);
    return solver.satisfies(type, "Copy");
}

bool BorrowChecker::states_equivalent(const State& a, const State& b) const {
    if (a.reachable != b.reachable || a.moved != b.moved || a.scopes.size() != b.scopes.size() ||
        a.borrows.size() != b.borrows.size() || a.ref_origins.size() != b.ref_origins.size()) {
        return false;
    }
    for (const auto& borrow : a.borrows) {
        if (std::none_of(b.borrows.begin(), b.borrows.end(), [&](const Borrow& other) {
                return same_borrow(borrow, other);
            })) {
            return false;
        }
    }
    for (const auto& [place, origins] : a.ref_origins) {
        const auto found = b.ref_origins.find(place);
        if (found == b.ref_origins.end() || found->second.size() != origins.size())
            return false;
        for (const auto& origin : origins) {
            if (std::none_of(found->second.begin(),
                             found->second.end(),
                             [&](const RefOrigin& other) { return same_origin(origin, other); })) {
                return false;
            }
        }
    }
    return true;
}

void BorrowChecker::mark_initialized(std::string_view place, State& state) {
    for (auto it = state.moved.begin(); it != state.moved.end();) {
        if (*it == place || is_strict_child(place, *it))
            it = state.moved.erase(it);
        else
            ++it;
    }
}

void BorrowChecker::kill_borrower(std::string_view borrower, State& state) {
    state.borrows.erase(
        std::remove_if(state.borrows.begin(),
                       state.borrows.end(),
                       [&](const Borrow& loan) { return loan.borrower == borrower; }),
        state.borrows.end());
}

void BorrowChecker::kill_borrowers_under(std::string_view borrower, State& state) {
    state.borrows.erase(std::remove_if(state.borrows.begin(),
                                       state.borrows.end(),
                                       [&](const Borrow& loan) {
                                           return loan.borrower == borrower ||
                                                  is_strict_child(borrower, loan.borrower);
                                       }),
                        state.borrows.end());
}
} // namespace uinx
