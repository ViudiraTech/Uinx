// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/sema.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace uinx {
namespace {
Type replace_self(Type type, const Type& owner) {
    if (type.kind == TypeKind::Named && type.name == "Self")
        return owner;
    if (type.pointee)
        type.pointee = std::make_shared<Type>(replace_self(*type.pointee, owner));
    for (auto& arg : type.args)
        arg = replace_self(std::move(arg), owner);
    return type;
}
} // namespace
hir::SymbolId NameResolver::add(hir::Module& out,
                                hir::SymbolKind kind,
                                std::string name,
                                SourceRange range,
                                std::optional<hir::SymbolId> parent) {
    hir::SymbolId id = static_cast<hir::SymbolId>(out.symbols.size());
    out.symbols.push_back({id, kind, std::move(name), std::move(range), parent});
    return id;
}
hir::Module NameResolver::resolve(const ast::Module& module) {
    hir::Module out;
    out.ast = &module;
    for (const auto& item : module.items) {
        std::visit(
            [&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, ast::FunctionDecl> ||
                              std::is_same_v<T, ast::StructDecl> ||
                              std::is_same_v<T, ast::TraitDecl>) {
                    hir::SymbolKind k = hir::SymbolKind::Function;
                    if constexpr (std::is_same_v<T, ast::StructDecl>)
                        k = hir::SymbolKind::Struct;
                    else if constexpr (std::is_same_v<T, ast::TraitDecl>)
                        k = hir::SymbolKind::Trait;
                    if (out.globals.contains(x.name)) {
                        if constexpr (std::is_same_v<T, ast::FunctionDecl>) {
                            if (!x.is_extern)
                                diags_.error(
                                    x.range, "E0200", "duplicate global symbol '" + x.name + "'");
                        } else
                            diags_.error(
                                x.range, "E0200", "duplicate global symbol '" + x.name + "'");
                    } else
                        out.globals[x.name] = add(out, k, x.name, x.range);
                }
            },
            item);
    }
    for (const auto& item : module.items)
        if (auto f = std::get_if<ast::FunctionDecl>(&item))
            resolve_function(out, *f);
        else if (auto im = std::get_if<ast::ImplDecl>(&item))
            for (const auto& m : im->methods)
                resolve_function(out, m);
    return out;
}
void NameResolver::resolve_function(hir::Module& out, const ast::FunctionDecl& fn) {
    std::optional<hir::SymbolId> owner;
    auto gi = out.globals.find(fn.name);
    if (gi != out.globals.end())
        owner = gi->second;
    std::vector<std::unordered_map<std::string, hir::SymbolId>> scopes(1);
    for (const auto& g : fn.generics) {
        auto id = add(out, hir::SymbolKind::Generic, g.name, g.range, owner);
        if (scopes.back().contains(g.name))
            diags_.error(g.range, "E0201", "duplicate generic parameter '" + g.name + "'");
        else
            scopes.back()[g.name] = id;
    }
    for (const auto& p : fn.params) {
        auto id = add(out, hir::SymbolKind::Parameter, p.name, p.range, owner);
        if (scopes.back().contains(p.name))
            diags_.error(p.range, "E0202", "duplicate parameter '" + p.name + "'");
        else
            scopes.back()[p.name] = id;
        out.params[&p] = id;
    }
    if (fn.body)
        resolve_block(out, *fn.body, scopes, owner);
}
void NameResolver::resolve_block(
    hir::Module& out,
    const ast::BlockStmt& block,
    std::vector<std::unordered_map<std::string, hir::SymbolId>>& scopes,
    std::optional<hir::SymbolId> owner) {
    scopes.emplace_back();
    for (const auto& sp : block.stmts) {
        const auto& s = *sp;
        if (auto l = dynamic_cast<const ast::LetStmt*>(&s)) {
            if (l->init)
                resolve_expr(out, *l->init, scopes);
            auto id = add(out, hir::SymbolKind::Local, l->name, l->range, owner);
            if (scopes.back().contains(l->name))
                diags_.error(
                    l->range, "E0203", "binding '" + l->name + "' already exists in this scope");
            else
                scopes.back()[l->name] = id;
            out.bindings[l] = id;
        } else if (auto e = dynamic_cast<const ast::ExprStmt*>(&s)) {
            resolve_expr(out, *e->expr, scopes);
        } else if (auto r = dynamic_cast<const ast::ReturnStmt*>(&s)) {
            if (r->value)
                resolve_expr(out, *r->value, scopes);
        } else if (auto b = dynamic_cast<const ast::BlockStmt*>(&s)) {
            resolve_block(out, *b, scopes, owner);
        } else if (auto i = dynamic_cast<const ast::IfStmt*>(&s)) {
            resolve_expr(out, *i->condition, scopes);
            resolve_block(out, *i->then_block, scopes, owner);
            if (i->else_block)
                resolve_block(out, *i->else_block, scopes, owner);
        } else if (auto w = dynamic_cast<const ast::WhileStmt*>(&s)) {
            resolve_expr(out, *w->condition, scopes);
            resolve_block(out, *w->body, scopes, owner);
        } else if (auto u = dynamic_cast<const ast::UnsafeStmt*>(&s)) {
            resolve_block(out, *u->body, scopes, owner);
        }
    }
    scopes.pop_back();
}
void NameResolver::resolve_expr(
    hir::Module& out,
    const ast::Expr& e,
    std::vector<std::unordered_map<std::string, hir::SymbolId>>& scopes) {
    switch (e.kind) {
        case ast::ExprKind::Name: {
            auto& n = static_cast<const ast::NameExpr&>(e);
            for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                auto f = it->find(n.name);
                if (f != it->end()) {
                    out.expr_resolution[&e] = f->second;
                    return;
                }
            }
            auto g = out.globals.find(n.name);
            if (g != out.globals.end()) {
                out.expr_resolution[&e] = g->second;
                return;
            }
            diags_.error(e.range, "E0204", "unresolved name '" + n.name + "'");
            break;
        }
        case ast::ExprKind::StructLiteral: {
            auto& st = static_cast<const ast::StructLiteralExpr&>(e);
            auto g = out.globals.find(st.name);
            if (g == out.globals.end())
                diags_.error(e.range, "E0206", "unknown struct '" + st.name + "'");
            else
                out.expr_resolution[&e] = g->second;
            for (auto& f : st.fields)
                resolve_expr(out, *f.value, scopes);
            break;
        }
        case ast::ExprKind::Unary:
            resolve_expr(out, *static_cast<const ast::UnaryExpr&>(e).operand, scopes);
            break;
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(e);
            resolve_expr(out, *b.lhs, scopes);
            resolve_expr(out, *b.rhs, scopes);
            break;
        }
        case ast::ExprKind::Borrow:
            resolve_expr(out, *static_cast<const ast::BorrowExpr&>(e).target, scopes);
            break;
        case ast::ExprKind::Call: {
            auto& c = static_cast<const ast::CallExpr&>(e);
            resolve_expr(out, *c.callee, scopes);
            for (auto& a : c.args)
                resolve_expr(out, *a, scopes);
            break;
        }
        case ast::ExprKind::Member:
            resolve_expr(out, *static_cast<const ast::MemberExpr&>(e).base, scopes);
            break;
        case ast::ExprKind::Index: {
            auto& i = static_cast<const ast::IndexExpr&>(e);
            resolve_expr(out, *i.base, scopes);
            resolve_expr(out, *i.index, scopes);
            break;
        }
        case ast::ExprKind::Await:
            resolve_expr(out, *static_cast<const ast::AwaitExpr&>(e).value, scopes);
            break;
        case ast::ExprKind::Cast:
            resolve_expr(out, *static_cast<const ast::CastExpr&>(e).value, scopes);
            break;
        case ast::ExprKind::Asm: {
            auto& a = static_cast<const ast::AsmExpr&>(e);
            for (auto& o : a.operands)
                if (o.value)
                    resolve_expr(out, *o.value, scopes);
            for (auto& o : a.operands)
                if (!o.out_name.empty()) {
                    bool found = false;
                    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
                        if (auto f = it->find(o.out_name); f != it->end()) {
                            found = true;
                            break;
                        }
                    if (!found)
                        diags_.error(
                            o.range, "E0205", "unknown asm output binding '" + o.out_name + "'");
                }
            break;
        }
        default:
            break;
    }
}

bool TraitSolver::satisfies(const Type& type, std::string_view trait) const {
    for (const auto& impl : model_.impls)
        if (impl.trait == trait && impl.for_type == type)
            return true;
    if (trait == "Copy")
        return type.is_copy();
    if (trait != "Send" && trait != "Sync")
        return false;

    std::unordered_set<std::string> visiting;
    std::function<bool(const Type&)> auto_trait = [&](const Type& current) -> bool {
        const std::string key = std::string(trait) + ":" + current.str();
        if (!visiting.insert(key).second)
            return true;
        auto finish = [&](bool value) {
            visiting.erase(key);
            return value;
        };
        if (current.kind == TypeKind::RawPtr)
            return finish(false);
        if (current.kind == TypeKind::Ref && current.pointee) {
            if (trait == "Send")
                return finish(auto_trait(*current.pointee));
            return finish(auto_trait(*current.pointee));
        }
        if (current.kind != TypeKind::Named)
            return finish(current.is_copy());
        for (const auto& impl : model_.impls)
            if (impl.trait == trait && impl.for_type == current)
                return finish(true);
        auto structure = model_.structs.find(current.name);
        if (structure == model_.structs.end())
            return finish(false);
        std::unordered_map<std::string, Type> subst;
        for (std::size_t i = 0;
             i < structure->second.generic_names.size() && i < current.args.size();
             ++i)
            subst[structure->second.generic_names[i]] = current.args[i];
        for (const auto& [field_name, field_type] : structure->second.fields) {
            (void)field_name;
            if (!auto_trait(substitute_type(field_type, subst)))
                return finish(false);
        }
        return finish(true);
    };
    return auto_trait(type);
}
void TypeChecker::collect_items(const ast::Module& module) {
    for (const auto& item : module.items) {
        if (auto f = std::get_if<ast::FunctionDecl>(&item)) {
            std::unordered_set<std::string> gs;
            FunctionSig sig;
            sig.name = f->name;
            sig.is_unsafe = f->is_unsafe;
            sig.is_async = f->is_async;
            sig.is_extern = f->is_extern;
            sig.abi = f->abi;
            sig.decl = f;
            for (const auto& g : f->generics) {
                gs.insert(g.name);
                sig.generic_names.push_back(g.name);
                sig.bounds[g.name] = g.bounds;
            }
            for (const auto& p : f->params)
                sig.params.push_back(type_from_ast(p.type, gs));
            sig.result = type_from_ast(f->return_type, gs);
            auto existing = model_.functions.find(f->name);
            if (existing != model_.functions.end()) {
                bool compatible = existing->second.is_extern && sig.is_extern &&
                                  existing->second.abi == sig.abi &&
                                  existing->second.params == sig.params &&
                                  existing->second.result == sig.result;
                if (!compatible)
                    diags_.error(f->range,
                                 "E0344",
                                 "conflicting declarations for function '" + f->name + "'");
            } else
                model_.functions[f->name] = std::move(sig);
        } else if (auto s = std::get_if<ast::StructDecl>(&item)) {
            StructInfo info;
            info.name = s->name;
            info.decl = s;
            std::unordered_set<std::string> gs;
            for (const auto& g : s->generics) {
                gs.insert(g.name);
                info.generic_names.push_back(g.name);
            }
            for (const auto& field : s->fields)
                info.fields[field.name] = type_from_ast(field.type, gs);
            model_.structs[s->name] = std::move(info);
        } else if (auto t = std::get_if<ast::TraitDecl>(&item)) {
            model_.traits[t->name] = {t->name, t};
        }
    }
    for (const auto& item : module.items)
        if (auto im = std::get_if<ast::ImplDecl>(&item)) {
            const Type owner = type_from_ast(im->for_type);
            if (im->trait_name && !model_.traits.contains(*im->trait_name))
                diags_.error(
                    im->range, "E0300", "impl references unknown trait '" + *im->trait_name + "'");
            const std::string trait_name = im->trait_name.value_or("");
            for (const auto& existing : model_.impls)
                if (existing.trait == trait_name && existing.for_type == owner)
                    diags_.error(im->range,
                                 "E0349",
                                 "duplicate impl for '" + owner.str() + "' and trait '" +
                                     (trait_name.empty() ? "<inherent>" : trait_name) + "'");
            model_.impls.push_back({trait_name, owner, im});
            for (const auto& m : im->methods) {
                std::unordered_set<std::string> gs;
                FunctionSig sig;
                sig.name = owner.name + "__" + m.name;
                sig.is_unsafe = m.is_unsafe;
                sig.is_async = m.is_async;
                sig.decl = &m;
                for (const auto& g : m.generics) {
                    gs.insert(g.name);
                    sig.generic_names.push_back(g.name);
                    sig.bounds[g.name] = g.bounds;
                }
                for (const auto& p : m.params)
                    sig.params.push_back(replace_self(type_from_ast(p.type, gs), owner));
                sig.result = replace_self(type_from_ast(m.return_type, gs), owner);
                const std::string method_key = owner.name + "::" + m.name;
                if (model_.functions.contains(method_key))
                    diags_.error(m.range,
                                 "E0350",
                                 "duplicate method '" + m.name + "' for type '" + owner.str() +
                                     "'");
                else
                    model_.functions[method_key] = std::move(sig);
            }
            if (im->trait_name) {
                auto trait_it = model_.traits.find(*im->trait_name);
                if (trait_it != model_.traits.end() && trait_it->second.decl) {
                    for (const auto& required : trait_it->second.decl->methods) {
                        auto actual_it = std::find_if(im->methods.begin(),
                                                      im->methods.end(),
                                                      [&](const ast::FunctionDecl& method) {
                                                          return method.name == required.name;
                                                      });
                        if (actual_it == im->methods.end()) {
                            diags_.error(im->range,
                                         "E0347",
                                         "trait impl for '" + owner.str() +
                                             "' is missing method '" + required.name + "'");
                            continue;
                        }
                        std::vector<Type> expected_params;
                        for (const auto& param : required.params)
                            expected_params.push_back(
                                replace_self(type_from_ast(param.type), owner));
                        const Type expected_result =
                            replace_self(type_from_ast(required.return_type), owner);
                        std::vector<Type> actual_params;
                        for (const auto& param : actual_it->params)
                            actual_params.push_back(replace_self(type_from_ast(param.type), owner));
                        const Type actual_result =
                            replace_self(type_from_ast(actual_it->return_type), owner);
                        if (expected_params != actual_params || expected_result != actual_result ||
                            required.is_unsafe != actual_it->is_unsafe)
                            diags_.error(actual_it->range,
                                         "E0348",
                                         "method '" + required.name +
                                             "' does not match trait declaration");
                    }
                }
            }
        }
}
SemanticModel TypeChecker::check(const ast::Module& module) {
    collect_items(module);
    for (const auto& item : module.items) {
        if (auto f = std::get_if<ast::FunctionDecl>(&item)) {
            auto it = model_.functions.find(f->name);
            if (it != model_.functions.end())
                check_function(*f, it->second);
        } else if (auto im = std::get_if<ast::ImplDecl>(&item)) {
            for (const auto& m : im->methods) {
                auto it = model_.functions.find(im->for_type.name + "::" + m.name);
                if (it != model_.functions.end())
                    check_function(m, it->second);
            }
        }
    }
    return std::move(model_);
}
void TypeChecker::check_function(const ast::FunctionDecl& fn, const FunctionSig& sig) {
    if (!fn.body)
        return;
    FnContext c;
    c.sig = &sig;
    c.unsafe_context = fn.is_unsafe;
    c.scopes.emplace_back();
    c.mutable_scopes.emplace_back();
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        c.scopes.back()[fn.params[i].name] = sig.params[i];
        if (fn.params[i].mut)
            c.mutable_scopes.back().insert(fn.params[i].name);
    }
    check_block(*fn.body, c);
}
void TypeChecker::check_block(const ast::BlockStmt& b, FnContext& c) {
    c.scopes.emplace_back();
    c.mutable_scopes.emplace_back();
    for (const auto& s : b.stmts)
        check_stmt(*s, c);
    c.mutable_scopes.pop_back();
    c.scopes.pop_back();
}
void TypeChecker::check_stmt(const ast::Stmt& s, FnContext& c) {
    if (auto l = dynamic_cast<const ast::LetStmt*>(&s)) {
        Type init = l->init ? check_expr(*l->init, c) : Type{};
        Type ty = l->type
                      ? type_from_ast(*l->type,
                                      std::unordered_set<std::string>(c.sig->generic_names.begin(),
                                                                      c.sig->generic_names.end()))
                      : init;
        if (l->type && !can_coerce(init, ty))
            diags_.error(l->range,
                         "E0301",
                         "initializer has type '" + init.str() + "' but binding expects '" +
                             ty.str() + "'");
        model_.binding_types[l] = ty;
        c.scopes.back()[l->name] = ty;
        if (l->mut)
            c.mutable_scopes.back().insert(l->name);
        return;
    }
    if (auto a = dynamic_cast<const ast::AssignStmt*>(&s)) {
        Type lhs = check_expr(*a->target, c, false), rhs = check_expr(*a->value, c);
        if (!is_mutable_place(*a->target, c))
            diags_.error(a->target->range, "E0333", "assignment requires a mutable place");
        if (!can_coerce(rhs, lhs))
            diags_.error(
                a->range, "E0334", "cannot assign '" + rhs.str() + "' to '" + lhs.str() + "'");
        if (a->op != "=" && (!lhs.is_numeric() || !rhs.is_numeric()))
            diags_.error(a->range, "E0335", "compound assignment requires numeric operands");
        return;
    }
    if (auto e = dynamic_cast<const ast::ExprStmt*>(&s)) {
        check_expr(*e->expr, c);
        return;
    }
    if (auto r = dynamic_cast<const ast::ReturnStmt*>(&s)) {
        Type got = r->value ? check_expr(*r->value, c) : Type::builtin("unit");
        if (!can_coerce(got, c.sig->result))
            diags_.error(r->range,
                         "E0302",
                         "return type '" + got.str() + "' does not match '" + c.sig->result.str() +
                             "'");
        return;
    }
    if (auto b = dynamic_cast<const ast::BlockStmt*>(&s)) {
        check_block(*b, c);
        return;
    }
    if (auto i = dynamic_cast<const ast::IfStmt*>(&s)) {
        Type t = check_expr(*i->condition, c);
        if (t.kind != TypeKind::Bool)
            diags_.error(
                i->condition->range, "E0303", "if condition must be bool, found '" + t.str() + "'");
        check_block(*i->then_block, c);
        if (i->else_block)
            check_block(*i->else_block, c);
        return;
    }
    if (auto w = dynamic_cast<const ast::WhileStmt*>(&s)) {
        Type t = check_expr(*w->condition, c);
        if (t.kind != TypeKind::Bool)
            diags_.error(w->condition->range, "E0304", "while condition must be bool");
        check_block(*w->body, c);
        return;
    }
    if (auto u = dynamic_cast<const ast::UnsafeStmt*>(&s)) {
        bool old = c.unsafe_context;
        c.unsafe_context = true;
        check_block(*u->body, c);
        c.unsafe_context = old;
        return;
    }
}
Type TypeChecker::lookup_local(std::string_view n, const FnContext& c, const SourceRange& r) {
    for (auto it = c.scopes.rbegin(); it != c.scopes.rend(); ++it) {
        auto f = it->find(std::string(n));
        if (f != it->end())
            return f->second;
    }
    diags_.error(r, "E0305", "name '" + std::string(n) + "' is not a value in this scope");
    return {};
}
bool TypeChecker::is_mutable_binding(std::string_view n, const FnContext& c) const {
    for (auto it = c.mutable_scopes.rbegin(); it != c.mutable_scopes.rend(); ++it)
        if (it->contains(std::string(n)))
            return true;
    return false;
}
bool TypeChecker::is_mutable_place(const ast::Expr& e, const FnContext& c) {
    if (e.kind == ast::ExprKind::Name)
        return is_mutable_binding(static_cast<const ast::NameExpr&>(e).name, c);
    if (e.kind == ast::ExprKind::Member) {
        const auto& m = static_cast<const ast::MemberExpr&>(e);
        Type base{};
        if (auto it = model_.expr_types.find(m.base.get()); it != model_.expr_types.end())
            base = it->second;
        if (base.kind == TypeKind::Ref)
            return base.mut;
        return is_mutable_place(*m.base, c);
    }
    if (e.kind == ast::ExprKind::Index) {
        const auto& i = static_cast<const ast::IndexExpr&>(e);
        Type base{};
        if (auto it = model_.expr_types.find(i.base.get()); it != model_.expr_types.end())
            base = it->second;
        if (base.kind == TypeKind::Ref)
            return base.mut;
        if (base.kind == TypeKind::Named && base.name == "SliceMut")
            return is_mutable_place(*i.base, c);
        return false;
    }
    if (e.kind == ast::ExprKind::Unary) {
        const auto& u = static_cast<const ast::UnaryExpr&>(e);
        if (u.op == "*") {
            Type p{};
            if (auto it = model_.expr_types.find(u.operand.get()); it != model_.expr_types.end())
                p = it->second;
            return p.kind == TypeKind::Ref ? p.mut : (p.kind == TypeKind::RawPtr ? p.mut : false);
        }
    }
    return false;
}
bool TypeChecker::unify_generic(const Type& f,
                                const Type& a,
                                std::unordered_map<std::string, Type>& s) {
    if (f.kind == TypeKind::Generic) {
        auto it = s.find(f.name);
        if (it == s.end()) {
            s[f.name] = a;
            return true;
        }
        return it->second == a;
    }
    if (f.kind != a.kind)
        return false;
    if (f.pointee && a.pointee)
        return f.mut == a.mut && unify_generic(*f.pointee, *a.pointee, s);
    if (f.args.size() != a.args.size())
        return false;
    for (std::size_t i = 0; i < f.args.size(); ++i)
        if (!unify_generic(f.args[i], a.args[i], s))
            return false;
    return f.name == a.name || f.name.empty() || a.name.empty();
}
Type TypeChecker::check_call(const ast::CallExpr& call, FnContext& c) {
    const FunctionSig* sig = nullptr;
    std::string display_name;
    std::vector<std::pair<const ast::Expr*, Type>> actuals;

    if (call.callee->kind == ast::ExprKind::Name) {
        const auto& n = static_cast<const ast::NameExpr&>(*call.callee);
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
        if (!unify_generic(sig->params[i], actual, subst) && !can_coerce(actual, expected))
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
                    if (!can_coerce(actual, expected))
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
                    diags_.error(e.range, "E0318", "! requires bool or integer");
                t = x;
            } else if (u.op == "-") {
                if (!x.is_numeric())
                    diags_.error(e.range, "E0319", "unary - requires numeric operand");
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
                       !c.unsafe_context)
                diags_.error(e.range, "E0331", "this raw pointer cast requires unsafe context");
            if (!(from.is_numeric() && to.is_numeric()) && !ref_to_raw &&
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
