// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/borrow.hpp"

#include <algorithm>
#include <sstream>

namespace uinx {
namespace {
constexpr std::size_t kMaxDataflowIterations = 4096;

std::string root_of(std::string_view place) {
    const auto dot = place.find('.');
    return std::string(place.substr(0, dot));
}

bool is_strict_child(std::string_view parent, std::string_view child) {
    return child.size() > parent.size() && child.substr(0, parent.size()) == parent &&
           child[parent.size()] == '.';
}

std::string symbol_key(std::string_view name, hir::SymbolId id) {
    return std::string(name) + "#" + std::to_string(id);
}

std::string display_place(std::string_view place) {
    if (place.starts_with("<external:"))
        return std::string(place);
    const auto dot = place.find('.');
    const std::string_view root = place.substr(0, dot);
    const auto hash = root.rfind('#');
    std::string shown = hash == std::string_view::npos ? std::string(root)
                                                        : std::string(root.substr(0, hash));
    if (dot != std::string_view::npos)
        shown += std::string(place.substr(dot));
    return shown;
}
} // namespace

void BorrowChecker::report_error(const SourceRange& range, std::string code, std::string message) {
    std::ostringstream key;
    key << code << '@' << range.begin.line << ':' << range.begin.column << '-' << range.end.line
        << ':' << range.end.column << ':' << message;
    if (emitted_errors_.insert(key.str()).second)
        diags_.error(range, std::move(code), std::move(message));
}

bool BorrowChecker::overlaps(std::string_view a, std::string_view b) const {
    return a == b || is_strict_child(a, b) || is_strict_child(b, a);
}

bool BorrowChecker::same_origin(const RefOrigin& a, const RefOrigin& b) const {
    return a.place == b.place && a.mut == b.mut && a.range.begin.line == b.range.begin.line &&
           a.range.begin.column == b.range.begin.column;
}

bool BorrowChecker::same_borrow(const Borrow& a, const Borrow& b) const {
    return a.place == b.place && a.borrower == b.borrower && a.mut == b.mut &&
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
            if (symbol.kind == hir::SymbolKind::Local ||
                symbol.kind == hir::SymbolKind::Parameter)
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

std::vector<BorrowChecker::RefOrigin>
BorrowChecker::reference_origins(const ast::Expr& expr, const State& state) const {
    std::vector<RefOrigin> result;
    auto append = [&](std::vector<RefOrigin> more) {
        for (auto& origin : more) {
            const bool duplicate = std::any_of(result.begin(), result.end(), [&](const RefOrigin& x) {
                return same_origin(x, origin);
            });
            if (!duplicate)
                result.push_back(std::move(origin));
        }
    };

    if (expr.kind == ast::ExprKind::Borrow) {
        const auto& borrow = static_cast<const ast::BorrowExpr&>(expr);
        if (auto place = place_of(*borrow.target)) {
            result.push_back({*place, expr.range, borrow.mut});
            return result;
        }
        result = reference_origins(*borrow.target, state);
        for (auto& origin : result)
            origin.mut = borrow.mut;
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
                        const auto method = model_.functions.find(owner.name + "::" + member.member);
                        if (method != model_.functions.end() && !method->second.params.empty() &&
                            method->second.params.front().kind == TypeKind::Ref) {
                            if (receiver_type.kind == TypeKind::Ref) {
                                // A reference parameter can point outside the current frame.  Keep
                                // that provenance external inside this function; the caller maps
                                // it back to its concrete argument when the call itself is bound.
                                result.push_back({"<external:" + *receiver_place + ">",
                                                  expr.range,
                                                  method->second.params.front().mut});
                            } else {
                                result.push_back({*receiver_place,
                                                  expr.range,
                                                  method->second.params.front().mut});
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
    for (std::size_t i = 0;
         i < structure->second.generic_names.size() && i < type.args.size();
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
            if (type.kind == TypeKind::Named &&
                (type.name == "Slice" || type.name == "SliceMut") && !type.args.empty()) {
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
            if (std::none_of(found->second.begin(), found->second.end(), [&](const RefOrigin& other) {
                    return same_origin(origin, other);
                })) {
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
    state.borrows.erase(
        std::remove_if(state.borrows.begin(),
                       state.borrows.end(),
                       [&](const Borrow& loan) {
                           return loan.borrower == borrower ||
                                  is_strict_child(borrower, loan.borrower);
                       }),
        state.borrows.end());
}

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
                           return std::any_of(origins.begin(), origins.end(), [&](const RefOrigin& o) {
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
        report_error(range,
                     "E0400",
                     "use of moved value/place '" + display_place(place) + "'");
        return;
    }

    if (access == Access::Read) {
        for (const auto& loan : state.borrows) {
            if (overlaps(place, loan.place) && loan.mut) {
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
            if (overlaps(place, loan.place) && loan.mut) {
                report_error(range,
                             "E0401",
                             "cannot immutably borrow '" + display_place(place) +
                                 "' while mutable borrow of '" + display_place(loan.place) +
                                 "' is active");
            }
        }
        return;
    }

    if (access == Access::BorrowMut) {
        for (const auto& loan : state.borrows) {
            if (overlaps(place, loan.place)) {
                report_error(range,
                             "E0402",
                             "cannot mutably borrow '" + display_place(place) +
                                 "' while borrow of '" + display_place(loan.place) +
                                 "' is active");
            }
        }
        return;
    }

    if (access == Access::Write || access == Access::Move) {
        for (const auto& loan : state.borrows) {
            if (overlaps(place, loan.place)) {
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
    const Borrow loan{place, std::move(borrower), mut, range};
    if (std::none_of(state.borrows.begin(), state.borrows.end(), [&](const Borrow& existing) {
            return same_borrow(existing, loan);
        })) {
        state.borrows.push_back(loan);
    }
}

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
        const LiveSet right = branch->else_block
                                  ? liveness_block(*branch->else_block,
                                                   live_after,
                                                   break_live,
                                                   continue_live)
                                  : live_after;
        before = left;
        before.insert(right.begin(), right.end());
        add(expression_uses(*branch->condition));
    } else if (const auto* while_stmt = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
        LiveSet header = live_after;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            const LiveSet body =
                liveness_block(*while_stmt->body, header, live_after, header);
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
    if (!fn.body)
        return;
    const LiveSet empty;
    (void)liveness_block(*fn.body, empty, empty, empty);
}

void BorrowChecker::expire_dead_loans(const ast::Stmt& stmt, State& state) {
    const auto live = live_before_.find(&stmt);
    state.borrows.erase(
        std::remove_if(state.borrows.begin(),
                       state.borrows.end(),
                       [&](const Borrow& loan) {
                           if (loan.borrower == "<temporary>")
                               return true;
                           const std::string borrower_root = root_of(loan.borrower);
                           if (model_.globals.contains(borrower_root))
                               return false;
                           return live == live_before_.end() ||
                                  !live->second.contains(borrower_root);
                       }),
        state.borrows.end());
}

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
                    report_error(
                        origin.range,
                        "E0405",
                        "reference stored in '" + display_place(borrower) +
                            "' outlives local place '" + display_place(origin.place) + "'");
                }
            }
        }

        state.scopes.pop_back();
        state.borrows.erase(
            std::remove_if(state.borrows.begin(),
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
                        begin_borrow(origin.place,
                                     "<temporary>",
                                     borrow.mut,
                                     expr.range,
                                     state);
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
                }
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
                                     "safe reference to stack-owned place '" + display_place(loan.place) +
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

void BorrowChecker::check_stmt(const ast::Stmt& stmt, State& state) {
    if (!state.reachable)
        return;
    expire_dead_loans(stmt, state);

    if (const auto* let = dynamic_cast<const ast::LetStmt*>(&stmt)) {
        const std::string key = binding_key(*let);
        Type type{};
        if (const auto found = model_.binding_types.find(let); found != model_.binding_types.end())
            type = found->second;

        std::vector<RefOrigin> origins;
        if (let->init)
            origins = reference_origins(*let->init, state);
        if (let->init)
            inspect_expr(*let->init, state, Access::Move);

        state.scopes.back()[key] = type;
        mark_initialized(key, state);

        std::unordered_set<std::string> visiting;
        if (contains_reference(type, visiting) && !origins.empty()) {
            set_reference_origins(key, {}, state);
            bind_reference_expression(key, *let->init, state);
        } else {
            set_reference_origins(key, {}, state);
        }
        return;
    }

    if (const auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
        const auto target = place_of(*assign->target);
        const Type target_type = target ? type_of_place(*target, state) : Type{};
        std::vector<RefOrigin> origins = reference_origins(*assign->value, state);

        if (assign->op != "=")
            inspect_expr(*assign->target, state, Access::Read);
        if (target)
            kill_borrowers_under(*target, state);

        inspect_expr(*assign->value, state, Access::Move);
        if (target) {
            access_place(*target, assign->target->range, Access::Write, state);
            mark_initialized(*target, state);
            std::unordered_set<std::string> visiting;
            if (contains_reference(target_type, visiting) && !origins.empty()) {
                set_reference_origins(*target, {}, state);
                bind_reference_expression(*target, *assign->value, state);
            } else {
                set_reference_origins(*target, {}, state);
            }
        }
        return;
    }

    if (const auto* expression = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
        inspect_expr(*expression->expr, state, Access::Move);
        return;
    }

    if (const auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
        if (ret->value) {
            bool reference_bearing = false;
            if (const auto type = model_.expr_types.find(ret->value.get());
                type != model_.expr_types.end()) {
                std::unordered_set<std::string> visiting;
                reference_bearing = contains_reference(type->second, visiting);
            }
            if (reference_bearing) {
                for (const auto& origin : reference_origins(*ret->value, state)) {
                    if (is_stack_owned(origin.place, state)) {
                        report_error(ret->value->range,
                                     "E0404",
                                     "reference to stack-owned place '" + display_place(origin.place) +
                                         "' escapes the function");
                    }
                }
            }
            inspect_expr(*ret->value, state, Access::Move);
        }
        state.reachable = false;
        return;
    }

    if (dynamic_cast<const ast::BreakStmt*>(&stmt)) {
        if (!loop_stack_.empty()) {
            State exit = state;
            truncate_scopes(exit, loop_stack_.back()->outer_scope_depth);
            loop_stack_.back()->breaks.push_back(std::move(exit));
        }
        state.reachable = false;
        return;
    }

    if (dynamic_cast<const ast::ContinueStmt*>(&stmt)) {
        if (!loop_stack_.empty()) {
            State exit = state;
            truncate_scopes(exit, loop_stack_.back()->outer_scope_depth);
            loop_stack_.back()->continues.push_back(std::move(exit));
        }
        state.reachable = false;
        return;
    }

    if (const auto* block = dynamic_cast<const ast::BlockStmt*>(&stmt)) {
        check_block(*block, state);
        return;
    }

    if (const auto* branch = dynamic_cast<const ast::IfStmt*>(&stmt)) {
        inspect_expr(*branch->condition, state, Access::Move);
        State left = state;
        check_block(*branch->then_block, left);
        State right = state;
        if (branch->else_block)
            check_block(*branch->else_block, right);
        merge_states(state, left, right);
        return;
    }

    if (const auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
        const State entry = state;
        State header = entry;
        State condition_exit = entry;
        std::optional<State> break_exit;

        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            State iter_state = header;
            inspect_expr(*loop->condition, iter_state, Access::Move);
            condition_exit = iter_state;

            LoopFrame frame{entry.scopes.size(), {}, {}};
            loop_stack_.push_back(&frame);
            State body = iter_state;
            check_block(*loop->body, body);
            loop_stack_.pop_back();

            for (const auto& exit : frame.breaks)
                merge_into(break_exit, exit);

            std::optional<State> backedge;
            merge_into(backedge, body);
            for (const auto& next : frame.continues)
                merge_into(backedge, next);

            State next_header = entry;
            if (backedge) {
                State merged;
                merge_states(merged, entry, *backedge);
                next_header = std::move(merged);
            }
            if (states_equivalent(next_header, header)) {
                header = std::move(next_header);
                converged = true;
                break;
            }
            header = std::move(next_header);
        }

        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow state analysis did not converge; compilation stopped "
                         "conservatively");

        state = condition_exit;
        if (break_exit) {
            State merged;
            merge_states(merged, state, *break_exit);
            state = std::move(merged);
        }
        return;
    }

    if (const auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
        inspect_expr(*loop->begin, state, Access::Move);
        inspect_expr(*loop->end, state, Access::Move);
        const State entry = state;
        State header = entry;
        std::optional<State> break_exit;
        Type iterator_type{};
        if (const auto type = model_.expr_types.find(loop->begin.get());
            type != model_.expr_types.end()) {
            iterator_type = type->second;
        }

        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            LoopFrame frame{entry.scopes.size(), {}, {}};
            loop_stack_.push_back(&frame);
            State body = header;
            body.scopes.emplace_back();
            body.scopes.back()[for_binding_key(*loop)] = iterator_type;
            check_block(*loop->body, body);
            truncate_scopes(body, entry.scopes.size());
            loop_stack_.pop_back();

            for (const auto& exit : frame.breaks)
                merge_into(break_exit, exit);

            std::optional<State> backedge;
            merge_into(backedge, body);
            for (const auto& next : frame.continues)
                merge_into(backedge, next);

            State next_header = entry;
            if (backedge) {
                State merged;
                merge_states(merged, entry, *backedge);
                next_header = std::move(merged);
            }
            if (states_equivalent(next_header, header)) {
                header = std::move(next_header);
                converged = true;
                break;
            }
            header = std::move(next_header);
        }

        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow state analysis did not converge; compilation stopped "
                         "conservatively");

        state = header;
        if (break_exit) {
            State merged;
            merge_states(merged, state, *break_exit);
            state = std::move(merged);
        }
        return;
    }

    if (const auto* loop = dynamic_cast<const ast::LoopStmt*>(&stmt)) {
        const State entry = state;
        State header = entry;
        std::optional<State> break_exit;

        bool converged = false;
        for (std::size_t iteration = 0; iteration < kMaxDataflowIterations; ++iteration) {
            LoopFrame frame{entry.scopes.size(), {}, {}};
            loop_stack_.push_back(&frame);
            State body = header;
            check_block(*loop->body, body);
            loop_stack_.pop_back();

            for (const auto& exit : frame.breaks)
                merge_into(break_exit, exit);

            std::optional<State> backedge;
            merge_into(backedge, body);
            for (const auto& next : frame.continues)
                merge_into(backedge, next);
            if (!backedge) {
                converged = true;
                break;
            }

            State next_header;
            merge_states(next_header, entry, *backedge);
            if (states_equivalent(next_header, header)) {
                header = std::move(next_header);
                converged = true;
                break;
            }
            header = std::move(next_header);
        }

        if (!converged)
            report_error(stmt.range,
                         "E0408",
                         "borrow state analysis did not converge; compilation stopped "
                         "conservatively");

        if (break_exit) {
            state = *break_exit;
        } else {
            state = entry;
            state.reachable = false;
        }
        return;
    }

    if (const auto* unsafe = dynamic_cast<const ast::UnsafeStmt*>(&stmt)) {
        check_block(*unsafe->body, state);
        return;
    }
}

void BorrowChecker::check_block(const ast::BlockStmt& block, State& state) {
    if (!state.reachable)
        return;
    const std::size_t outer_depth = state.scopes.size();
    state.scopes.emplace_back();
    for (const auto& stmt : block.stmts) {
        if (!state.reachable)
            break;
        check_stmt(*stmt, state);
    }
    truncate_scopes(state, outer_depth);
}

void BorrowChecker::check_function(const ast::FunctionDecl& fn, const FunctionSig* signature) {
    if (!fn.body)
        return;
    prepare_liveness(fn);
    copy_generics_.clear();
    if (signature) {
        for (const auto& [generic, bounds] : signature->bounds)
            if (std::find(bounds.begin(), bounds.end(), "Copy") != bounds.end())
                copy_generics_.insert(generic);
    }
    State state;
    state.async_context = fn.is_async;
    state.scopes.emplace_back();
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        const Type type = signature && i < signature->params.size()
                              ? signature->params[i]
                              : type_from_ast(fn.params[i].type);
        state.scopes.back()[parameter_key(fn.params[i])] = type;
    }
    check_block(*fn.body, state);
    loop_stack_.clear();
    copy_generics_.clear();
}

void BorrowChecker::check(const ast::Module& module) {
    emitted_errors_.clear();
    for (const auto& item : module.items) {
        if (const auto* function = std::get_if<ast::FunctionDecl>(&item)) {
            const auto signature = model_.functions.find(function->name);
            check_function(*function,
                           signature == model_.functions.end() ? nullptr : &signature->second);
        } else if (const auto* implementation = std::get_if<ast::ImplDecl>(&item)) {
            const Type owner = type_from_ast(implementation->for_type);
            for (const auto& method : implementation->methods) {
                const auto signature = model_.functions.find(owner.name + "::" + method.name);
                check_function(method,
                               signature == model_.functions.end() ? nullptr : &signature->second);
            }
        }
    }
}
} // namespace uinx
