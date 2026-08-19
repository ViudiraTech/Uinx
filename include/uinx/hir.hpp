// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/ast.hpp"
#include "uinx/diagnostic.hpp"

namespace uinx::hir {
using SymbolId=std::uint32_t;
enum class SymbolKind { Function, Struct, Trait, Global, Local, Parameter, Field, Generic };
struct Symbol { SymbolId id{}; SymbolKind kind{}; std::string name; SourceRange range{}; std::optional<SymbolId> parent; };
struct Module {
  const ast::Module* ast{};
  std::vector<Symbol> symbols;
  std::unordered_map<std::string,SymbolId> globals;
  std::unordered_map<const ast::Expr*,SymbolId> expr_resolution;
  std::unordered_map<const ast::LetStmt*,SymbolId> bindings;
  std::unordered_map<const ast::Param*,SymbolId> params;
};
}
namespace uinx {
class NameResolver {
 public:
  explicit NameResolver(Diagnostics& d):diags_(d){}
  hir::Module resolve(const ast::Module& module);
 private:
  hir::SymbolId add(hir::Module& out,hir::SymbolKind kind,std::string name,SourceRange range,std::optional<hir::SymbolId> parent={});
  void resolve_function(hir::Module& out,const ast::FunctionDecl& fn);
  void resolve_block(hir::Module& out,const ast::BlockStmt& block,std::vector<std::unordered_map<std::string,hir::SymbolId>>& scopes,std::optional<hir::SymbolId> owner);
  void resolve_expr(hir::Module& out,const ast::Expr& expr,std::vector<std::unordered_map<std::string,hir::SymbolId>>& scopes);
  Diagnostics& diags_;
};
}
