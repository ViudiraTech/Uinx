// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/ast.hpp"
#include <memory>

namespace uinx {
enum class TypeKind {
  Error, Unit, Never, Bool, I8, I16, I32, I64, I128, U8, U16, U32, U64, U128,
  Isize, Usize, F32, F64, Char, Str, Named, Generic, Ref, RawPtr
};
struct Type {
  TypeKind kind{TypeKind::Error};
  std::string name;
  std::vector<Type> args;
  std::shared_ptr<Type> pointee;
  bool mut{false};
  bool nullable{false};
  static Type builtin(std::string_view name);
  static Type named(std::string name,std::vector<Type> args={});
  static Type generic(std::string name);
  static Type ref(Type to,bool mut);
  static Type raw_ptr(Type to,bool mut);
  [[nodiscard]] std::string str() const;
  [[nodiscard]] bool is_integer() const;
  [[nodiscard]] bool is_float() const;
  [[nodiscard]] bool is_numeric() const { return is_integer() || is_float(); }
  [[nodiscard]] bool is_copy() const;
  [[nodiscard]] bool is_error() const { return kind==TypeKind::Error; }
  [[nodiscard]] bool is_unit() const { return kind==TypeKind::Unit; }
  [[nodiscard]] bool operator==(const Type& other) const;
  [[nodiscard]] bool operator!=(const Type& other) const { return !(*this==other); }
};
Type type_from_ast(const ast::TypeRef& ref,const std::unordered_set<std::string>& generics={});
Type substitute_type(const Type& type,const std::unordered_map<std::string,Type>& subst);
bool can_coerce(const Type& from,const Type& to);
}
