// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/borrow.hpp"

#include "borrow_internal.hpp"

#include <algorithm>
#include <sstream>

namespace uinx {
using namespace borrow_detail;

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
        if (auto binding = model_.hir.assignment_bindings.find(assign);
            binding != model_.hir.assignment_bindings.end()) {
            const auto& name = static_cast<const ast::NameExpr&>(*assign->target).name;
            const std::string key = symbol_key(name, binding->second);
            Type type{};
            if (auto found = model_.assignment_binding_types.find(assign);
                found != model_.assignment_binding_types.end())
                type = found->second;

            std::vector<RefOrigin> origins = reference_origins(*assign->value, state);
            inspect_expr(*assign->value, state, Access::Move);
            state.scopes.back()[key] = type;
            mark_initialized(key, state);
            std::unordered_set<std::string> visiting;
            if (contains_reference(type, visiting) && !origins.empty()) {
                set_reference_origins(key, {}, state);
                bind_reference_expression(key, *assign->value, state);
            } else {
                set_reference_origins(key, {}, state);
            }
            return;
        }
        const auto target = place_of(*assign->target);
        const Type target_type = target ? type_of_place(*target, state) : Type{};
        std::vector<RefOrigin> origins = reference_origins(*assign->value, state);

        if (assign->op != "=" && target) {
            // Two-phase compound assignment (`x += x`): reserve target as
            // shared-like, inspect RHS, then perform the write. This mirrors
            // rustc's overloaded `AddAssign::add_assign(&mut x, x)` handling
            // where the LHS reservation coexists with RHS reads.
            const std::string rid = "<twophase:assign:" + std::to_string(assign->range.begin.line) +
                                    ":" + std::to_string(assign->range.begin.column) + ">";
            begin_two_phase_reservation(*target, rid, assign->target->range, state);
            inspect_expr(*assign->value, state, Access::Move);
            activate_two_phase(rid, assign->range, state);
            // Consume the reservation before the write so the assignment does
            // not conflict with itself; other overlapping live loans were
            // already checked at reservation/activation.
            kill_borrower(rid, state);
            // Write kills borrowers under target except the just-consumed
            // reservation (already removed).
            if (target)
                kill_borrowers_under(*target, state);
            // Re-establish the activated loan after kill (kill removes it
            // because borrower is under target? No: borrower is `<twophase:>`
            // root, not under target, so it survives. Just proceed to write.
            access_place(*target, assign->target->range, Access::Write, state);
            mark_initialized(*target, state);
            std::unordered_set<std::string> visiting2;
            if (contains_reference(target_type, visiting2) && !origins.empty()) {
                set_reference_origins(*target, {}, state);
                bind_reference_expression(*target, *assign->value, state);
            } else {
                set_reference_origins(*target, {}, state);
            }
            return;
        }
        if (assign->op != "=")
            inspect_expr(*assign->target, state, Access::Read);
        if (target)
            kill_borrowers_under(*target, state);

        inspect_expr(*assign->value, state, Access::Move);
        if (!target)
            write_through_reference(*assign->target, state);
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
                                     "reference to stack-owned place '" +
                                         display_place(origin.place) + "' escapes the function");
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
