// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/mir.hpp"

#include "mir_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace uinx {
using namespace mir_detail;

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
void MIRLowerer::declare_runtime_function(std::string name,
                                          Type result,
                                          std::vector<mir::Parameter> params) {
    for (const auto& function : out_.functions)
        if (function.name == name)
            return;
    mir::Function declaration;
    declaration.name = name;
    declaration.source_name = name;
    declaration.result = std::move(result);
    declaration.params = std::move(params);
    declaration.is_extern = true;
    out_.functions.push_back(std::move(declaration));
}
Type MIRLowerer::subst_type(const Type& t, const FnState& s) const {
    return substitute_type(t, s.subst);
}
Type MIRLowerer::expr_type(const ast::Expr& e, const FnState& s) const {
    auto it = model_.expr_types.find(&e);
    return it == model_.expr_types.end() ? Type{} : subst_type(it->second, s);
}
std::string MIRLowerer::coerce_value(
    FnState& s, std::string value, const Type& from, const Type& to, const SourceRange& range) {
    if (from == to || value == "undef" || value.empty())
        return value;
    // Integer literals are allowed to coerce across widths in sema
    // (`can_coerce_expr`); materialize that as an explicit MIR cast so the
    // backend never sees mismatched operand types (e.g. i32 temp stored into
    // an i64 slot, or `sub i32` feeding `icmp sge i64`).
    const bool int_to_int = from.is_integer() && to.is_integer();
    const bool bool_to_int = from.kind == TypeKind::Bool && to.is_integer();
    const bool int_to_float = from.is_integer() && to.is_float();
    const bool float_to_float = from.is_float() && to.is_float();
    const bool float_to_int = from.is_float() && to.is_integer();
    if (!int_to_int && !bool_to_int && !int_to_float && !float_to_float && !float_to_int)
        return value;
    std::string out = temp(s);
    emit(s, {mir::Op::Cast, out, to, {value}, from.str(), {}, false, range});
    return out;
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
    s.fn.is_extern = d.is_extern || !d.body;
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
} // namespace uinx
