// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/mir.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace uinx {
namespace {
bool unsigned_integer_kind(TypeKind kind) {
    return kind == TypeKind::U8 || kind == TypeKind::U16 || kind == TypeKind::U32 ||
           kind == TypeKind::U64 || kind == TypeKind::U128 || kind == TypeKind::Usize;
}
} // namespace
std::string MIRLowerer::temp(FnState& s) {
    return "%t" + std::to_string(s.temp_counter++);
}
std::string MIRLowerer::slot(FnState& s, std::string_view h) {
    return "%" + std::string(h) + ".slot." + std::to_string(s.slot_counter++);
}
mir::BasicBlock& MIRLowerer::block(FnState& s) {
    return s.fn.blocks[s.block];
}
std::size_t MIRLowerer::new_block(FnState& s, std::string label) {
    s.fn.blocks.push_back({std::move(label), {}, false});
    return s.fn.blocks.size() - 1;
}
void MIRLowerer::emit(FnState& s, mir::Instruction i) {
    if (block(s).terminated) {
        diags_.warning(i.range, "W0500", "unreachable statement after terminator");
        return;
    }
    if (i.op == mir::Op::Return || i.op == mir::Op::Jump || i.op == mir::Op::CondJump)
        block(s).terminated = true;
    block(s).instructions.push_back(std::move(i));
}
Type MIRLowerer::subst_type(const Type& t, const FnState& s) const {
    return substitute_type(t, s.subst);
}
Type MIRLowerer::expr_type(const ast::Expr& e, const FnState& s) const {
    auto it = model_.expr_types.find(&e);
    return it == model_.expr_types.end() ? Type{} : subst_type(it->second, s);
}
std::string MIRLowerer::intern_string(std::string v) {
    for (const auto& s : out_.strings)
        if (s.value == v)
            return s.symbol;
    std::string sym = ".str." + std::to_string(out_.strings.size());
    out_.strings.push_back({sym, std::move(v)});
    return sym;
}
mir::Module MIRLowerer::lower(const ast::Module& module) {
    out_ = {};
    pending_.clear();
    emitted_.clear();
    smp_mode_ = module.smp_mode;
    for (const auto& i : module.items)
        if (auto st = std::get_if<ast::StructDecl>(&i))
            out_.structs.push_back(st);
    for (const auto& i : module.items)
        if (auto g = std::get_if<ast::GlobalDecl>(&i)) {
            std::string init = "0";
            if (g->init) {
                switch (g->init->kind) {
                    case ast::ExprKind::Integer:
                        init = static_cast<const ast::IntegerExpr&>(*g->init).value;
                        break;
                    case ast::ExprKind::Float:
                        init = static_cast<const ast::FloatExpr&>(*g->init).value;
                        break;
                    case ast::ExprKind::Bool:
                        init = static_cast<const ast::BoolExpr&>(*g->init).value ? "1" : "0";
                        break;
                    case ast::ExprKind::Char: {
                        const auto& v = static_cast<const ast::CharExpr&>(*g->init).value;
                        init = std::to_string(v.empty() ? 0 : static_cast<unsigned char>(v[0]));
                        break;
                    }
                    case ast::ExprKind::StructLiteral:
                        init = "zeroinitializer";
                        break;
                    default:
                        diags_.error(
                            g->range, "E0508", "unsupported non-literal global initializer");
                        break;
                }
            }
            out_.globals.push_back({g->name,
                                    type_from_ast(g->type),
                                    std::move(init),
                                    g->is_mut,
                                    g->is_shared,
                                    g->is_percpu});
        }
    for (const auto& i : module.items)
        if (auto f = std::get_if<ast::FunctionDecl>(&i)) {
            if (f->is_extern) {
                lower_function(*f, f->name);
                continue;
            }
            if (f->generics.empty())
                lower_function(*f, f->name);
        }
    for (const auto& i : module.items)
        if (auto im = std::get_if<ast::ImplDecl>(&i)) {
            const Type owner = type_from_ast(im->for_type);
            for (const auto& m : im->methods)
                if (m.generics.empty())
                    lower_function(m, owner.name + "__" + m.name, {{"Self", owner}});
        }
    while (!pending_.empty()) {
        auto sp = std::move(pending_.back());
        pending_.pop_back();
        if (!emitted_.contains(sp.name))
            lower_function(*sp.decl, sp.name, sp.subst);
    }
    return std::move(out_);
}
void MIRLowerer::lower_function(const ast::FunctionDecl& d,
                                const std::string& emitted,
                                const std::unordered_map<std::string, Type>& subst) {
    if (emitted_.contains(emitted))
        return;
    emitted_.insert(emitted);
    FnState s;
    s.fn.name = emitted;
    s.fn.source_name = d.name;
    s.fn.is_extern = d.is_extern;
    s.fn.is_unsafe = d.is_unsafe;
    s.fn.is_async = d.is_async;
    s.fn.abi = d.abi;
    s.subst = subst;
    s.concurrent = d.is_concurrent;
    for (const auto& [key, sig] : model_.functions) {
        (void)key;
        if (sig.decl == &d && sig.is_concurrent) {
            s.concurrent = true;
            break;
        }
    }
    s.smp_mode = smp_mode_;
    std::unordered_set<std::string> gs;
    for (auto& g : d.generics)
        gs.insert(g.name);
    if (subst.contains("Self"))
        gs.insert("Self");
    s.fn.result = substitute_type(type_from_ast(d.return_type, gs), subst);
    for (const auto& p : d.params) {
        Type pt = substitute_type(type_from_ast(p.type, gs), subst);
        s.fn.params.push_back({p.name, pt});
        s.local_types[p.name] = pt;
    }
    if (d.is_extern || !d.body) {
        out_.functions.push_back(std::move(s.fn));
        return;
    }
    s.fn.blocks.push_back({"entry", {}, false});
    s.scope_locals.emplace_back();
    for (const auto& p : s.fn.params) {
        std::string sl = slot(s, p.name);
        s.slots[p.name] = sl;
        emit(s, {mir::Op::Alloca, sl, p.type, {}, "", {}, false, d.range});
        emit(s, {mir::Op::Store, "", p.type, {"%arg." + p.name, sl}, "", {}, false, d.range});
    }
    lower_block(*d.body, s);
    if (!block(s).terminated) {
        emit_scope_drops(s);
        if (s.fn.result.is_unit())
            emit(s, {mir::Op::Return, "", Type::builtin("unit"), {}, "", {}, false, d.range});
        else
            diags_.error(d.range,
                         "E0501",
                         "function '" + d.name + "' may reach end without returning '" +
                             s.fn.result.str() + "'");
    }
    out_.functions.push_back(std::move(s.fn));
}
void MIRLowerer::emit_scope_drops(FnState& s) {
    if (s.scope_locals.empty())
        return;
    TraitSolver solver(model_);
    for (auto it = s.scope_locals.back().rbegin(); it != s.scope_locals.back().rend(); ++it) {
        auto ty = s.local_types.find(*it);
        if (ty != s.local_types.end() && !ty->second.is_copy() &&
            solver.satisfies(ty->second, "Drop")) {
            auto sl = s.slots.find(*it);
            if (sl != s.slots.end())
                emit(s, {mir::Op::Drop, "", ty->second, {sl->second}, "", {}, false, {}});
        }
    }
}
void MIRLowerer::lower_block(const ast::BlockStmt& b, FnState& s) {
    s.scope_locals.emplace_back();
    for (const auto& st : b.stmts) {
        if (block(s).terminated)
            break;
        lower_stmt(*st, s);
    }
    if (!block(s).terminated)
        emit_scope_drops(s);
    s.scope_locals.pop_back();
}
bool MIRLowerer::place_is_atomic(const ast::Expr& e, const FnState& s) const {
    const Type ty = expr_type(e, s);
    const bool compatible =
        ty.is_integer() || ty.kind == TypeKind::Bool || ty.kind == TypeKind::RawPtr;
    if (!compatible)
        return false;
    if (e.kind == ast::ExprKind::Name) {
        const auto& n = static_cast<const ast::NameExpr&>(e);
        auto g = model_.globals.find(n.name);
        if (g == model_.globals.end() || g->second.is_percpu)
            return false;
        if (g->second.is_shared)
            return true;
        return s.smp_mode != ast::SmpMode::Manual && s.concurrent && g->second.is_mut;
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
        (void)init_ty;
        Type t = init_ty;
        if (auto it = model_.binding_types.find(l); it != model_.binding_types.end())
            t = subst_type(it->second, s);
        std::string sl = slot(s, l->name);
        s.slots[l->name] = sl;
        s.local_types[l->name] = t;
        s.scope_locals.back().push_back(l->name);
        emit(s, {mir::Op::Alloca, sl, t, {}, "", {}, false, l->range});
        emit(s, {mir::Op::Store, "", t, {v, sl}, "", {}, false, l->range});
        return;
    }
    if (auto a = dynamic_cast<const ast::AssignStmt*>(&st)) {
        std::string addr = lower_place_address(*a->target, s);
        Type target_ty = expr_type(*a->target, s);
        auto [v, vt] = lower_expr(*a->value, s);
        (void)vt;
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
            (void)t;
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
std::string MIRLowerer::lower_place_address(const ast::Expr& e, FnState& s) {
    if (e.kind == ast::ExprKind::Unary) {
        const auto& u = static_cast<const ast::UnaryExpr&>(e);
        if (u.op == "*")
            return lower_expr(*u.operand, s).first;
    }
    if (e.kind == ast::ExprKind::Name) {
        auto& n = static_cast<const ast::NameExpr&>(e);
        auto it = s.slots.find(n.name);
        if (it != s.slots.end())
            return it->second;
        if (model_.globals.contains(n.name))
            return "@" + n.name;
        diags_.error(e.range, "E0502", "internal: no storage for '" + n.name + "'");
        return "null";
    }
    if (e.kind == ast::ExprKind::Member) {
        const auto& m = static_cast<const ast::MemberExpr&>(e);
        Type base_ty = expr_type(*m.base, s);
        std::string base_addr;
        if (base_ty.kind == TypeKind::Ref && base_ty.pointee) {
            auto lowered = lower_expr(*m.base, s);
            base_addr = lowered.first;
            base_ty = *base_ty.pointee;
        } else
            base_addr = lower_place_address(*m.base, s);
        if (base_ty.kind != TypeKind::Named) {
            diags_.error(e.range, "E0503", "field address base is not a struct");
            return "null";
        }
        auto si = model_.structs.find(base_ty.name);
        if (si == model_.structs.end() || !si->second.decl) {
            diags_.error(e.range, "E0503", "unknown struct layout for '" + base_ty.name + "'");
            return "null";
        }
        std::size_t index = 0;
        bool found = false;
        for (const auto& field : si->second.decl->fields) {
            if (field.name == m.member) {
                found = true;
                break;
            }
            ++index;
        }
        if (!found) {
            diags_.error(e.range, "E0503", "unknown field '" + m.member + "'");
            return "null";
        }
        Type field_ty = expr_type(e, s);
        std::string result = temp(s);
        mir::Instruction in;
        in.op = mir::Op::FieldAddr;
        in.result = result;
        in.type = field_ty;
        in.args = {base_addr};
        in.text = std::to_string(index);
        in.range = e.range;
        in.auxiliary_type = base_ty;
        emit(s, std::move(in));
        return result;
    }
    if (e.kind == ast::ExprKind::Index) {
        const auto& index_expr = static_cast<const ast::IndexExpr&>(e);
        Type container_ty = expr_type(*index_expr.base, s);
        std::string container_addr;
        if (container_ty.kind == TypeKind::Ref && container_ty.pointee) {
            container_addr = lower_expr(*index_expr.base, s).first;
            container_ty = *container_ty.pointee;
        } else
            container_addr = lower_place_address(*index_expr.base, s);
        if (container_ty.kind != TypeKind::Named ||
            (container_ty.name != "Slice" && container_ty.name != "SliceMut") ||
            container_ty.args.empty()) {
            diags_.error(e.range, "E0505", "MIR indexing requires Slice<T> or SliceMut<T>");
            return "null";
        }
        const Type elem = container_ty.args[0];
        const bool mut = container_ty.name == "SliceMut";
        const Type data_ty = Type::raw_ptr(elem, mut);
        const Type usize_ty = Type::builtin("usize");
        std::string data_addr = temp(s);
        mir::Instruction data_field;
        data_field.op = mir::Op::FieldAddr;
        data_field.result = data_addr;
        data_field.type = data_ty;
        data_field.args = {container_addr};
        data_field.text = "0";
        data_field.auxiliary_type = container_ty;
        data_field.range = e.range;
        emit(s, std::move(data_field));
        std::string data = temp(s);
        emit(s, {mir::Op::Load, data, data_ty, {data_addr}, "", {}, false, e.range});
        std::string len_addr = temp(s);
        mir::Instruction len_field;
        len_field.op = mir::Op::FieldAddr;
        len_field.result = len_addr;
        len_field.type = usize_ty;
        len_field.args = {container_addr};
        len_field.text = "1";
        len_field.auxiliary_type = container_ty;
        len_field.range = e.range;
        emit(s, std::move(len_field));
        std::string len = temp(s);
        emit(s, {mir::Op::Load, len, usize_ty, {len_addr}, "", {}, false, e.range});
        auto [index_value, index_ty] = lower_expr(*index_expr.index, s);
        std::string result = temp(s);
        mir::Instruction indexed;
        indexed.op = mir::Op::IndexAddr;
        indexed.result = result;
        indexed.type = elem;
        indexed.args = {data, index_value, len};
        indexed.auxiliary_type = index_ty;
        indexed.range = e.range;
        emit(s, std::move(indexed));
        return result;
    }
    diags_.error(e.range, "E0504", "expression is not an addressable place");
    return "null";
}
std::string
MIRLowerer::specialize_call(const ast::CallExpr& c, const FunctionSig& sig, FnState& s) {
    if (sig.generic_names.empty())
        return sig.name;
    std::unordered_map<std::string, Type> subst;
    if (!c.generic_args.empty()) {
        for (std::size_t i = 0; i < std::min(c.generic_args.size(), sig.generic_names.size()); ++i)
            subst[sig.generic_names[i]] = type_from_ast(c.generic_args[i]);
    } else {
        for (std::size_t i = 0; i < c.args.size() && i < sig.params.size(); ++i) {
            Type formal = subst_type(sig.params[i], s), actual = expr_type(*c.args[i], s);
            if (formal.kind == TypeKind::Generic)
                subst[formal.name] = actual;
        }
    }
    std::ostringstream name;
    name << sig.name;
    for (const auto& g : sig.generic_names) {
        auto it = subst.find(g);
        if (it == subst.end())
            continue;
        name << '$';
        for (char ch : it->second.str())
            name << (std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_');
    }
    std::string n = name.str();
    if (!emitted_.contains(n)) {
        pending_.push_back({sig.decl, subst, n});
    }
    return n;
}
std::pair<std::string, Type> MIRLowerer::lower_expr(const ast::Expr& e, FnState& s) {
    Type ty = expr_type(e, s);
    switch (e.kind) {
        case ast::ExprKind::Integer: {
            auto& x = static_cast<const ast::IntegerExpr&>(e);
            std::string r = temp(s);
            emit(s, {mir::Op::ConstInt, r, ty, {}, x.value, {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Float: {
            auto& x = static_cast<const ast::FloatExpr&>(e);
            std::string r = temp(s);
            emit(s, {mir::Op::ConstFloat, r, ty, {}, x.value, {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Bool: {
            auto& x = static_cast<const ast::BoolExpr&>(e);
            std::string r = temp(s);
            emit(s, {mir::Op::ConstBool, r, ty, {}, x.value ? "1" : "0", {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Char: {
            auto& x = static_cast<const ast::CharExpr&>(e);
            std::string r = temp(s);
            std::uint32_t v = x.value.empty() ? 0 : static_cast<unsigned char>(x.value[0]);
            emit(s, {mir::Op::ConstChar, r, ty, {}, std::to_string(v), {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::String: {
            auto& x = static_cast<const ast::StringExpr&>(e);
            std::string r = temp(s), sym = intern_string(x.value);
            emit(s, {mir::Op::ConstString, r, ty, {}, sym, {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Name: {
            auto addr = lower_place_address(e, s);
            std::string r = temp(s);
            const bool atomic = place_is_atomic(e, s);
            emit(s,
                 {mir::Op::Load,
                  r,
                  ty,
                  {addr},
                  atomic ? load_order(e, s) : "",
                  {},
                  atomic,
                  e.range});
            return {r, ty};
        }
        case ast::ExprKind::StructLiteral: {
            auto& st = static_cast<const ast::StructLiteralExpr&>(e);
            auto si = model_.structs.find(st.name);
            if (si == model_.structs.end() || !si->second.decl) {
                diags_.error(e.range, "E0506", "missing struct layout for '" + st.name + "'");
                return {"undef", ty};
            }
            std::unordered_map<std::string, const ast::Expr*> values;
            for (const auto& f : st.fields)
                values[f.name] = f.value.get();
            std::vector<std::string> args;
            for (const auto& decl_field : si->second.decl->fields) {
                auto it = values.find(decl_field.name);
                if (it == values.end()) {
                    args.push_back("undef");
                    continue;
                }
                args.push_back(lower_expr(*it->second, s).first);
            }
            std::string r = temp(s);
            emit(s, {mir::Op::StructInit, r, ty, std::move(args), "", {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Borrow: {
            auto& b = static_cast<const ast::BorrowExpr&>(e);
            std::string a = lower_place_address(*b.target, s);
            std::string r = temp(s);
            emit(s, {mir::Op::AddressOf, r, ty, {a}, "", {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Unary: {
            auto& u = static_cast<const ast::UnaryExpr&>(e);
            auto [v, t] = lower_expr(*u.operand, s);
            if (u.op == "move")
                return {v, ty};
            if (u.op == "*") {
                std::string r = temp(s);
                emit(s, {mir::Op::Load, r, ty, {v}, "", {}, false, e.range});
                return {r, ty};
            }
            std::string r = temp(s);
            std::string zero = temp(s);
            if (u.op == "-") {
                emit(s, {mir::Op::ConstInt, zero, t, {}, "0", {}, false, e.range});
                emit(s, {mir::Op::Binary, r, t, {zero, v}, "-", {}, false, e.range});
            } else
                emit(s, {mir::Op::Binary, r, t, {v}, u.op, {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(e);
            auto [l, lt] = lower_expr(*b.lhs, s);
            auto [r, rt] = lower_expr(*b.rhs, s);
            (void)rt;
            std::string out = temp(s);
            bool cmp = b.op == "==" || b.op == "!=" || b.op == "<" || b.op == "<=" || b.op == ">" ||
                       b.op == ">=";
            emit(s,
                 {cmp ? mir::Op::Compare : mir::Op::Binary,
                  out,
                  cmp ? lt : ty,
                  {l, r},
                  b.op,
                  {},
                  false,
                  e.range});
            return {out, ty};
        }
        case ast::ExprKind::Call: {
            auto& c = static_cast<const ast::CallExpr&>(e);
            std::string callee;
            std::vector<std::string> args;
            if (c.callee->kind == ast::ExprKind::Name) {
                auto& n = static_cast<const ast::NameExpr&>(*c.callee);
                auto fi = model_.functions.find(n.name);
                callee = n.name;
                if (fi != model_.functions.end())
                    callee = specialize_call(c, fi->second, s);
                for (auto& a : c.args)
                    args.push_back(lower_expr(*a, s).first);
            } else if (c.callee->kind == ast::ExprKind::Member) {
                auto& member = static_cast<const ast::MemberExpr&>(*c.callee);
                Type receiver_ty = expr_type(*member.base, s);
                Type owner = receiver_ty;
                if (owner.kind == TypeKind::Ref && owner.pointee)
                    owner = *owner.pointee;
                const auto fi = model_.functions.find(owner.name + "::" + member.member);
                if (fi == model_.functions.end()) {
                    diags_.error(
                        e.range, "E0507", "internal: method signature missing during MIR lowering");
                    return {"undef", ty};
                }
                callee = fi->second.name;
                if (!fi->second.params.empty() && fi->second.params.front().kind == TypeKind::Ref) {
                    if (receiver_ty.kind == TypeKind::Ref)
                        args.push_back(lower_expr(*member.base, s).first);
                    else
                        args.push_back(lower_place_address(*member.base, s));
                } else
                    args.push_back(lower_expr(*member.base, s).first);
                for (auto& a : c.args)
                    args.push_back(lower_expr(*a, s).first);
            } else {
                diags_.error(
                    e.range, "E0507", "internal: unsupported call target during MIR lowering");
                return {"undef", ty};
            }
            std::string r = ty.is_unit() ? "" : temp(s);
            emit(s, {mir::Op::Call, r, ty, std::move(args), callee, {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Cast: {
            auto& c = static_cast<const ast::CastExpr&>(e);
            auto [v, from] = lower_expr(*c.value, s);
            std::string r = temp(s);
            emit(s, {mir::Op::Cast, r, ty, {v}, from.str(), {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Asm: {
            auto& a = static_cast<const ast::AsmExpr&>(e);
            mir::Instruction in;
            in.op = mir::Op::InlineAsm;
            in.type = Type::builtin("unit");
            in.text = a.assembly;
            in.flag = a.is_volatile;
            in.range = e.range;
            for (const auto& o : a.operands) {
                mir::AsmOperand mo;
                mo.kind = o.kind;
                mo.constraint = o.constraint;
                if (o.value) {
                    auto [v, t] = lower_expr(*o.value, s);
                    mo.value = v;
                    mo.type = t;
                }
                if (!o.out_name.empty()) {
                    auto it = s.slots.find(o.out_name);
                    if (it != s.slots.end()) {
                        mo.out_slot = it->second;
                        mo.type = s.local_types[o.out_name];
                    }
                }
                in.asm_operands.push_back(std::move(mo));
            }
            emit(s, std::move(in));
            return {"", ty};
        }
        case ast::ExprKind::Await: {
            auto& a = static_cast<const ast::AwaitExpr&>(e);
            auto [v, future_ty] = lower_expr(*a.value, s);
            (void)future_ty;
            std::string r = temp(s);
            emit(s, {mir::Op::Await, r, ty, {v}, "", {}, false, e.range});
            return {r, ty};
        }
        case ast::ExprKind::Member: {
            std::string addr = lower_place_address(e, s);
            std::string r = temp(s);
            const bool atomic = place_is_atomic(e, s);
            emit(s,
                 {mir::Op::Load,
                  r,
                  ty,
                  {addr},
                  atomic ? load_order(e, s) : "",
                  {},
                  atomic,
                  e.range});
            return {r, ty};
        }
        case ast::ExprKind::Index: {
            std::string addr = lower_place_address(e, s);
            std::string r = temp(s);
            emit(s, {mir::Op::Load, r, ty, {addr}, "", {}, false, e.range});
            return {r, ty};
        }
    }
    return {"undef", ty};
}
void MIROptimizer::remove_unreachable(mir::Function& f) const {
    std::unordered_set<std::string> reachable;
    if (f.blocks.empty())
        return;
    std::unordered_map<std::string, std::size_t> labels;
    for (std::size_t i = 0; i < f.blocks.size(); ++i)
        labels[f.blocks[i].label] = i;
    std::vector<std::size_t> work{0};
    while (!work.empty()) {
        const std::size_t index = work.back();
        work.pop_back();
        if (index >= f.blocks.size() || !reachable.insert(f.blocks[index].label).second)
            continue;
        const auto& block = f.blocks[index];
        if (block.instructions.empty()) {
            if (index + 1 < f.blocks.size())
                work.push_back(index + 1);
            continue;
        }
        const auto& tail = block.instructions.back();
        if (tail.op == mir::Op::Jump && !tail.args.empty()) {
            if (auto it = labels.find(tail.args[0]); it != labels.end())
                work.push_back(it->second);
        } else if (tail.op == mir::Op::CondJump && tail.args.size() >= 3) {
            if (auto it = labels.find(tail.args[1]); it != labels.end())
                work.push_back(it->second);
            if (auto it = labels.find(tail.args[2]); it != labels.end())
                work.push_back(it->second);
        } else if (tail.op != mir::Op::Return && index + 1 < f.blocks.size()) {
            work.push_back(index + 1);
        }
    }
    f.blocks.erase(std::remove_if(f.blocks.begin(),
                                  f.blocks.end(),
                                  [&](const mir::BasicBlock& block) {
                                      return !reachable.contains(block.label);
                                  }),
                   f.blocks.end());
}

void MIROptimizer::forward_local_loads(mir::Function& fn) const {
    for (auto& block : fn.blocks) {
        std::unordered_map<std::string, std::string> slots;
        std::unordered_map<std::string, std::string> aliases;
        auto resolve = [&](std::string value) {
            for (int depth = 0; depth < 32; ++depth) {
                auto it = aliases.find(value);
                if (it == aliases.end() || it->second == value)
                    break;
                value = it->second;
            }
            return value;
        };
        std::vector<mir::Instruction> output;
        output.reserve(block.instructions.size());
        for (auto instruction : block.instructions) {
            for (auto& arg : instruction.args)
                arg = resolve(arg);
            for (auto& operand : instruction.asm_operands) {
                if (!operand.value.empty())
                    operand.value = resolve(operand.value);
            }
            const auto is_local_slot = [](const std::string& address) {
                return !address.empty() && address.front() == '%' &&
                       address.find(".slot.") != std::string::npos;
            };
            if (instruction.op == mir::Op::Load && !instruction.flag && !instruction.args.empty()) {
                const auto it = slots.find(instruction.args[0]);
                if (it != slots.end()) {
                    aliases[instruction.result] = resolve(it->second);
                    continue;
                }
            }
            if (instruction.op == mir::Op::Store && !instruction.flag &&
                instruction.args.size() >= 2) {
                const std::string& address = instruction.args[1];
                if (is_local_slot(address))
                    slots[address] = instruction.args[0];
                else
                    slots.clear();
            } else if (instruction.op == mir::Op::AddressOf) {
                if (!instruction.args.empty())
                    slots.erase(instruction.args[0]);
            } else if (instruction.op == mir::Op::Call || instruction.op == mir::Op::InlineAsm ||
                       instruction.op == mir::Op::AtomicRmw || instruction.op == mir::Op::Fence ||
                       instruction.op == mir::Op::CompilerFence ||
                       (instruction.op == mir::Op::Load && instruction.flag) ||
                       (instruction.op == mir::Op::Store && instruction.flag)) {
                slots.clear();
            }
            output.push_back(std::move(instruction));
        }
        block.instructions = std::move(output);
    }
}

void MIROptimizer::constant_fold(mir::Function& fn) const {
    for (auto& block : fn.blocks) {
        std::unordered_map<std::string, std::int64_t> constants;
        auto get = [&](const std::string& value, std::int64_t& out) {
            if (auto it = constants.find(value); it != constants.end()) {
                out = it->second;
                return true;
            }
            char* end = nullptr;
            const long long parsed = std::strtoll(value.c_str(), &end, 0);
            if (end && *end == '\0' && end != value.c_str()) {
                out = static_cast<std::int64_t>(parsed);
                return true;
            }
            return false;
        };
        for (auto& instruction : block.instructions) {
            if ((instruction.op == mir::Op::ConstInt || instruction.op == mir::Op::ConstBool ||
                 instruction.op == mir::Op::ConstChar) &&
                !instruction.result.empty()) {
                std::int64_t value{};
                if (get(instruction.text, value))
                    constants[instruction.result] = value;
                continue;
            }
            if (instruction.type.kind == TypeKind::I128 || instruction.type.kind == TypeKind::U128)
                continue;
            if (instruction.op == mir::Op::Binary && !instruction.result.empty()) {
                std::int64_t a{}, b{};
                if (instruction.text == "!" && instruction.args.size() == 1 &&
                    get(instruction.args[0], a)) {
                    const std::int64_t result = instruction.type.kind == TypeKind::Bool ? !a : ~a;
                    instruction.op = instruction.type.kind == TypeKind::Bool ? mir::Op::ConstBool
                                                                             : mir::Op::ConstInt;
                    instruction.args.clear();
                    instruction.text = std::to_string(result);
                    constants[instruction.result] = result;
                    continue;
                }
                if (instruction.args.size() != 2 || !get(instruction.args[0], a) ||
                    !get(instruction.args[1], b))
                    continue;
                std::int64_t result{};
                bool ok = true;
                if (instruction.text == "+")
                    result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) +
                                                       static_cast<std::uint64_t>(b));
                else if (instruction.text == "-")
                    result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) -
                                                       static_cast<std::uint64_t>(b));
                else if (instruction.text == "*")
                    result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) *
                                                       static_cast<std::uint64_t>(b));
                else if (instruction.text == "/") {
                    if (b == 0)
                        ok = false;
                    else
                        result = unsigned_integer_kind(instruction.type.kind)
                                     ? static_cast<std::int64_t>(static_cast<std::uint64_t>(a) /
                                                                 static_cast<std::uint64_t>(b))
                                     : a / b;
                } else if (instruction.text == "%") {
                    if (b == 0)
                        ok = false;
                    else
                        result = unsigned_integer_kind(instruction.type.kind)
                                     ? static_cast<std::int64_t>(static_cast<std::uint64_t>(a) %
                                                                 static_cast<std::uint64_t>(b))
                                     : a % b;
                } else if (instruction.text == "&" || instruction.text == "&&")
                    result = a & b;
                else if (instruction.text == "|" || instruction.text == "||")
                    result = a | b;
                else if (instruction.text == "^")
                    result = a ^ b;
                else if (instruction.text == "<<") {
                    if (b < 0 || b >= 64)
                        ok = false;
                    else
                        result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) << b);
                } else if (instruction.text == ">>") {
                    if (b < 0 || b >= 64)
                        ok = false;
                    else
                        result = unsigned_integer_kind(instruction.type.kind)
                                     ? static_cast<std::int64_t>(static_cast<std::uint64_t>(a) >> b)
                                     : (a >> b);
                } else
                    ok = false;
                if (ok) {
                    instruction.op = mir::Op::ConstInt;
                    instruction.args.clear();
                    instruction.text = std::to_string(result);
                    constants[instruction.result] = result;
                }
            } else if (instruction.op == mir::Op::Compare && instruction.args.size() == 2 &&
                       !instruction.result.empty()) {
                std::int64_t a{}, b{};
                if (!get(instruction.args[0], a) || !get(instruction.args[1], b))
                    continue;
                const bool u = unsigned_integer_kind(instruction.type.kind);
                bool result = false;
                if (instruction.text == "==")
                    result = a == b;
                else if (instruction.text == "!=")
                    result = a != b;
                else if (instruction.text == "<")
                    result =
                        u ? static_cast<std::uint64_t>(a) < static_cast<std::uint64_t>(b) : a < b;
                else if (instruction.text == "<=")
                    result =
                        u ? static_cast<std::uint64_t>(a) <= static_cast<std::uint64_t>(b) : a <= b;
                else if (instruction.text == ">")
                    result =
                        u ? static_cast<std::uint64_t>(a) > static_cast<std::uint64_t>(b) : a > b;
                else if (instruction.text == ">=")
                    result =
                        u ? static_cast<std::uint64_t>(a) >= static_cast<std::uint64_t>(b) : a >= b;
                instruction.op = mir::Op::ConstBool;
                instruction.type = Type::builtin("bool");
                instruction.args.clear();
                instruction.text = result ? "1" : "0";
                constants[instruction.result] = result ? 1 : 0;
            }
        }
    }
}

void MIROptimizer::eliminate_dead_values(mir::Function& fn) const {
    bool changed = true;
    while (changed) {
        changed = false;
        std::unordered_map<std::string, std::size_t> uses;
        for (const auto& block : fn.blocks)
            for (const auto& instruction : block.instructions) {
                for (const auto& arg : instruction.args)
                    ++uses[arg];
                for (const auto& operand : instruction.asm_operands)
                    if (!operand.value.empty())
                        ++uses[operand.value];
            }
        for (auto& block : fn.blocks) {
            auto it = std::remove_if(
                block.instructions.begin(),
                block.instructions.end(),
                [&](const mir::Instruction& instruction) {
                    if (instruction.result.empty() || uses[instruction.result] != 0)
                        return false;
                    bool pure =
                        instruction.op == mir::Op::Alloca || instruction.op == mir::Op::ConstInt ||
                        instruction.op == mir::Op::ConstFloat ||
                        instruction.op == mir::Op::ConstBool ||
                        instruction.op == mir::Op::ConstChar ||
                        instruction.op == mir::Op::ConstString ||
                        instruction.op == mir::Op::StructInit ||
                        instruction.op == mir::Op::FieldAddr || instruction.op == mir::Op::Binary ||
                        instruction.op == mir::Op::Compare || instruction.op == mir::Op::Cast ||
                        instruction.op == mir::Op::AddressOf ||
                        (instruction.op == mir::Op::Load && !instruction.flag);
                    if (pure)
                        changed = true;
                    return pure;
                });
            block.instructions.erase(it, block.instructions.end());
        }
    }
}

void MIROptimizer::run(mir::Module& m) const {
    if (level_ <= 0)
        return;
    for (auto& f : m.functions) {
        if (f.is_extern)
            continue;
        remove_unreachable(f);
        forward_local_loads(f);
        constant_fold(f);
        eliminate_dead_values(f);
        if (level_ >= 2) {
            forward_local_loads(f);
            eliminate_dead_values(f);
        }
    }
}

} // namespace uinx
