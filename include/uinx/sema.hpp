// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/hir.hpp"
#include "uinx/types.hpp"

namespace uinx {
struct FunctionSig {
  std::string name;
  std::vector<std::string> generic_names;
  std::unordered_map<std::string,std::vector<std::string>> bounds;
  std::vector<Type> params;
  Type result;
  bool is_unsafe{false};
  bool is_async{false};
  bool is_extern{false};
  bool is_concurrent{false};
  std::string abi;
  const ast::FunctionDecl* decl{};
};
struct StructInfo { std::string name; std::vector<std::string> generic_names; std::unordered_map<std::string,Type> fields; std::unordered_set<std::string> shared_fields; const ast::StructDecl* decl{}; };
struct GlobalInfo { std::string name; Type type; bool is_mut{false}; bool is_const{false}; bool is_shared{false}; bool is_percpu{false}; const ast::GlobalDecl* decl{}; };
struct TraitInfo { std::string name; const ast::TraitDecl* decl{}; };
struct ImplInfo { std::string trait; Type for_type; const ast::ImplDecl* decl{}; };
struct SemanticModel {
  hir::Module hir;
  std::unordered_map<std::string,FunctionSig> functions;
  std::unordered_map<std::string,StructInfo> structs;
  std::unordered_map<std::string,GlobalInfo> globals;
  std::unordered_map<std::string,TraitInfo> traits;
  std::vector<ImplInfo> impls;
  std::unordered_map<const ast::Expr*,Type> expr_types;
  std::unordered_map<const ast::LetStmt*,Type> binding_types;
};
class TraitSolver {
 public:
  explicit TraitSolver(const SemanticModel& model):model_(model){}
  bool satisfies(const Type& type,std::string_view trait) const;
 private: const SemanticModel& model_;
};
class TypeChecker {
 public:
  TypeChecker(Diagnostics& d,hir::Module hir):diags_(d){model_.hir=std::move(hir);}
  SemanticModel check(const ast::Module& module);
 private:
  struct FnContext { const FunctionSig* sig{}; std::vector<std::unordered_map<std::string,Type>> scopes; std::vector<std::unordered_set<std::string>> mutable_scopes; bool unsafe_context{false}; std::size_t loop_depth{0}; };
  void collect_items(const ast::Module& module);
  void check_function(const ast::FunctionDecl& fn,const FunctionSig& sig);
  void check_block(const ast::BlockStmt& block,FnContext& ctx);
  void check_stmt(const ast::Stmt& stmt,FnContext& ctx);
  Type check_expr(const ast::Expr& expr,FnContext& ctx,bool value_context=true);
  Type check_call(const ast::CallExpr& call,FnContext& ctx);
  Type lookup_local(std::string_view name,const FnContext& ctx,const SourceRange& range);
  bool is_mutable_binding(std::string_view name,const FnContext& ctx) const;
  bool is_mutable_place(const ast::Expr& expr,const FnContext& ctx);
  bool unify_generic(const Type& formal,const Type& actual,std::unordered_map<std::string,Type>& subst);
  void validate_asm(const ast::AsmExpr& a,FnContext& ctx);
  void propagate_concurrency(ast::SmpMode mode);
  Diagnostics& diags_;
  SemanticModel model_;
};
}
