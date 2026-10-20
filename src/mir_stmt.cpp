// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "mir_internal.hpp"
#include "uinx/mir.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace uinx {
using namespace mir_detail;

bool MIRLowerer::place_is_atomic(const ast::Expr& e, const FnState& s) const {
    const Type ty = expr_type(e, s);
    // Bool excluded: LLVM i1 atomics are invalid (byte-sized required).
    // Use AtomicBool (u8-backed) instead.
    const bool compatible = ty.is_integer() || ty.kind == TypeKind::RawPtr;
    if (!compatible)
        return false;
    (void)s;
    // Explicit-shared only: smp auto no longer promotes. Only `shared`
    // globals/fields lower to LLVM atomics. percpu is never atomic.
    if (e.kind == ast::ExprKind::Name) {
        const auto& n = static_cast<const ast::NameExpr&>(e);
        auto g = model_.globals.find(n.name);
        if (g == model_.globals.end() || g->second.is_percpu)
            return false;
        return g->second.is_shared;
    }
    if (e.kind == ast::ExprKind::Member) {
        const auto& m = static_cast<const ast::MemberExpr&>(e);
        Type base = expr_type(*m.base, s);
        if (base.kind == TypeKind::Ref && base.pointee)
            base = *base.pointee;
        auto it = model_.structs.find(base.name);
        return it != model_.structs.end() && it->second.shared_fields.contains(m.member);
    }
    return false;
}
std::string MIRLowerer::load_order(const ast::Expr& e, const FnState& s) const {
    (void)e;
    return s.smp_mode == ast::SmpMode::Strict ? "seq_cst" : "acquire";
}
std::string MIRLowerer::store_order(const ast::Expr& e, const FnState& s) const {
    (void)e;
    return s.smp_mode == ast::SmpMode::Strict ? "seq_cst" : "release";
}
std::string MIRLowerer::rmw_order(const ast::Expr& e, const FnState& s) const {
    (void)e;
    return s.smp_mode == ast::SmpMode::Strict ? "seq_cst" : "acq_rel";
}

void MIRLowerer::lower_stmt(const ast::Stmt& st, FnState& s) {
    if (auto l = dynamic_cast<const ast::LetStmt*>(&st)) {
        auto [v, init_ty] = lower_expr(*l->init, s);
        Type t = init_ty;
        if (auto it = model_.binding_types.find(l); it != model_.binding_types.end())
            t = subst_type(it->second, s);
        v = coerce_value(s, v, init_ty, t, l->range);
        std::string sl = slot(s, l->name);
        s.slots[l->name] = sl;
        s.local_types[l->name] = t;
        s.scope_locals.back().push_back(l->name);
        emit(s, {mir::Op::Alloca, sl, t, {}, "", {}, false, l->range});
        emit(s, {mir::Op::Store, "", t, {v, sl}, "", {}, false, l->range});
        return;
    }
    if (auto a = dynamic_cast<const ast::AssignStmt*>(&st)) {
        if (auto binding = model_.hir.assignment_bindings.find(a);
            binding != model_.hir.assignment_bindings.end()) {
            auto [v, init_ty] = lower_expr(*a->value, s);
            Type t = init_ty;
            if (auto it = model_.assignment_binding_types.find(a);
                it != model_.assignment_binding_types.end())
                t = subst_type(it->second, s);
            v = coerce_value(s, v, init_ty, t, a->range);
            const auto& name = static_cast<const ast::NameExpr&>(*a->target).name;
            std::string sl = slot(s, name);
            s.slots[name] = sl;
            s.local_types[name] = t;
            s.scope_locals.back().push_back(name);
            emit(s, {mir::Op::Alloca, sl, t, {}, "", {}, false, a->range});
            emit(s, {mir::Op::Store, "", t, {v, sl}, "", {}, false, a->range});
            return;
        }
        std::string addr = lower_place_address(*a->target, s);
        Type target_ty = expr_type(*a->target, s);
        auto [v, vt] = lower_expr(*a->value, s);
        v = coerce_value(s, v, vt, target_ty, a->range);
        const bool atomic = place_is_atomic(*a->target, s);
        if (atomic && a->op != "=") {
            const std::string op = a->op.substr(0, a->op.size() - 1);
            if (op == "+" || op == "-" || op == "&" || op == "|" || op == "^") {
                emit(s,
                     {mir::Op::AtomicRmw,
                      "",
                      target_ty,
                      {addr, v, rmw_order(*a->target, s)},
                      op,
                      {},
                      true,
                      a->range});
                return;
            }
            diags_.error(a->range,
                         "E0509",
                         "atomic compound assignment '" + a->op +
                             "' is not supported; use an explicit compare/exchange loop");
            return;
        }
        if (a->op != "=") {
            std::string old = temp(s);
            emit(s, {mir::Op::Load, old, target_ty, {addr}, "", {}, false, a->range});
            std::string combined = temp(s);
            std::string op = a->op.substr(0, a->op.size() - 1);
            emit(s, {mir::Op::Binary, combined, target_ty, {old, v}, op, {}, false, a->range});
            v = combined;
        }
        emit(s,
             {mir::Op::Store,
              "",
              target_ty,
              {v, addr},
              atomic ? store_order(*a->target, s) : "",
              {},
              atomic,
              a->range});
        return;
    }
    if (auto e = dynamic_cast<const ast::ExprStmt*>(&st)) {
        (void)lower_expr(*e->expr, s);
        return;
    }
    if (auto r = dynamic_cast<const ast::ReturnStmt*>(&st)) {
        emit_scope_drops(s);
        if (r->value) {
            auto [v, t] = lower_expr(*r->value, s);
            v = coerce_value(s, v, t, s.fn.result, r->range);
            emit(s, {mir::Op::Return, "", s.fn.result, {v}, "", {}, false, r->range});
        } else
            emit(s, {mir::Op::Return, "", Type::builtin("unit"), {}, "", {}, false, r->range});
        return;
    }
    if (auto f = dynamic_cast<const ast::FenceStmt*>(&st)) {
        emit(s,
             {f->compiler_only ? mir::Op::CompilerFence : mir::Op::Fence,
              "",
              Type::builtin("unit"),
              {},
              f->order,
              {},
              false,
              f->range});
        return;
    }
    if (auto b = dynamic_cast<const ast::BlockStmt*>(&st)) {
        lower_block(*b, s);
        return;
    }
    if (auto u = dynamic_cast<const ast::UnsafeStmt*>(&st)) {
        lower_block(*u->body, s);
        return;
    }
    if (auto i = dynamic_cast<const ast::IfStmt*>(&st)) {
        auto [c, ct] = lower_expr(*i->condition, s);
        (void)ct;
        std::size_t then_i = new_block(s, "if.then." + std::to_string(s.fn.blocks.size()));
        std::size_t else_i = new_block(s, "if.else." + std::to_string(s.fn.blocks.size()));
        std::size_t merge_i = new_block(s, "if.end." + std::to_string(s.fn.blocks.size()));
        std::string then_l = s.fn.blocks[then_i].label, else_l = s.fn.blocks[else_i].label,
                    merge_l = s.fn.blocks[merge_i].label;
        emit(s,
             {mir::Op::CondJump,
              "",
              Type::builtin("bool"),
              {c, then_l, else_l},
              "",
              {},
              false,
              i->range});
        s.block = then_i;
        lower_block(*i->then_block, s);
        if (!block(s).terminated)
            emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {merge_l}, "", {}, false, i->range});
        s.block = else_i;
        if (i->else_block)
            lower_block(*i->else_block, s);
        if (!block(s).terminated)
            emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {merge_l}, "", {}, false, i->range});
        s.block = merge_i;
        return;
    }
    if (auto w = dynamic_cast<const ast::WhileStmt*>(&st)) {
        std::size_t cond_i = new_block(s, "while.cond." + std::to_string(s.fn.blocks.size()));
        std::size_t body_i = new_block(s, "while.body." + std::to_string(s.fn.blocks.size()));
        std::size_t end_i = new_block(s, "while.end." + std::to_string(s.fn.blocks.size()));
        std::string cl = s.fn.blocks[cond_i].label, bl = s.fn.blocks[body_i].label,
                    el = s.fn.blocks[end_i].label;
        emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {cl}, "", {}, false, w->range});
        s.block = cond_i;
        auto [c, t] = lower_expr(*w->condition, s);
        (void)t;
        emit(s,
             {mir::Op::CondJump, "", Type::builtin("bool"), {c, bl, el}, "", {}, false, w->range});
        s.block = body_i;
        s.loop_targets.push_back({cond_i, end_i});
        lower_block(*w->body, s);
        s.loop_targets.pop_back();
        if (!block(s).terminated)
            emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {cl}, "", {}, false, w->range});
        s.block = end_i;
        return;
    }
    if (auto f = dynamic_cast<const ast::ForStmt*>(&st)) {
        auto [begin_value, iter_ty] = lower_expr(*f->begin, s);
        auto [end_value, end_ty] = lower_expr(*f->end, s);
        (void)end_ty;
        std::string iter_slot = slot(s, f->name);
        s.slots[f->name] = iter_slot;
        s.local_types[f->name] = iter_ty;
        s.scope_locals.back().push_back(f->name);
        emit(s, {mir::Op::Alloca, iter_slot, iter_ty, {}, "", {}, false, f->range});
        emit(s, {mir::Op::Store, "", iter_ty, {begin_value, iter_slot}, "", {}, false, f->range});
        std::size_t cond_i = new_block(s, "for.cond." + std::to_string(s.fn.blocks.size()));
        std::size_t body_i = new_block(s, "for.body." + std::to_string(s.fn.blocks.size()));
        std::size_t step_i = new_block(s, "for.step." + std::to_string(s.fn.blocks.size()));
        std::size_t end_i = new_block(s, "for.end." + std::to_string(s.fn.blocks.size()));
        const std::string cl = s.fn.blocks[cond_i].label, bl = s.fn.blocks[body_i].label,
                          sl = s.fn.blocks[step_i].label, el = s.fn.blocks[end_i].label;
        emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {cl}, "", {}, false, f->range});
        s.block = cond_i;
        std::string current = temp(s);
        emit(s, {mir::Op::Load, current, iter_ty, {iter_slot}, "", {}, false, f->range});
        std::string cond = temp(s);
        emit(s,
             {mir::Op::Compare,
              cond,
              iter_ty,
              {current, end_value},
              f->inclusive ? "<=" : "<",
              {},
              false,
              f->range});
        emit(s,
             {mir::Op::CondJump,
              "",
              Type::builtin("bool"),
              {cond, bl, el},
              "",
              {},
              false,
              f->range});
        s.block = body_i;
        s.loop_targets.push_back({step_i, end_i});
        lower_block(*f->body, s);
        s.loop_targets.pop_back();
        if (!block(s).terminated)
            emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {sl}, "", {}, false, f->range});
        s.block = step_i;
        std::string old = temp(s);
        emit(s, {mir::Op::Load, old, iter_ty, {iter_slot}, "", {}, false, f->range});
        std::string one = temp(s);
        emit(s, {mir::Op::ConstInt, one, iter_ty, {}, "1", {}, false, f->range});
        std::string next = temp(s);
        emit(s, {mir::Op::Binary, next, iter_ty, {old, one}, "+", {}, false, f->range});
        emit(s, {mir::Op::Store, "", iter_ty, {next, iter_slot}, "", {}, false, f->range});
        emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {cl}, "", {}, false, f->range});
        s.block = end_i;
        return;
    }
    if (auto l = dynamic_cast<const ast::LoopStmt*>(&st)) {
        std::size_t body_i = new_block(s, "loop.body." + std::to_string(s.fn.blocks.size()));
        std::size_t end_i = new_block(s, "loop.end." + std::to_string(s.fn.blocks.size()));
        const std::string bl = s.fn.blocks[body_i].label, el = s.fn.blocks[end_i].label;
        emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {bl}, "", {}, false, l->range});
        s.block = body_i;
        s.loop_targets.push_back({body_i, end_i});
        lower_block(*l->body, s);
        s.loop_targets.pop_back();
        if (!block(s).terminated)
            emit(s, {mir::Op::Jump, "", Type::builtin("unit"), {bl}, "", {}, false, l->range});
        s.block = end_i;
        return;
    }
    if (dynamic_cast<const ast::BreakStmt*>(&st)) {
        if (!s.loop_targets.empty()) {
            emit_scope_drops(s);
            emit(s,
                 {mir::Op::Jump,
                  "",
                  Type::builtin("unit"),
                  {s.fn.blocks[s.loop_targets.back().second].label},
                  "",
                  {},
                  false,
                  st.range});
        }
        return;
    }
    if (dynamic_cast<const ast::ContinueStmt*>(&st)) {
        if (!s.loop_targets.empty()) {
            emit_scope_drops(s);
            emit(s,
                 {mir::Op::Jump,
                  "",
                  Type::builtin("unit"),
                  {s.fn.blocks[s.loop_targets.back().first].label},
                  "",
                  {},
                  false,
                  st.range});
        }
        return;
    }
}
} // namespace uinx
