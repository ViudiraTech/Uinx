// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/borrow.hpp"

#include <algorithm>

namespace uinx {
static std::string root_of(std::string_view p) {
    auto n = p.find('.');
    return std::string(p.substr(0, n));
}
bool BorrowChecker::overlaps(std::string_view a, std::string_view b) const {
    if (a == b)
        return true;
    auto prefix = [](std::string_view x, std::string_view y) {
        return y.size() > x.size() && y.substr(0, x.size()) == x && y[x.size()] == '.';
    };
    return prefix(a, b) || prefix(b, a);
}
std::optional<std::string> BorrowChecker::place_of(const ast::Expr& e) const {
    if (e.kind == ast::ExprKind::Name)
        return static_cast<const ast::NameExpr&>(e).name;
    if (e.kind == ast::ExprKind::Member) {
        auto& m = static_cast<const ast::MemberExpr&>(e);
        auto b = place_of(*m.base);
        if (b)
            return *b + "." + m.member;
    }
    if (e.kind == ast::ExprKind::Index) {
        auto& i = static_cast<const ast::IndexExpr&>(e);
        auto b = place_of(*i.base);
        if (b)
            return *b + ".[*]";
    }
    return std::nullopt;
}
std::optional<BorrowChecker::RefOrigin> BorrowChecker::reference_origin(const ast::Expr& e,
                                                                        const State& s) const {
    if (e.kind == ast::ExprKind::Borrow) {
        auto& b = static_cast<const ast::BorrowExpr&>(e);
        if (auto p = place_of(*b.target))
            return RefOrigin{*p, e.range};
        return reference_origin(*b.target, s);
    }
    if (e.kind == ast::ExprKind::Name) {
        const auto& n = static_cast<const ast::NameExpr&>(e);
        auto it = s.ref_origins.find(n.name);
        if (it != s.ref_origins.end())
            return it->second;
        return std::nullopt;
    }
    if (e.kind == ast::ExprKind::StructLiteral) {
        for (const auto& field : static_cast<const ast::StructLiteralExpr&>(e).fields)
            if (auto origin = reference_origin(*field.value, s))
                return origin;
    } else if (e.kind == ast::ExprKind::Call) {
        const auto& c = static_cast<const ast::CallExpr&>(e);
        for (const auto& arg : c.args)
            if (auto origin = reference_origin(*arg, s))
                return origin;
    } else if (e.kind == ast::ExprKind::Cast) {
        return reference_origin(*static_cast<const ast::CastExpr&>(e).value, s);
    } else if (e.kind == ast::ExprKind::Await) {
        return reference_origin(*static_cast<const ast::AwaitExpr&>(e).value, s);
    }
    return std::nullopt;
}
bool BorrowChecker::contains_reference(const Type& t,
                                       std::unordered_set<std::string>& visiting) const {
    if (t.kind == TypeKind::Ref)
        return true;
    if (t.kind != TypeKind::Named)
        return false;
    if (!visiting.insert(t.name).second)
        return false;
    auto it = model_.structs.find(t.name);
    if (it == model_.structs.end()) {
        visiting.erase(t.name);
        return false;
    }
    for (const auto& [name, field] : it->second.fields) {
        (void)name;
        if (contains_reference(field, visiting)) {
            visiting.erase(t.name);
            return true;
        }
    }
    visiting.erase(t.name);
    return false;
}
Type BorrowChecker::type_of_place(std::string_view p, const State& s) const {
    std::string root = root_of(p);
    Type t;
    bool found = false;
    for (auto it = s.scopes.rbegin(); it != s.scopes.rend(); ++it) {
        auto f = it->find(root);
        if (f != it->end()) {
            t = f->second;
            found = true;
            break;
        }
    }
    if (!found)
        return {};
    std::size_t pos = root.size();
    while (pos < p.size() && p[pos] == '.') {
        ++pos;
        auto next = p.find('.', pos);
        std::string field(
            p.substr(pos, next == std::string_view::npos ? p.size() - pos : next - pos));
        if (t.kind == TypeKind::Ref && t.pointee)
            t = *t.pointee;
        if (field == "[*]") {
            if (t.kind == TypeKind::Named && (t.name == "Slice" || t.name == "SliceMut") &&
                !t.args.empty())
                t = t.args[0];
            else
                return {};
        } else {
            auto si = model_.structs.find(t.name);
            if (si == model_.structs.end())
                return {};
            auto fi = si->second.fields.find(field);
            if (fi == si->second.fields.end())
                return {};
            t = fi->second;
        }
        if (next == std::string_view::npos)
            break;
        pos = next;
    }
    return t;
}
bool BorrowChecker::unavailable(std::string_view p, const State& s) const {
    for (const auto& m : s.moved)
        if (overlaps(m, p))
            return true;
    return false;
}
void BorrowChecker::access_place(const std::string& p, const SourceRange& r, Access a, State& s) {
    if (unavailable(p, s)) {
        diags_.error(r, "E0400", "use of moved value/place '" + p + "'");
        return;
    }
    if (a == Access::BorrowShared) {
        for (const auto& b : s.borrows)
            if (overlaps(p, b.place) && b.mut)
                diags_.error(r,
                             "E0401",
                             "cannot immutably borrow '" + p + "' while mutable borrow of '" +
                                 b.place + "' is active");
        return;
    }
    if (a == Access::BorrowMut) {
        for (const auto& b : s.borrows)
            if (overlaps(p, b.place))
                diags_.error(r,
                             "E0402",
                             "cannot mutably borrow '" + p + "' while borrow of '" + b.place +
                                 "' is active");
        return;
    }
    if (a == Access::Write || a == Access::Move) {
        for (const auto& b : s.borrows)
            if (overlaps(p, b.place))
                diags_.error(r,
                             "E0403",
                             "cannot " + std::string(a == Access::Move ? "move" : "write") + " '" +
                                 p + "' while it is borrowed");
    }
    if (a == Access::Move) {
        Type t = type_of_place(p, s);
        if (!t.is_copy())
            s.moved.insert(p);
    }
}
void BorrowChecker::begin_borrow(const std::string& p,
                                 std::string borrower,
                                 bool mut,
                                 std::size_t end,
                                 const SourceRange& r,
                                 State& s) {
    access_place(p, r, mut ? Access::BorrowMut : Access::BorrowShared, s);
    s.borrows.push_back({p, std::move(borrower), mut, end, r});
}
void BorrowChecker::collect_expr_uses(const ast::Expr& e,
                                      std::size_t i,
                                      std::unordered_map<std::string, std::size_t>& o) const {
    switch (e.kind) {
        case ast::ExprKind::Name:
            o[static_cast<const ast::NameExpr&>(e).name] = i;
            break;
        case ast::ExprKind::StructLiteral: {
            for (auto& f : static_cast<const ast::StructLiteralExpr&>(e).fields)
                collect_expr_uses(*f.value, i, o);
            break;
        }
        case ast::ExprKind::Unary:
            collect_expr_uses(*static_cast<const ast::UnaryExpr&>(e).operand, i, o);
            break;
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(e);
            collect_expr_uses(*b.lhs, i, o);
            collect_expr_uses(*b.rhs, i, o);
            break;
        }
        case ast::ExprKind::Borrow:
            collect_expr_uses(*static_cast<const ast::BorrowExpr&>(e).target, i, o);
            break;
        case ast::ExprKind::Call: {
            auto& c = static_cast<const ast::CallExpr&>(e);
            collect_expr_uses(*c.callee, i, o);
            for (auto& a : c.args)
                collect_expr_uses(*a, i, o);
            break;
        }
        case ast::ExprKind::Member:
            collect_expr_uses(*static_cast<const ast::MemberExpr&>(e).base, i, o);
            break;
        case ast::ExprKind::Index: {
            auto& x = static_cast<const ast::IndexExpr&>(e);
            collect_expr_uses(*x.base, i, o);
            collect_expr_uses(*x.index, i, o);
            break;
        }
        case ast::ExprKind::Await:
            collect_expr_uses(*static_cast<const ast::AwaitExpr&>(e).value, i, o);
            break;
        case ast::ExprKind::Cast:
            collect_expr_uses(*static_cast<const ast::CastExpr&>(e).value, i, o);
            break;
        case ast::ExprKind::Asm: {
            for (auto& x : static_cast<const ast::AsmExpr&>(e).operands) {
                if (x.value)
                    collect_expr_uses(*x.value, i, o);
                if (!x.out_name.empty())
                    o[x.out_name] = i;
            }
            break;
        }
        default:
            break;
    }
}
void BorrowChecker::collect_uses(const ast::Stmt& s,
                                 std::size_t i,
                                 std::unordered_map<std::string, std::size_t>& o) const {
    if (auto l = dynamic_cast<const ast::LetStmt*>(&s)) {
        if (l->init)
            collect_expr_uses(*l->init, i, o);
    } else if (auto a = dynamic_cast<const ast::AssignStmt*>(&s)) {
        collect_expr_uses(*a->target, i, o);
        collect_expr_uses(*a->value, i, o);
    } else if (auto e = dynamic_cast<const ast::ExprStmt*>(&s))
        collect_expr_uses(*e->expr, i, o);
    else if (auto r = dynamic_cast<const ast::ReturnStmt*>(&s)) {
        if (r->value)
            collect_expr_uses(*r->value, i, o);
    } else if (auto b = dynamic_cast<const ast::BlockStmt*>(&s)) {
        for (auto& x : b->stmts)
            collect_uses(*x, i, o);
    } else if (auto x = dynamic_cast<const ast::IfStmt*>(&s)) {
        collect_expr_uses(*x->condition, i, o);
        for (auto& y : x->then_block->stmts)
            collect_uses(*y, i, o);
        if (x->else_block)
            for (auto& y : x->else_block->stmts)
                collect_uses(*y, i, o);
    } else if (auto w = dynamic_cast<const ast::WhileStmt*>(&s)) {
        collect_expr_uses(*w->condition, i, o);
        for (auto& y : w->body->stmts)
            collect_uses(*y, i, o);
    } else if (auto u = dynamic_cast<const ast::UnsafeStmt*>(&s)) {
        for (auto& y : u->body->stmts)
            collect_uses(*y, i, o);
    }
}
void BorrowChecker::inspect_expr(const ast::Expr& e, State& s, Access access) {
    if (auto p = place_of(e)) {
        access_place(*p, e.range, access, s);
        return;
    }
    switch (e.kind) {
        case ast::ExprKind::StructLiteral: {
            for (auto& f : static_cast<const ast::StructLiteralExpr&>(e).fields)
                inspect_expr(*f.value, s, Access::Move);
            break;
        }
        case ast::ExprKind::Unary: {
            auto& u = static_cast<const ast::UnaryExpr&>(e);
            inspect_expr(*u.operand, s, u.op == "*" ? Access::Read : access);
            break;
        }
        case ast::ExprKind::Binary: {
            auto& b = static_cast<const ast::BinaryExpr&>(e);
            inspect_expr(*b.lhs, s, Access::Move);
            inspect_expr(*b.rhs, s, Access::Move);
            break;
        }
        case ast::ExprKind::Borrow: {
            auto& b = static_cast<const ast::BorrowExpr&>(e);
            if (auto p2 = place_of(*b.target))
                begin_borrow(*p2, "<temporary>", b.mut, s.stmt_index, e.range, s);
            else
                inspect_expr(*b.target, s, Access::Read);
            break;
        }
        case ast::ExprKind::Call: {
            auto& c = static_cast<const ast::CallExpr&>(e);
            if (c.callee->kind == ast::ExprKind::Member) {
                auto& m = static_cast<const ast::MemberExpr&>(*c.callee);
                Type receiver{};
                if (auto rt = model_.expr_types.find(m.base.get()); rt != model_.expr_types.end())
                    receiver = rt->second;
                Type owner = receiver;
                if (owner.kind == TypeKind::Ref && owner.pointee)
                    owner = *owner.pointee;
                auto method = model_.functions.find(owner.name + "::" + m.member);
                if (method != model_.functions.end() && !method->second.params.empty()) {
                    auto receiver_access = Access::Move;
                    const Type& self_ty = method->second.params.front();
                    if (self_ty.kind == TypeKind::Ref)
                        receiver_access = self_ty.mut ? Access::BorrowMut : Access::BorrowShared;
                    inspect_expr(*m.base, s, receiver_access);
                }
            }
            for (auto& a : c.args) {
                auto it = model_.expr_types.find(a.get());
                Access aa = Access::Move;
                if (it != model_.expr_types.end() && it->second.kind == TypeKind::Ref &&
                    !it->second.mut)
                    aa = Access::Read;
                inspect_expr(*a, s, aa);
            }
            break;
        }
        case ast::ExprKind::Index: {
            auto& i = static_cast<const ast::IndexExpr&>(e);
            inspect_expr(*i.base, s, Access::Read);
            inspect_expr(*i.index, s, Access::Move);
            break;
        }
        case ast::ExprKind::Await:
            inspect_expr(*static_cast<const ast::AwaitExpr&>(e).value, s, Access::Move);
            break;
        case ast::ExprKind::Cast:
            inspect_expr(*static_cast<const ast::CastExpr&>(e).value, s, Access::Move);
            break;
        case ast::ExprKind::Asm: {
            auto& a = static_cast<const ast::AsmExpr&>(e);
            for (auto& o : a.operands) {
                if (o.value)
                    inspect_expr(*o.value, s, Access::Read);
                if (!o.out_name.empty())
                    access_place(o.out_name, o.range, Access::Write, s);
            }
            break;
        }
        default:
            break;
    }
}
void BorrowChecker::check_stmt(const ast::Stmt& st, State& s) {
    s.borrows.erase(std::remove_if(s.borrows.begin(),
                                   s.borrows.end(),
                                   [&](const Borrow& b) { return b.end_stmt < s.stmt_index; }),
                    s.borrows.end());
    if (auto l = dynamic_cast<const ast::LetStmt*>(&st)) {
        std::optional<RefOrigin> origin;
        if (l->init)
            origin = reference_origin(*l->init, s);
        if (l->init && l->init->kind == ast::ExprKind::Borrow) {
            auto& b = static_cast<const ast::BorrowExpr&>(*l->init);
            if (auto p = place_of(*b.target)) {
                auto end = s.last_use.contains(l->name) ? s.last_use[l->name] : s.stmt_index;
                begin_borrow(*p, l->name, b.mut, end, l->init->range, s);
            } else
                inspect_expr(*l->init, s, Access::Read);
        } else if (l->init)
            inspect_expr(*l->init, s, Access::Move);
        Type t{};
        auto it = model_.binding_types.find(l);
        if (it != model_.binding_types.end())
            t = it->second;
        s.scopes.back()[l->name] = t;
        s.moved.erase(l->name);
        if (t.kind == TypeKind::Ref && origin)
            s.ref_origins[l->name] = *origin;
        else
            s.ref_origins.erase(l->name);
        return;
    }
    if (auto a = dynamic_cast<const ast::AssignStmt*>(&st)) {
        auto target = place_of(*a->target);
        Type target_type = target ? type_of_place(*target, s) : Type{};
        std::optional<RefOrigin> origin = reference_origin(*a->value, s);
        if (a->op != "=")
            inspect_expr(*a->target, s, Access::Read);
        if (target && target_type.kind == TypeKind::Ref) {
            s.borrows.erase(std::remove_if(s.borrows.begin(),
                                           s.borrows.end(),
                                           [&](const Borrow& b) { return b.borrower == *target; }),
                            s.borrows.end());
        }
        bool began_reference_loan = false;
        if (target && target_type.kind == TypeKind::Ref &&
            a->value->kind == ast::ExprKind::Borrow) {
            auto& borrow = static_cast<const ast::BorrowExpr&>(*a->value);
            if (auto source = place_of(*borrow.target)) {
                const std::string borrower = *target;
                const std::string borrower_root = root_of(borrower);
                const auto end =
                    s.last_use.contains(borrower_root) ? s.last_use[borrower_root] : s.stmt_index;
                begin_borrow(*source, borrower, borrow.mut, end, a->value->range, s);
                began_reference_loan = true;
            }
        }
        if (!began_reference_loan)
            inspect_expr(*a->value, s, Access::Move);
        if (target) {
            access_place(*target, a->target->range, Access::Write, s);
            for (auto it = s.moved.begin(); it != s.moved.end();) {
                if (*it == *target)
                    it = s.moved.erase(it);
                else
                    ++it;
            }
            if (target_type.kind == TypeKind::Ref && origin)
                s.ref_origins[*target] = *origin;
            else if (target_type.kind == TypeKind::Ref)
                s.ref_origins.erase(*target);
        }
        return;
    }
    if (auto e = dynamic_cast<const ast::ExprStmt*>(&st)) {
        inspect_expr(*e->expr, s, Access::Move);
        return;
    }
    if (auto r = dynamic_cast<const ast::ReturnStmt*>(&st)) {
        if (r->value) {
            bool reference_bearing = false;
            if (auto type_it = model_.expr_types.find(r->value.get());
                type_it != model_.expr_types.end()) {
                std::unordered_set<std::string> visiting;
                reference_bearing = contains_reference(type_it->second, visiting);
            }
            if (reference_bearing)
                if (auto origin = reference_origin(*r->value, s)) {
                    const std::string root = root_of(origin->place);
                    bool stack_owned = false;
                    for (const auto& scope : s.scopes)
                        if (scope.contains(root)) {
                            stack_owned = true;
                            break;
                        }
                    if (stack_owned)
                        diags_.error(r->value->range,
                                     "E0404",
                                     "reference to stack-owned place '" + origin->place +
                                         "' escapes the function");
                }
            inspect_expr(*r->value, s, Access::Move);
        }
        return;
    }
    if (auto b = dynamic_cast<const ast::BlockStmt*>(&st)) {
        check_block(*b, s);
        return;
    }
    if (auto i = dynamic_cast<const ast::IfStmt*>(&st)) {
        inspect_expr(*i->condition, s, Access::Move);
        State left = s;
        check_block(*i->then_block, left);
        if (i->else_block) {
            State right = s;
            check_block(*i->else_block, right);
            for (auto& m : left.moved)
                if (right.moved.contains(m))
                    s.moved.insert(m);
        }
        return;
    }
    if (auto w = dynamic_cast<const ast::WhileStmt*>(&st)) {
        inspect_expr(*w->condition, s, Access::Move);
        State body = s;
        check_block(*w->body, body);
        for (auto& m : body.moved)
            s.moved.insert(m);
        return;
    }
    if (auto u = dynamic_cast<const ast::UnsafeStmt*>(&st)) {
        check_block(*u->body, s);
        return;
    }
}
void BorrowChecker::check_block(const ast::BlockStmt& b, State& s) {
    auto saved_last = s.last_use;
    std::unordered_map<std::string, std::size_t> local_last;
    for (std::size_t i = 0; i < b.stmts.size(); ++i)
        collect_uses(*b.stmts[i], i, local_last);
    s.last_use = local_last;
    s.scopes.emplace_back();
    std::size_t old_idx = s.stmt_index;
    for (std::size_t i = 0; i < b.stmts.size(); ++i) {
        s.stmt_index = i;
        check_stmt(*b.stmts[i], s);
    }
    std::unordered_set<std::string> locals;
    for (auto& [n, t] : s.scopes.back()) {
        (void)t;
        locals.insert(n);
    }
    for (const auto& [borrower, origin] : s.ref_origins) {
        if (locals.contains(root_of(borrower)))
            continue;
        if (locals.contains(root_of(origin.place)))
            diags_.error(origin.range,
                         "E0405",
                         "reference stored in '" + borrower + "' outlives local place '" +
                             origin.place + "'");
    }
    s.scopes.pop_back();
    s.borrows.erase(
        std::remove_if(s.borrows.begin(),
                       s.borrows.end(),
                       [&](const Borrow& br) { return locals.contains(root_of(br.borrower)); }),
        s.borrows.end());
    for (auto& l : locals)
        s.moved.erase(l);
    for (auto it = s.ref_origins.begin(); it != s.ref_origins.end();) {
        if (locals.contains(root_of(it->first)))
            it = s.ref_origins.erase(it);
        else
            ++it;
    }
    s.stmt_index = old_idx;
    s.last_use = std::move(saved_last);
}
void BorrowChecker::check_function(const ast::FunctionDecl& fn) {
    if (!fn.body)
        return;
    State s;
    s.scopes.emplace_back();
    for (const auto& p : fn.params)
        s.scopes.back()[p.name] = type_from_ast(p.type);
    check_block(*fn.body, s);
}
void BorrowChecker::check(const ast::Module& m) {
    for (const auto& i : m.items) {
        if (auto f = std::get_if<ast::FunctionDecl>(&i))
            check_function(*f);
        else if (auto im = std::get_if<ast::ImplDecl>(&i))
            for (const auto& method : im->methods)
                check_function(method);
    }
}
} // namespace uinx
