// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/sema.hpp"

namespace uinx::mir {
enum class Op { Alloca, ConstInt, ConstFloat, ConstBool, ConstChar, ConstString, StructInit, Load, Store, AtomicRmw, Fence, CompilerFence, FieldAddr, IndexAddr, Binary, Compare, Call, Cast, AddressOf, Await, InlineAsm, Drop, Jump, CondJump, Return };
struct AsmOperand { ast::AsmOperand::Kind kind{}; std::string constraint; std::string value; std::string out_slot; Type type; };
struct Instruction {
  Op op{};
  std::string result;
  Type type;
  std::vector<std::string> args;
  std::string text;
  std::vector<AsmOperand> asm_operands;
  bool flag{false};
  SourceRange range{};
  Type auxiliary_type;
  Instruction(Op o=Op::Alloca,std::string r={},Type t={},std::vector<std::string> a={},std::string tx={},std::vector<AsmOperand> ao={},bool f=false,SourceRange sr={},Type aux={})
      : op(o),result(std::move(r)),type(std::move(t)),args(std::move(a)),text(std::move(tx)),asm_operands(std::move(ao)),flag(f),range(std::move(sr)),auxiliary_type(std::move(aux)) {}
};
struct BasicBlock { std::string label; std::vector<Instruction> instructions; bool terminated{false}; };
struct Parameter { std::string name; Type type; };
struct Function {
  std::string name;
  std::string source_name;
  std::vector<Parameter> params;
  Type result;
  std::vector<BasicBlock> blocks;
  bool is_extern{false};
  bool is_unsafe{false};
  bool is_async{false};
  std::string abi{"Uinx"};
};
struct StringConstant { std::string symbol; std::string value; };
struct Global { std::string name; Type type; std::string initializer; bool is_mut{false}; bool is_shared{false}; bool is_percpu{false}; };
struct Module { std::vector<Function> functions; std::vector<StringConstant> strings; std::vector<Global> globals; std::vector<const ast::StructDecl*> structs; };
}
namespace uinx {
class MIRLowerer {
 public:
  MIRLowerer(Diagnostics& d,const SemanticModel& m):diags_(d),model_(m){}
  mir::Module lower(const ast::Module& module);
 private:
  struct FnState {
    mir::Function fn;
    std::size_t block{};
    std::size_t temp_counter{};
    std::size_t slot_counter{};
    std::unordered_map<std::string,std::string> slots;
    std::unordered_map<std::string,Type> local_types;
    std::vector<std::vector<std::string>> scope_locals;
    std::unordered_map<std::string,Type> subst;
    std::vector<std::pair<std::size_t,std::size_t>> loop_targets;
    bool concurrent{false};
    ast::SmpMode smp_mode{ast::SmpMode::Auto};
  };
  struct Specialization { const ast::FunctionDecl* decl{}; std::unordered_map<std::string,Type> subst; std::string name; };
  std::string temp(FnState& s);
  std::string slot(FnState& s,std::string_view hint);
  mir::BasicBlock& block(FnState& s);
  std::size_t new_block(FnState& s,std::string label);
  void emit(FnState& s,mir::Instruction i);
  void lower_function(const ast::FunctionDecl& decl,const std::string& emitted_name,const std::unordered_map<std::string,Type>& subst={});
  void lower_block(const ast::BlockStmt& b,FnState& s);
  void lower_stmt(const ast::Stmt& st,FnState& s);
  std::pair<std::string,Type> lower_expr(const ast::Expr& e,FnState& s);
  std::string lower_place_address(const ast::Expr& e,FnState& s);
  Type expr_type(const ast::Expr& e,const FnState& s) const;
  Type subst_type(const Type& t,const FnState& s) const;
  std::string specialize_call(const ast::CallExpr& c,const FunctionSig& sig,FnState& s);
  void emit_scope_drops(FnState& s);
  bool place_is_atomic(const ast::Expr& e,const FnState& s) const;
  std::string load_order(const ast::Expr& e,const FnState& s) const;
  std::string store_order(const ast::Expr& e,const FnState& s) const;
  std::string rmw_order(const ast::Expr& e,const FnState& s) const;
  std::string intern_string(std::string value);
  Diagnostics& diags_;
  const SemanticModel& model_;
  mir::Module out_;
  std::vector<Specialization> pending_;
  std::unordered_set<std::string> emitted_;
  ast::SmpMode smp_mode_{ast::SmpMode::Auto};
};
class MIROptimizer {
 public:
  explicit MIROptimizer(int level):level_(level){}
  void run(mir::Module& module) const;
 private:
  void remove_unreachable(mir::Function& fn) const;
  void forward_local_loads(mir::Function& fn) const;
  void constant_fold(mir::Function& fn) const;
  void eliminate_dead_values(mir::Function& fn) const;
  int level_{};
};
}
