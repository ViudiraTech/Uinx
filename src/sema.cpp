// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/sema.hpp"

#include "sema_internal.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace uinx {
using namespace sema_detail;

void TypeChecker::collect_items(const ast::Module& module) {
    for (const auto& item : module.items) {
        if (auto f = std::get_if<ast::FunctionDecl>(&item)) {
            std::unordered_set<std::string> gs;
            FunctionSig sig;
            sig.name = f->name;
            sig.is_unsafe = f->is_unsafe;
            sig.is_async = f->is_async;
            sig.is_extern = f->is_extern;
            sig.is_concurrent = f->is_concurrent;
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
                info.bounds[g.name] = g.bounds;
            }
            for (const auto& field : s->fields) {
                Type ft = type_from_ast(field.type, gs);
                info.fields[field.name] = ft;
                if (field.is_shared) {
                    info.shared_fields.insert(field.name);
                    if (!(ft.is_integer() || ft.kind == TypeKind::RawPtr ||
                          ft.kind == TypeKind::Generic))
                        diags_.error(field.range,
                                     "E0358",
                                     "shared fields require integer, raw pointer, or "
                                     "atomic-compatible generic type (bool uses AtomicBool)");
                    if (ft.kind == TypeKind::Generic) {
                        const auto parameter = std::find_if(
                            s->generics.begin(),
                            s->generics.end(),
                            [&](const ast::GenericParam& g) { return g.name == ft.name; });
                        if (parameter == s->generics.end() ||
                            std::find(parameter->bounds.begin(), parameter->bounds.end(), "Send") ==
                                parameter->bounds.end() ||
                            std::find(parameter->bounds.begin(), parameter->bounds.end(), "Sync") ==
                                parameter->bounds.end())
                            diags_.error(field.range,
                                         "E0366",
                                         "a shared generic field requires both Send and Sync "
                                         "bounds");
                    }
                }
            }
            model_.structs[s->name] = std::move(info);
        } else if (auto g = std::get_if<ast::GlobalDecl>(&item)) {
            Type ty = type_from_ast(g->type);
            if (g->is_shared && g->is_percpu)
                diags_.error(g->range, "E0351", "a global cannot be both shared and percpu");
            if (g->is_shared && !(ty.is_integer() || ty.kind == TypeKind::RawPtr))
                diags_.error(g->range,
                             "E0352",
                             "shared globals require integer or raw pointer type (bool uses "
                             "AtomicBool)");
            model_.globals[g->name] = {
                g->name, ty, g->is_mut, g->is_const, g->is_shared, g->is_percpu, g};
        } else if (auto t = std::get_if<ast::TraitDecl>(&item)) {
            model_.traits[t->name] = {t->name, t};
        } else if (auto marker = std::get_if<ast::MarkerDecl>(&item)) {
            std::unordered_set<std::string> gs;
            MarkerInfo info;
            info.trait = marker->kind == ast::MarkerKind::Send ? "Send" : "Sync";
            info.decl = marker;
            for (const auto& generic : marker->generics) {
                gs.insert(generic.name);
                info.generic_names.push_back(generic.name);
                info.bounds[generic.name] = generic.bounds;
            }
            info.type = type_from_ast(marker->type, gs);
            model_.markers.push_back(std::move(info));
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
                sig.is_concurrent = m.is_concurrent;
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

bool TypeChecker::copy_eligible(const Type& type,
                                std::unordered_set<std::string>& visiting,
                                bool require_explicit_named_impl) const {
    if (type.is_copy())
        return true;
    if (type.kind == TypeKind::Ref)
        return !type.mut;
    if (type.kind == TypeKind::Generic)
        return false;
    if (type.kind != TypeKind::Named)
        return false;

    const std::string key = type.str();
    if (!visiting.insert(key).second)
        return false;

    const auto finish = [&](bool result) {
        visiting.erase(key);
        return result;
    };

    const bool has_drop =
        std::any_of(model_.impls.begin(), model_.impls.end(), [&](const ImplInfo& impl) {
            return impl.trait == "Drop" && impl.for_type == type;
        });
    if (has_drop)
        return finish(false);

    if (require_explicit_named_impl) {
        const bool has_copy =
            std::any_of(model_.impls.begin(), model_.impls.end(), [&](const ImplInfo& impl) {
                return impl.trait == "Copy" && impl.for_type == type;
            });
        if (!has_copy)
            return finish(false);
    }

    const auto structure = model_.structs.find(type.name);
    if (structure == model_.structs.end())
        return finish(false);
    if (type.args.size() != structure->second.generic_names.size())
        return finish(false);

    std::unordered_map<std::string, Type> subst;
    for (std::size_t i = 0; i < type.args.size(); ++i)
        subst[structure->second.generic_names[i]] = type.args[i];

    for (const auto& [field_name, field_type] : structure->second.fields) {
        (void)field_name;
        const Type concrete = substitute_type(field_type, subst);
        if (!copy_eligible(concrete, visiting, true))
            return finish(false);
    }
    return finish(true);
}

void TypeChecker::validate_special_traits() {
    for (const auto& impl : model_.impls) {
        if (impl.trait == "Send" || impl.trait == "Sync") {
            const SourceRange range = impl.decl ? impl.decl->range : SourceRange{};
            diags_.error(range,
                         "E0365",
                         "use the simpler unsafe " + impl.trait +
                             " declaration for an explicit thread-safety marker");
            continue;
        }
        if (impl.trait != "Copy")
            continue;
        std::unordered_set<std::string> visiting;
        if (!copy_eligible(impl.for_type, visiting, false)) {
            const SourceRange range = impl.decl ? impl.decl->range : SourceRange{};
            diags_.error(range,
                         "E0361",
                         "type '" + impl.for_type.str() +
                             "' cannot implement Copy: every field must be Copy and Copy is "
                             "incompatible with mutable references or Drop");
        }
    }
}

SemanticModel TypeChecker::check(const ast::Module& module) {
    collect_items(module);
    validate_special_traits();
    FunctionSig global_sig;
    global_sig.result = Type::builtin("unit");
    FnContext global_ctx;
    global_ctx.sig = &global_sig;
    global_ctx.scopes.emplace_back();
    global_ctx.mutable_scopes.emplace_back();
    for (const auto& [name, g] : model_.globals) {
        global_ctx.scopes.back()[name] = g.type;
        if (g.is_mut)
            global_ctx.mutable_scopes.back().insert(name);
    }
    for (const auto& item : module.items) {
        if (auto g = std::get_if<ast::GlobalDecl>(&item)) {
            if (!g->init) {
                diags_.error(g->range, "E0353", "global requires an initializer");
                continue;
            }
            Type init = check_expr(*g->init, global_ctx);
            Type expected = type_from_ast(g->type);
            if (!can_coerce_expr(g->init.get(), init, expected))
                diags_.error(g->range,
                             "E0354",
                             "global initializer has type '" + init.str() + "', expected '" +
                                 expected.str() + "'");
            if (!static_initializer_expr(*g->init))
                diags_.error(g->range,
                             "E0355",
                             "global initializer must be a compile-time scalar literal or a "
                             "zero-initialized struct");
        } else if (auto f = std::get_if<ast::FunctionDecl>(&item)) {
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
    propagate_concurrency(module.smp_mode);
    validate_concurrency_boundaries();
    return std::move(model_);
}
} // namespace uinx
