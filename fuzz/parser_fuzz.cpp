// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "uinx/lexer.hpp"
#include "uinx/parser.hpp"
#include <cstddef>
#include <cstdint>
#include <sstream>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,std::size_t size){
  std::string input(reinterpret_cast<const char*>(data),size);
  std::ostringstream sink;
  uinx::Diagnostics d(&sink);
  uinx::Lexer lexer("<fuzz>",input,d);
  auto tokens=lexer.lex();
  if(!d.has_errors()){
    uinx::Parser parser(std::move(tokens),d);
    (void)parser.parse_module("<fuzz>");
  }
  return 0;
}
