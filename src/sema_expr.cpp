// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "sema_internal.hpp"
#include "uinx/sema.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace uinx {
using namespace sema_detail;

Type TypeChecker::check_call(const ast::CallExpr& call, FnContext& c) {
    const FunctionSig* sig = nullptr;
    std::string display_name;
    std::vector<std::pair<const ast::Expr*, Type>> actuals;

    if (call.callee->kind == ast::ExprKind::Name) {
        const auto& n = static_cast<const ast::NameExpr&>(*call.callee);
        if (builtin_function(n.name))
            return check_builtin_call(call, c);
        auto it = model_.functions.find(n.name);
        if (it == model_.functions.end()) {
            diags_.error(call.range, "E0307", "unknown function '" + n.name + "'");
            return {};
        }
        sig = &it->second;
        display_name = n.name;
        for (auto& arg : call.args)
            actuals.push_back({arg.get(), check_expr(*arg, c)});
    } else if (call.callee->kind == ast::ExprKind::Member) {
        const auto& member = static_cast<const ast::MemberExpr&>(*call.callee);
        Type receiver = check_expr(*member.base, c, false);
        Type owner = receiver;
        if (owner.kind == TypeKind::Ref && owner.pointee)
            owner = *owner.pointee;
        if (owner.kind != TypeKind::Named) {
            diags_.error(call.range, "E0306", "method receiver must be a named type");
            return {};
        }
        const std::string key = owner.name + "::" + member.member;
        auto it = model_.functions.find(key);
        if (it == model_.functions.end()) {
            diags_.error(call.range,
                         "E0307",
                         "type '" + owner.str() + "' has no method '" + member.member + "'");
            return {};
        }
        sig = &it->second;
        display_name = owner.name + "." + member.member;
        if (sig->params.empty()) {
            diags_.error(call.range, "E0345", "method '" + display_name + "' has no self receiver");
            return sig->result;
        }
        Type receiver_actual = owner;
        const Type& expected_receiver = sig->params.front();
        if (expected_receiver.kind == TypeKind::Ref) {
            receiver_actual = Type::ref(owner, expected_receiver.mut);
            if (expected_receiver.mut && !is_mutable_place(*member.base, c) &&
                !(receiver.kind == TypeKind::Ref && receiver.mut))
                diags_.error(member.base->range,
                             "E0346",
                             "method '" + display_name + "' requires a mutable receiver");
        }
        actuals.push_back({member.base.get(), receiver_actual});
        for (auto& arg : call.args)
            actuals.push_back({arg.get(), check_expr(*arg, c)});
    } else {
        diags_.error(call.range, "E0306", "call target must be a function or method");
        return {};
    }

    if (sig->is_unsafe && !c.unsafe_context)
        diags_.error(call.range,
                     "E0308",
                     "call to unsafe function/method '" + display_name +
                         "' requires unsafe context");
    if (actuals.size() != sig->params.size()) {
        const std::size_t explicit_expected =
            sig->params.size() - (call.callee->kind == ast::ExprKind::Member ? 1u : 0u);
        diags_.error(call.range,
                     "E0309",
                     "function/method '" + display_name + "' expects " +
                         std::to_string(explicit_expected) + " arguments, got " +
                         std::to_string(call.args.size()));
        return sig->result;
    }
    std::unordered_map<std::string, Type> subst;
    if (!call.generic_args.empty()) {
        if (call.generic_args.size() != sig->generic_names.size())
            diags_.error(call.range, "E0310", "wrong number of explicit generic arguments");
        else
            for (std::size_t i = 0; i < call.generic_args.size(); ++i)
                subst[sig->generic_names[i]] = type_from_ast(call.generic_args[i]);
    }
    for (std::size_t i = 0; i < actuals.size(); ++i) {
        const Type actual = actuals[i].second;
        const Type expected = substitute_type(sig->params[i], subst);
        if (!unify_generic(sig->params[i], actual, subst) &&
            !can_coerce_expr(actuals[i].first, actual, expected))
            diags_.error(actuals[i].first->range,
                         "E0311",
                         "argument " + std::to_string(i + 1) + " has type '" + actual.str() +
                             "', expected '" + expected.str() + "'");
    }
    TraitSolver solver(model_);
    for (const auto& [g, bounds] : sig->bounds) {
        auto si = subst.find(g);
        if (si == subst.end()) {
            diags_.error(call.range, "E0312", "cannot infer generic parameter '" + g + "'");
            continue;
        }
        for (const auto& bound : bounds)
            if (!solver.satisfies(si->second, bound))
                diags_.error(call.range,
                             "E0313",
                             "type '" + si->second.str() + "' does not satisfy trait '" + bound +
                                 "'");
    }
    Type result = substitute_type(sig->result, subst);
    return sig->is_async ? Type::named("Future", {result}) : result;
}

Type TypeChecker::check_builtin_call(const ast::CallExpr& call, FnContext& c) {
    const auto& name = static_cast<const ast::NameExpr&>(*call.callee).name;
    const auto string_like = [](const Type& type) {
        return type.kind == TypeKind::Str ||
               (type.kind == TypeKind::Ref && type.pointee && type.pointee->kind == TypeKind::Str);
    };
    const auto printable = [&](const Type& type) {
        return type.is_integer() || type.is_float() || type.kind == TypeKind::Bool ||
               type.kind == TypeKind::Char || string_like(type);
    };
    std::vector<Type> arguments;
    for (const auto& argument : call.args)
        arguments.push_back(check_expr(*argument, c));

    if (name == "print" || name == "println" || name == "eprint" || name == "eprintln") {
        for (std::size_t i = 0; i < arguments.size(); ++i)
            if (!printable(arguments[i]))
                diags_.error(call.args[i]->range,
                             "E0367",
                             "print argument has unsupported type '" + arguments[i].str() + "'");
        return Type::builtin("unit");
    }
    if (name == "input") {
        if (arguments.size() > 1)
            diags_.error(call.range, "E0368", "input expects zero or one prompt argument");
        if (!arguments.empty() && !string_like(arguments[0]))
            diags_.error(call.args[0]->range, "E0369", "input prompt must be a string");
        return Type::ref(Type::builtin("str"), false);
    }
    if (name == "str") {
        if (arguments.size() != 1)
            diags_.error(call.range, "E0370", "str expects exactly one argument");
        else if (!printable(arguments[0]))
            diags_.error(call.args[0]->range,
                         "E0371",
                         "str argument has unsupported type '" + arguments[0].str() + "'");
        return Type::ref(Type::builtin("str"), false);
    }
    if (name == "int" || name == "float") {
        if (arguments.size() != 1)
            diags_.error(call.range, "E0370", name + " expects exactly one argument");
        else if (!arguments[0].is_numeric() && !string_like(arguments[0]))
            diags_.error(
                call.args[0]->range, "E0372", name + " argument must be numeric or string-like");
        return Type::builtin(name == "int" ? "i64" : "f64");
    }
    if (name == "bool") {
        if (arguments.size() != 1)
            diags_.error(call.range, "E0370", "bool expects exactly one argument");
        else if (arguments[0].kind != TypeKind::Bool && !arguments[0].is_numeric() &&
                 !string_like(arguments[0]))
            diags_.error(
                call.args[0]->range, "E0373", "bool argument must be scalar or string-like");
        return Type::builtin("bool");
    }
    if (name == "len") {
        if (arguments.size() != 1)
            diags_.error(call.range, "E0370", "len expects exactly one argument");
        else {
            const Type& type = arguments[0];
            if (!string_like(type))
                diags_.error(call.args[0]->range,
                             "E0374",
                             "len is not implemented for type '" + type.str() + "'");
        }
        return Type::builtin("usize");
    }
    return {};
}
void TypeChecker::validate_asm(const ast::AsmExpr& a, FnContext& c) {
    if (!c.unsafe_context)
        diags_.error(a.range, "E0314", "asm() is only permitted inside unsafe code");
    if (a.assembly.empty())
        diags_.warning(a.range, "W0315", "empty asm template");
    for (const auto& o : a.operands) {
        if (o.constraint.empty())
            diags_.error(o.range, "E0316", "asm register/clobber constraint cannot be empty");
        for (char ch : o.constraint)
            if (!(std::isalnum(static_cast<unsigned char>(ch)) ||
                  std::string("=+&{},~*").find(ch) != std::string::npos))
                diags_.error(
                    o.range, "E0317", "invalid character in asm constraint '" + o.constraint + "'");
        if (o.value)
            check_expr(*o.value, c);
    }
}
Type TypeChecker::check_expr(const ast::Expr& e, FnContext& c, bool) {
    Type t;
    switch (e.kind) {
        case ast::ExprKind::Integer:
            t = Type::builtin("i32");
            break;
        case ast::ExprKind::Float:
            t = Type::builtin("f64");
            break;
        case ast::ExprKind::String:
            t = Type::ref(Type::builtin("str"), false);
            break;
        case ast::ExprKind::Char:
            t = Type::builtin("char");
            break;
        case ast::ExprKind::Bool:
            t = Type::builtin("bool");
            break;
        case ast::ExprKind::Name: {
            auto& n = static_cast<const ast::NameExpr&>(e);
            auto fi = model_.functions.find(n.name);
            t = fi != model_.functions.end() ? Type::named("fn") : lookup_local(n.name, c, e.range);
            break;
        }
        case ast::ExprKind::StructLiteral: {
            auto& st = static_cast<const ast::StructLiteralExpr&>(e);
            auto si = model_.structs.find(st.name);
            if (si == model_.structs.end()) {
                diags_.error(e.range, "E0337", "unknown struct '" + st.name + "'");
                t = {};
                break;
            }
            std::vector<Type> type_args;
            for (const auto& arg : st.generic_args)
                type_args.push_back(type_from_ast(arg));
            if (type_args.size() != si->second.generic_names.size()) {
                diags_.error(e.range,
                             "E0338",
                             "struct '" + st.name + "' expects " +
                                 std::to_string(si->second.generic_names.size()) +
                                 " type arguments, got " + std::to_string(type_args.size()));
                t = {};
                break;
            }
            std::unordered_map<std::string, Type> subst;
            for (std::size_t i = 0; i < type_args.size(); ++i)
                subst[si->second.generic_names[i]] = type_args[i];
            TraitSolver solver(model_);
            for (const auto& [generic, bounds] : si->second.bounds) {
                const auto actual = subst.find(generic);
                if (actual == subst.end())
                    continue;
                for (const auto& bound : bounds)
                    if (!solver.satisfies(actual->second, bound))
                        diags_.error(e.range,
                                     "E0362",
                                     "type '" + actual->second.str() +
                                         "' does not satisfy struct bound '" + generic + ": " +
                                         bound + "'");
            }
            std::unordered_set<std::string> seen;
            for (const auto& f : st.fields) {
                if (!seen.insert(f.name).second)
                    diags_.error(
                        f.range, "E0339", "duplicate field '" + f.name + "' in struct literal");
                auto fi = si->second.fields.find(f.name);
                Type actual = check_expr(*f.value, c);
                if (fi == si->second.fields.end())
                    diags_.error(
                        f.range, "E0340", "struct '" + st.name + "' has no field '" + f.name + "'");
                else {
                    Type expected = substitute_type(fi->second, subst);
                    if (!can_coerce_expr(f.value.get(), actual, expected))
                        diags_.error(f.range,
                                     "E0341",
                                     "field '" + f.name + "' expects '" + expected.str() +
                                         "', found '" + actual.str() + "'");
                }
            }
            for (const auto& [name, ft] : si->second.fields) {
                (void)ft;
                if (!seen.contains(name))
                    diags_.error(e.range,
                                 "E0342",
                                 "missing field '" + name + "' in struct literal '" + st.name +
                                     "'");
            }
            t = Type::named(st.name, std::move(type_args));
            break;
        }
        case ast::ExprKind::Unary: {
            auto& u = static_cast<const ast::UnaryExpr&>(e);
            Type x = check_expr(*u.operand, c);
            if (u.op == "!") {
                if (x.kind != TypeKind::Bool && !x.is_integer())
                    diags_.error(e.range, "E0318", "not/! requires bool or integer");
                t = x;
            } else if (u.op == "~") {
                if (!x.is_integer())
                    diags_.error(e.range, "E0318", "~ requires an integer operand");
                t = x;
            } else if (u.op == "-") {
                if (!x.is_numeric())
                    diags_.error(e.range, "E0319", "unary - requires numeric operand");
                t = x;
            } else if (u.op == "move") {
                t = x;
            } else if (u.op == "*") {
                if (x.kind != TypeKind::Ref && x.kind != TypeKind::RawPtr) {
                    diags_.error(e.range, "E0320", "dereference requires reference or raw pointer");
                    t = {};
                } else {
                    if (x.kind == TypeKind::RawPtr && !c.unsafe_context)
                        diags_.error(e.range,
                                     "E0321",
                                     "dereferencing a raw pointer requires unsafe context");
                    t = x.pointee ? *x.pointee : Type{};
                }
            }
            break;
        }
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(e);
            Type l = check_expr(*b.lhs, c), r = check_expr(*b.rhs, c);
            if (l.is_integer() && r.is_integer() && l != r) {
                if (integer_literal(b.lhs.get()) && can_coerce_expr(b.lhs.get(), l, r)) {
                    l = r;
                    model_.expr_types[b.lhs.get()] = r;
                    // `-literal` is a Unary node wrapping the Integer token; the
                    // MIR unary lowering reads the inner operand type, so keep it
                    // in sync or `sub i32` feeds an `i64` compare.
                    if (b.lhs->kind == ast::ExprKind::Unary) {
                        const auto& unary = static_cast<const ast::UnaryExpr&>(*b.lhs);
                        if (unary.operand)
                            model_.expr_types[unary.operand.get()] = r;
                    }
                } else if (integer_literal(b.rhs.get()) && can_coerce_expr(b.rhs.get(), r, l)) {
                    r = l;
                    model_.expr_types[b.rhs.get()] = l;
                    if (b.rhs->kind == ast::ExprKind::Unary) {
                        const auto& unary = static_cast<const ast::UnaryExpr&>(*b.rhs);
                        if (unary.operand)
                            model_.expr_types[unary.operand.get()] = l;
                    }
                }
            }
            if ((l.kind == TypeKind::RawPtr || r.kind == TypeKind::RawPtr) &&
                (b.op == "+" || b.op == "-") && !c.unsafe_context)
                diags_.error(e.range, "E0322", "raw pointer arithmetic requires unsafe context");
            if (b.op == "==" || b.op == "!=" || b.op == "<" || b.op == "<=" || b.op == ">" ||
                b.op == ">=") {
                if (!can_coerce(l, r) && !can_coerce(r, l))
                    diags_.error(e.range, "E0323", "comparison operands have incompatible types");
                t = Type::builtin("bool");
            } else if (b.op == "&&" || b.op == "||") {
                if (l.kind != TypeKind::Bool || r.kind != TypeKind::Bool)
                    diags_.error(e.range, "E0324", "logical operators require bool operands");
                t = Type::builtin("bool");
            } else if (b.op == "&" || b.op == "|" || b.op == "^" || b.op == "<<" || b.op == ">>") {
                if (l != r || !l.is_integer())
                    diags_.error(e.range,
                                 "E0325",
                                 "bitwise/shift operators require matching integer operands");
                t = l;
            } else if (l.kind == TypeKind::RawPtr && (b.op == "+" || b.op == "-") && r.is_integer())
                t = l;
            else if (l == r && l.is_numeric())
                t = l;
            else {
                diags_.error(e.range,
                             "E0325",
                             "binary operator '" + b.op + "' cannot combine '" + l.str() +
                                 "' and '" + r.str() + "'");
                t = {};
            }
            break;
        }
        case ast::ExprKind::Borrow: {
            auto& b = static_cast<const ast::BorrowExpr&>(e);
            Type target = check_expr(*b.target, c, false);
            if (b.mut && !is_mutable_place(*b.target, c))
                diags_.error(e.range, "E0336", "mutable borrow requires a mutable place");
            t = Type::ref(target, b.mut);
            break;
        }
        case ast::ExprKind::Call:
            t = check_call(static_cast<const ast::CallExpr&>(e), c);
            break;
        case ast::ExprKind::Member: {
            auto& m = static_cast<const ast::MemberExpr&>(e);
            Type base = check_expr(*m.base, c, false);
            if (base.kind == TypeKind::Ref && base.pointee)
                base = *base.pointee;
            if (base.kind != TypeKind::Named || !model_.structs.contains(base.name)) {
                diags_.error(e.range, "E0326", "member access requires struct type");
                t = {};
            } else {
                auto& si = model_.structs.at(base.name);
                auto f = si.fields.find(m.member);
                if (f == si.fields.end()) {
                    diags_.error(e.range,
                                 "E0327",
                                 "struct '" + base.name + "' has no field '" + m.member + "'");
                    t = {};
                } else {
                    std::unordered_map<std::string, Type> subst;
                    for (std::size_t gi = 0; gi < si.generic_names.size() && gi < base.args.size();
                         ++gi)
                        subst[si.generic_names[gi]] = base.args[gi];
                    t = substitute_type(f->second, subst);
                }
            }
            break;
        }
        case ast::ExprKind::Index: {
            auto& i = static_cast<const ast::IndexExpr&>(e);
            Type b = check_expr(*i.base, c, false), idx = check_expr(*i.index, c);
            if (!idx.is_integer())
                diags_.error(i.index->range, "E0328", "index must be integer");
            Type container = b;
            if (container.kind == TypeKind::Ref && container.pointee)
                container = *container.pointee;
            if (container.kind == TypeKind::Named &&
                (container.name == "Slice" || container.name == "SliceMut") &&
                !container.args.empty())
                t = container.args[0];
            else if (container.kind == TypeKind::Str)
                t = Type::builtin("u8");
            else {
                diags_.error(e.range, "E0329", "type '" + b.str() + "' is not safely indexable");
                t = {};
            }
            break;
        }
        case ast::ExprKind::Await: {
            if (!c.sig->is_async)
                diags_.error(e.range, "E0343", "await is only valid inside an async function");
            Type f = check_expr(*static_cast<const ast::AwaitExpr&>(e).value, c);
            if (f.kind != TypeKind::Named || f.name != "Future" || f.args.size() != 1) {
                diags_.error(e.range, "E0330", "await requires Future<T>");
                t = {};
            } else
                t = f.args[0];
            break;
        }
        case ast::ExprKind::Cast: {
            auto& x = static_cast<const ast::CastExpr&>(e);
            Type from = check_expr(*x.value, c), to = type_from_ast(x.to);
            bool ref_to_raw = from.kind == TypeKind::Ref && to.kind == TypeKind::RawPtr &&
                              from.pointee && to.pointee;
            if (ref_to_raw) {
                if (!can_coerce(*from.pointee, *to.pointee))
                    diags_.error(
                        e.range, "E0332", "reference/raw-pointer pointee types are incompatible");
                if (to.mut && !from.mut)
                    diags_.error(e.range,
                                 "E0332",
                                 "cannot create mutable raw pointer from shared reference");
            } else if ((from.kind == TypeKind::RawPtr || to.kind == TypeKind::RawPtr) &&
                       !(from.kind == TypeKind::Str && to.kind == TypeKind::RawPtr) &&
                       !c.unsafe_context)
                diags_.error(e.range, "E0331", "this raw pointer cast requires unsafe context");
            const bool str_to_raw = from.kind == TypeKind::Str && to.kind == TypeKind::RawPtr &&
                                    to.pointee && to.pointee->kind == TypeKind::U8;
            if (!(from.is_numeric() && to.is_numeric()) && !ref_to_raw && !str_to_raw &&
                from.kind != TypeKind::RawPtr && to.kind != TypeKind::RawPtr)
                diags_.error(e.range,
                             "E0332",
                             "unsupported cast from '" + from.str() + "' to '" + to.str() + "'");
            t = to;
            break;
        }
        case ast::ExprKind::Asm: {
            auto& a = static_cast<const ast::AsmExpr&>(e);
            validate_asm(a, c);
            t = Type::builtin("unit");
            break;
        }
    }
    model_.expr_types[&e] = t;
    return t;
}
} // namespace uinx
