// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/mir.hpp"

namespace uinx {
struct TargetInfo {
  std::string triple;
  std::string data_layout;
  std::string arch;
  static TargetInfo from_triple(std::string triple);
};
class LLVMCodegen {
 public:
  LLVMCodegen(Diagnostics& d,TargetInfo target):diags_(d),target_(std::move(target)){}
  std::string emit(const mir::Module& module);
 private:
  std::string llvm_type(const Type& t) const;
  std::string llvm_name(std::string_view n) const;
  std::string escape_ir_string(std::string_view s) const;
  std::string escape_asm(std::string_view s) const;
  bool validate_constraint(std::string_view c,const SourceRange& r) const;
  void emit_function(std::ostream& os,const mir::Function& fn,const std::unordered_map<std::string,const mir::Function*>& functions) const;
  void emit_async_function(std::ostream& os,const mir::Function& fn,const std::unordered_map<std::string,const mir::Function*>& functions) const;
  void emit_inline_asm(std::ostream& os,const mir::Instruction& in,std::unordered_map<std::string,std::string>& values,std::unordered_map<std::string,Type>& value_types,std::size_t& asm_counter) const;
  Diagnostics& diags_;
  TargetInfo target_;
};
}
