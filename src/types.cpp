// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/types.hpp"

#include <algorithm>

namespace uinx {
Type Type::builtin(std::string_view n) {
    static const std::unordered_map<std::string_view, TypeKind> m = {{"unit", TypeKind::Unit},
                                                                     {"never", TypeKind::Never},
                                                                     {"bool", TypeKind::Bool},
                                                                     {"i8", TypeKind::I8},
                                                                     {"i16", TypeKind::I16},
                                                                     {"i32", TypeKind::I32},
                                                                     {"i64", TypeKind::I64},
                                                                     {"i128", TypeKind::I128},
                                                                     {"u8", TypeKind::U8},
                                                                     {"u16", TypeKind::U16},
                                                                     {"u32", TypeKind::U32},
                                                                     {"u64", TypeKind::U64},
                                                                     {"u128", TypeKind::U128},
                                                                     {"isize", TypeKind::Isize},
                                                                     {"usize", TypeKind::Usize},
                                                                     {"f32", TypeKind::F32},
                                                                     {"f64", TypeKind::F64},
                                                                     {"char", TypeKind::Char},
                                                                     {"str", TypeKind::Str}};
    auto it = m.find(n);
    Type t;
    t.name = std::string(n);
    t.kind = it == m.end() ? TypeKind::Error : it->second;
    return t;
}
Type Type::named(std::string n, std::vector<Type> a) {
    Type t;
    t.kind = TypeKind::Named;
    t.name = std::move(n);
    t.args = std::move(a);
    return t;
}
Type Type::generic(std::string n) {
    Type t;
    t.kind = TypeKind::Generic;
    t.name = std::move(n);
    return t;
}
Type Type::ref(Type to, bool m) {
    Type t;
    t.kind = TypeKind::Ref;
    t.pointee = std::make_shared<Type>(std::move(to));
    t.mut = m;
    return t;
}
Type Type::raw_ptr(Type to, bool m) {
    Type t;
    t.kind = TypeKind::RawPtr;
    t.pointee = std::make_shared<Type>(std::move(to));
    t.mut = m;
    return t;
}
std::string Type::str() const {
    if (kind == TypeKind::Ref)
        return std::string(mut ? "mutref " : "ref ") + (pointee ? pointee->str() : "<error>");
    if (kind == TypeKind::RawPtr)
        return std::string(mut ? "mutptr " : "ptr ") + (pointee ? pointee->str() : "<error>");
    std::string s = name.empty() ? "<error>" : name;
    if (!args.empty()) {
        s += '<';
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i)
                s += ", ";
            s += args[i].str();
        }
        s += '>';
    }
    if (nullable)
        s += '?';
    return s;
}
bool Type::is_integer() const {
    return kind >= TypeKind::I8 && kind <= TypeKind::Usize;
}
bool Type::is_float() const {
    return kind == TypeKind::F32 || kind == TypeKind::F64;
}
bool Type::is_copy() const {
    return kind == TypeKind::Bool || is_numeric() || kind == TypeKind::Char ||
           (kind == TypeKind::Ref && !mut) || kind == TypeKind::RawPtr || kind == TypeKind::Unit;
}
bool Type::operator==(const Type& o) const {
    if (kind != o.kind || name != o.name || mut != o.mut || nullable != o.nullable ||
        args != o.args)
        return false;
    if (static_cast<bool>(pointee) != static_cast<bool>(o.pointee))
        return false;
    return !pointee || *pointee == *o.pointee;
}
Type type_from_ast(const ast::TypeRef& r, const std::unordered_set<std::string>& generics) {
    Type base;
    if (generics.contains(r.name))
        base = Type::generic(r.name);
    else {
        base = Type::builtin(r.name);
        if (base.kind == TypeKind::Error) {
            std::vector<Type> a;
            for (const auto& x : r.args)
                a.push_back(type_from_ast(x, generics));
            base = Type::named(r.name, std::move(a));
        }
    }
    base.nullable = r.nullable;
    if (!r.prefixes.empty()) {
        for (auto it = r.prefixes.rbegin(); it != r.prefixes.rend(); ++it) {
            if (*it == "&")
                base = Type::ref(base, false);
            else if (*it == "&mut")
                base = Type::ref(base, true);
            else if (*it == "*const")
                base = Type::raw_ptr(base, false);
            else if (*it == "*mut")
                base = Type::raw_ptr(base, true);
        }
        return base;
    }
    if (r.is_ref)
        return Type::ref(base, r.is_mut_ref);
    if (r.is_raw_ptr)
        return Type::raw_ptr(base, r.raw_mut);
    return base;
}
Type substitute_type(const Type& t, const std::unordered_map<std::string, Type>& s) {
    if (t.kind == TypeKind::Generic) {
        auto it = s.find(t.name);
        return it == s.end() ? t : it->second;
    }
    Type out = t;
    if (t.pointee)
        out.pointee = std::make_shared<Type>(substitute_type(*t.pointee, s));
    out.args.clear();
    for (const auto& a : t.args)
        out.args.push_back(substitute_type(a, s));
    return out;
}
bool can_coerce(const Type& f, const Type& t) {
    if (f == t || f.is_error() || t.is_error())
        return true;
    if (f.kind == TypeKind::Ref && t.kind == TypeKind::Ref && f.pointee && t.pointee) {
        if (f.mut && !t.mut)
            return can_coerce(*f.pointee, *t.pointee);
        return f.mut == t.mut && can_coerce(*f.pointee, *t.pointee);
    }
    return false;
}
} // namespace uinx
