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
            // Expected field types under the call-site substitution, so
            // integer literals coerce across widths exactly like call
            // arguments and `let` bindings do (sema permits them via
            // `can_coerce_expr`; the backend must never see mismatched
            // operand types, e.g. i32 `7` into a u64 generic field).
            std::unordered_set<std::string> field_generics(si->second.generic_names.begin(),
                                                           si->second.generic_names.end());
            std::unordered_map<std::string, Type> field_subst;
            for (std::size_t i = 0; i < si->second.generic_names.size() && i < ty.args.size(); ++i)
                field_subst[si->second.generic_names[i]] = ty.args[i];
            std::vector<std::string> args;
            for (const auto& decl_field : si->second.decl->fields) {
                auto it = values.find(decl_field.name);
                if (it == values.end()) {
                    args.push_back("undef");
                    continue;
                }
                auto [field_value, field_actual] = lower_expr(*it->second, s);
                const Type field_expected =
                    substitute_type(type_from_ast(decl_field.type, field_generics), field_subst);
                args.push_back(
                    coerce_value(s, field_value, field_actual, field_expected, it->second->range));
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
                // `ty` is the (possibly coerced) result type from sema, while `t`
                // is the inner operand's own type. For `-literal` coerced to a
                // wider integer (e.g. `result >= -4095` with result: i64), using
                // `t` here emits `sub i32` whose result is later used as i64 and
                // produces invalid `icmp sge i64 %x, %y(i32)` IR at -O0
                // (-O2 hides it via constant folding). Lower in `ty` instead,
                // casting the operand up when needed.
                std::string operand = v;
                if (t != ty) {
                    std::string casted = temp(s);
                    emit(s, {mir::Op::Cast, casted, ty, {v}, t.str(), {}, false, e.range});
                    operand = casted;
                }
                if (ty.is_float()) {
                    emit(s, {mir::Op::ConstFloat, zero, ty, {}, "0.0", {}, false, e.range});
                } else {
                    emit(s, {mir::Op::ConstInt, zero, ty, {}, "0", {}, false, e.range});
                }
                emit(s, {mir::Op::Binary, r, ty, {zero, operand}, "-", {}, false, e.range});
            } else {
                std::string operand = v;
                if (t != ty) {
                    std::string casted = temp(s);
                    emit(s, {mir::Op::Cast, casted, ty, {v}, t.str(), {}, false, e.range});
                    operand = casted;
                }
                emit(s, {mir::Op::Binary, r, ty, {operand}, u.op, {}, false, e.range});
            }
            return {r, ty};
        }
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(e);
            auto [l, lt] = lower_expr(*b.lhs, s);
            auto [r, rt] = lower_expr(*b.rhs, s);
            std::string out = temp(s);
            bool cmp = b.op == "==" || b.op == "!=" || b.op == "<" || b.op == "<=" || b.op == ">" ||
                       b.op == ">=";
            if (cmp) {
                // Comparison operands must share one LLVM integer type.
                l = coerce_value(s, l, lt, lt, b.lhs->range);
                r = coerce_value(s, r, rt, lt, b.rhs->range);
            } else {
                l = coerce_value(s, l, lt, ty, b.lhs->range);
                r = coerce_value(s, r, rt, ty, b.rhs->range);
            }
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
            if (c.callee->kind == ast::ExprKind::Name) {
                const auto& name = static_cast<const ast::NameExpr&>(*c.callee).name;
                if (builtin_function(name))
                    return lower_builtin_call(c, s);
            }
            std::string callee;
            std::vector<std::string> args;
            if (c.callee->kind == ast::ExprKind::Name) {
                auto& n = static_cast<const ast::NameExpr&>(*c.callee);
                auto fi = model_.functions.find(n.name);
                callee = n.name;
                if (fi != model_.functions.end())
                    callee = specialize_call(c, fi->second, s);
                for (std::size_t i = 0; i < c.args.size(); ++i) {
                    auto [v, actual] = lower_expr(*c.args[i], s);
                    if (fi != model_.functions.end() && i < fi->second.params.size()) {
                        Type expected = subst_type(fi->second.params[i], s);
                        // For generic callees the formal may still be generic;
                        // coerce_value is a no-op unless it is a concrete
                        // int/bool/float mismatch (e.g. i32 literal -> i64 param).
                        v = coerce_value(s, v, actual, expected, c.args[i]->range);
                    }
                    args.push_back(v);
                }
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
                // Method params[0] is the receiver already handled above.
                const std::size_t param_offset =
                    fi->second.params.empty() ? 0 : args.size() - c.args.size();
                for (std::size_t i = 0; i < c.args.size(); ++i) {
                    auto [v, actual] = lower_expr(*c.args[i], s);
                    if (param_offset + i < fi->second.params.size()) {
                        Type expected = subst_type(fi->second.params[param_offset + i], s);
                        v = coerce_value(s, v, actual, expected, c.args[i]->range);
                    }
                    args.push_back(v);
                }
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

std::pair<std::string, Type> MIRLowerer::lower_builtin_call(const ast::CallExpr& call, FnState& s) {
    const auto& name = static_cast<const ast::NameExpr&>(*call.callee).name;
    const Type result = expr_type(call, s);
    auto declare_call = [&](const std::string& runtime_name,
                            const Type& runtime_result,
                            const std::vector<Type>& parameter_types) {
        std::vector<mir::Parameter> parameters;
        for (std::size_t i = 0; i < parameter_types.size(); ++i)
            parameters.push_back({"p" + std::to_string(i), parameter_types[i]});
        declare_runtime_function(runtime_name, runtime_result, std::move(parameters));
    };
    auto runtime_call = [&](const std::string& runtime_name,
                            const Type& runtime_result,
                            const std::vector<Type>& parameter_types,
                            std::vector<std::string> arguments,
                            const SourceRange& range) {
        declare_call(runtime_name, runtime_result, parameter_types);
        const std::string output = runtime_result.is_unit() ? "" : temp(s);
        emit(s,
             {mir::Op::Call,
              output,
              runtime_result,
              std::move(arguments),
              runtime_name,
              {},
              false,
              range});
        return output;
    };
    auto cast_value =
        [&](const std::string& value, const Type& from, const Type& to, const SourceRange& range) {
            if (from == to)
                return value;
            const std::string output = temp(s);
            emit(s, {mir::Op::Cast, output, to, {value}, from.str(), {}, false, range});
            return output;
        };

    if (name == "print" || name == "println" || name == "eprint" || name == "eprintln") {
        const bool stderr_output = name == "eprint" || name == "eprintln";
        const Type i32 = Type::builtin("i32");
        const Type fd_type = i32;
        const std::string fd = temp(s);
        emit(
            s,
            {mir::Op::ConstInt, fd, fd_type, {}, stderr_output ? "2" : "1", {}, false, call.range});

        for (std::size_t i = 0; i < call.args.size(); ++i) {
            if (i != 0) {
                runtime_call(
                    "uinx_print_space", Type::builtin("unit"), {fd_type}, {fd}, call.range);
            }
            auto [value, type] = lower_expr(*call.args[i], s);
            if (type.is_integer() && type.kind != TypeKind::I128 && type.kind != TypeKind::U128) {
                const Type target = Type::builtin(unsigned_integer_kind(type.kind) ? "u64" : "i64");
                value = cast_value(value, type, target, call.args[i]->range);
                runtime_call(unsigned_integer_kind(type.kind) ? "uinx_print_u64" : "uinx_print_i64",
                             Type::builtin("unit"),
                             {target, fd_type},
                             {value, fd},
                             call.args[i]->range);
            } else if (type.is_float()) {
                value = cast_value(value, type, Type::builtin("f64"), call.args[i]->range);
                runtime_call("uinx_print_f64",
                             Type::builtin("unit"),
                             {Type::builtin("f64"), fd_type},
                             {value, fd},
                             call.args[i]->range);
            } else if (type.kind == TypeKind::Bool) {
                value = cast_value(value, type, Type::builtin("i64"), call.args[i]->range);
                runtime_call("uinx_print_bool",
                             Type::builtin("unit"),
                             {Type::builtin("i64"), fd_type},
                             {value, fd},
                             call.args[i]->range);
            } else if (type.kind == TypeKind::Char) {
                runtime_call("uinx_print_char",
                             Type::builtin("unit"),
                             {Type::builtin("i32"), fd_type},
                             {value, fd},
                             call.args[i]->range);
            } else {
                runtime_call("uinx_print_cstr",
                             Type::builtin("unit"),
                             {Type::ref(Type::builtin("str"), false), fd_type},
                             {value, fd},
                             call.args[i]->range);
            }
        }
        runtime_call("uinx_print_newline", Type::builtin("unit"), {fd_type}, {fd}, call.range);
        return {"", Type::builtin("unit")};
    }

    if (name == "input") {
        const Type str_ref = Type::ref(Type::builtin("str"), false);
        std::string prompt = "null";
        if (!call.args.empty())
            prompt = lower_expr(*call.args[0], s).first;
        const std::string fd = temp(s);
        emit(s, {mir::Op::ConstInt, fd, Type::builtin("i32"), {}, "1", {}, false, call.range});
        return {runtime_call("uinx_input_cstr",
                             str_ref,
                             {str_ref, Type::builtin("i32")},
                             {prompt, fd},
                             call.range),
                str_ref};
    }

    auto [value, type] = lower_expr(*call.args[0], s);
    const Type str_ref = Type::ref(Type::builtin("str"), false);
    const auto is_string = [](const Type& checked) {
        return checked.kind == TypeKind::Str || (checked.kind == TypeKind::Ref && checked.pointee &&
                                                 checked.pointee->kind == TypeKind::Str);
    };
    if (name == "str") {
        if (is_string(type))
            return {value, str_ref};
        if (type.is_integer()) {
            const Type target = Type::builtin(unsigned_integer_kind(type.kind) ? "u64" : "i64");
            value = cast_value(value, type, target, call.range);
            return {runtime_call(unsigned_integer_kind(type.kind) ? "uinx_string_from_u64"
                                                                  : "uinx_string_from_i64",
                                 str_ref,
                                 {target},
                                 {value},
                                 call.range),
                    str_ref};
        }
        if (type.is_float()) {
            value = cast_value(value, type, Type::builtin("f64"), call.range);
            return {
                runtime_call(
                    "uinx_string_from_f64", str_ref, {Type::builtin("f64")}, {value}, call.range),
                str_ref};
        }
        if (type.kind == TypeKind::Bool) {
            value = cast_value(value, type, Type::builtin("i64"), call.range);
            return {
                runtime_call(
                    "uinx_string_from_bool", str_ref, {Type::builtin("i64")}, {value}, call.range),
                str_ref};
        }
        if (type.kind == TypeKind::Char)
            return {
                runtime_call(
                    "uinx_string_from_char", str_ref, {Type::builtin("i32")}, {value}, call.range),
                str_ref};
    }
    if (name == "int" || name == "float") {
        const Type target = Type::builtin(name == "int" ? "i64" : "f64");
        if (is_string(type)) {
            return {runtime_call(name == "int" ? "uinx_parse_i64_cstr" : "uinx_parse_f64_cstr",
                                 target,
                                 {str_ref},
                                 {value},
                                 call.range),
                    target};
        }
        return {cast_value(value, type, target, call.range), target};
    }
    if (name == "bool") {
        if (type.kind == TypeKind::Bool)
            return {value, type};
        if (is_string(type)) {
            const Type i64 = Type::builtin("i64");
            value = runtime_call("uinx_parse_i64_cstr", i64, {str_ref}, {value}, call.range);
            type = i64;
        }
        const std::string zero = temp(s);
        emit(s, {mir::Op::ConstInt, zero, type, {}, "0", {}, false, call.range});
        const std::string output = temp(s);
        emit(s,
             {mir::Op::Compare,
              output,
              Type::builtin("bool"),
              {value, zero},
              "!=",
              {},
              false,
              call.range});
        return {output, Type::builtin("bool")};
    }
    if (name == "len") {
        if (is_string(type))
            return {runtime_call(
                        "uinx_cstr_len", Type::builtin("usize"), {str_ref}, {value}, call.range),
                    Type::builtin("usize")};
    }
    diags_.error(call.range, "E0507", "internal: unsupported builtin lowering");
    return {"undef", result};
}
} // namespace uinx
