// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "codegen_internal.hpp"
#include "uinx/codegen.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace uinx {
using namespace codegen_detail;

void LLVMCodegen::emit_function(
    std::ostream& os,
    const mir::Function& fn,
    const std::unordered_map<std::string, const mir::Function*>& functions) const {
    const std::string return_type = fn.is_async ? "%uinx.future" : llvm_type(fn.result);
    const std::string name = llvm_name(fn.name);
    if (fn.is_extern) {
        os << "declare " << return_type << " @" << name << '(';
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (i)
                os << ", ";
            os << llvm_type(fn.params[i].type);
        }
        os << ")\n\n";
        return;
    }
    if (fn.is_async) {
        emit_async_function(os, fn, functions);
        return;
    }

    os << "define " << return_type << " @" << name << '(';
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i)
            os << ", ";
        os << llvm_type(fn.params[i].type) << " %arg." << llvm_name(fn.params[i].name);
    }
    os << ") {\n";

    std::unordered_map<std::string, std::string> values;
    std::unordered_map<std::string, Type> value_types;
    for (const auto& param : fn.params)
        value_types["%arg." + param.name] = param.type;
    std::size_t asm_counter = 0;
    std::size_t bounds_counter = 0;
    std::size_t atomic_counter = 0;
    for (const auto& block : fn.blocks) {
        os << llvm_name(block.label) << ":\n";
        for (const auto& instruction : block.instructions) {
            auto value_of = [&](const std::string& value) {
                const auto it = values.find(value);
                return it == values.end() ? value : it->second;
            };
            switch (instruction.op) {
                case mir::Op::Alloca:
                    os << "  " << instruction.result << " = alloca " << llvm_type(instruction.type)
                       << "\n";
                    value_types[instruction.result] = Type::raw_ptr(instruction.type, true);
                    break;
                case mir::Op::ConstInt:
                    values[instruction.result] = llvm_integer_constant(instruction.text);
                    value_types[instruction.result] = instruction.type;
                    break;
                case mir::Op::ConstFloat:
                case mir::Op::ConstBool:
                case mir::Op::ConstChar:
                    values[instruction.result] = instruction.text;
                    value_types[instruction.result] = instruction.type;
                    break;
                case mir::Op::ConstString:
                    values[instruction.result] = "getelementptr inbounds (i8, ptr @" +
                                                 llvm_name(instruction.text) + ", i64 0)";
                    value_types[instruction.result] = instruction.type;
                    break;
                case mir::Op::StructInit: {
                    if (instruction.args.empty()) {
                        values[instruction.result] = "zeroinitializer";
                        value_types[instruction.result] = instruction.type;
                        break;
                    }
                    std::string aggregate = "poison";
                    for (std::size_t index = 0; index < instruction.args.size(); ++index) {
                        const std::string next =
                            index + 1 == instruction.args.size()
                                ? instruction.result
                                : instruction.result + ".field." + std::to_string(index);
                        const Type arg_type = value_types.contains(instruction.args[index])
                                                  ? value_types[instruction.args[index]]
                                                  : Type::builtin("i32");
                        os << "  " << next << " = insertvalue " << llvm_type(instruction.type)
                           << ' ' << aggregate << ", " << llvm_type(arg_type) << ' '
                           << value_of(instruction.args[index]) << ", " << index << "\n";
                        aggregate = next;
                    }
                    values[instruction.result] = instruction.result;
                    value_types[instruction.result] = instruction.type;
                    break;
                }
                case mir::Op::Load:
                    os << "  " << instruction.result << " = load ";
                    if (instruction.flag)
                        os << "atomic ";
                    os << llvm_type(instruction.type) << ", ptr " << value_of(instruction.args[0]);
                    if (instruction.flag)
                        os << ' ' << instruction.text << ", align "
                           << type_alignment(instruction.type);
                    os << "\n";
                    values[instruction.result] = instruction.result;
                    value_types[instruction.result] = instruction.type;
                    break;
                case mir::Op::Store:
                    os << "  store ";
                    if (instruction.flag)
                        os << "atomic ";
                    os << llvm_type(instruction.type) << ' ' << value_of(instruction.args[0])
                       << ", ptr " << value_of(instruction.args[1]);
                    if (instruction.flag)
                        os << ' ' << instruction.text << ", align "
                           << type_alignment(instruction.type);
                    os << "\n";
                    break;
                case mir::Op::AtomicRmw: {
                    const std::string result = "%atomic." + std::to_string(atomic_counter++);
                    os << "  " << result << " = atomicrmw " << atomic_rmw_opcode(instruction.text)
                       << " ptr " << value_of(instruction.args[0]) << ", "
                       << llvm_type(instruction.type) << ' ' << value_of(instruction.args[1]) << ' '
                       << instruction.args[2] << ", align " << type_alignment(instruction.type)
                       << "\n";
                    break;
                }
                case mir::Op::Fence:
                    os << "  fence " << instruction.text << "\n";
                    break;
                case mir::Op::CompilerFence:
                    os << "  fence syncscope(\"singlethread\") " << instruction.text << "\n";
                    break;
                case mir::Op::FieldAddr:
                    os << "  " << instruction.result << " = getelementptr inbounds "
                       << llvm_type(instruction.auxiliary_type) << ", ptr "
                       << value_of(instruction.args[0]) << ", i32 0, i32 " << instruction.text
                       << "\n";
                    values[instruction.result] = instruction.result;
                    value_types[instruction.result] = Type::raw_ptr(instruction.type, true);
                    break;
                case mir::Op::IndexAddr: {
                    const std::string data = value_of(instruction.args[0]),
                                      index = value_of(instruction.args[1]),
                                      len = value_of(instruction.args[2]);
                    const int bits = integer_bits(instruction.auxiliary_type);
                    const bool is_unsigned = unsigned_integer(instruction.auxiliary_type.kind);
                    const std::string suffix = std::to_string(bounds_counter++);
                    std::string index64 = index, ok;
                    if (bits > 64) {
                        const std::string len_ext = "%bound.len." + suffix;
                        os << "  " << len_ext << " = zext i64 " << len << " to "
                           << llvm_type(instruction.auxiliary_type) << "\n";
                        const std::string lt = "%bound.lt." + suffix;
                        os << "  " << lt << " = icmp ult " << llvm_type(instruction.auxiliary_type)
                           << " " << index << ", " << len_ext << "\n";
                        ok = lt;
                        if (!is_unsigned) {
                            const std::string nonneg = "%bound.nonneg." + suffix;
                            os << "  " << nonneg << " = icmp sge "
                               << llvm_type(instruction.auxiliary_type) << " " << index << ", 0\n";
                            const std::string both = "%bound.ok." + suffix;
                            os << "  " << both << " = and i1 " << nonneg << ", " << lt << "\n";
                            ok = both;
                        }
                        index64 = "%bound.index." + suffix;
                        os << "  " << index64 << " = trunc "
                           << llvm_type(instruction.auxiliary_type) << " " << index << " to i64\n";
                    } else {
                        if (bits < 64) {
                            index64 = "%bound.index." + suffix;
                            os << "  " << index64 << " = " << (is_unsigned ? "zext" : "sext") << " "
                               << llvm_type(instruction.auxiliary_type) << " " << index
                               << " to i64\n";
                        }
                        ok = "%bound.ok." + suffix;
                        os << "  " << ok << " = icmp ult i64 " << index64 << ", " << len << "\n";
                    }
                    os << "  br i1 " << ok << ", label %bounds.ok." << suffix
                       << ", label %bounds.fail." << suffix << "\n";
                    os << "bounds.fail." << suffix
                       << ":\n  call void @llvm.trap()\n  unreachable\n";
                    os << "bounds.ok." << suffix << ":\n";
                    os << "  " << instruction.result << " = getelementptr inbounds "
                       << llvm_type(instruction.type) << ", ptr " << data << ", i64 " << index64
                       << "\n";
                    values[instruction.result] = instruction.result;
                    value_types[instruction.result] = Type::raw_ptr(instruction.type, true);
                    break;
                }
                case mir::Op::AddressOf:
                    values[instruction.result] = value_of(instruction.args[0]);
                    value_types[instruction.result] = instruction.type;
                    break;
                case mir::Op::Binary: {
                    if (instruction.type.kind == TypeKind::RawPtr &&
                        (instruction.text == "+" || instruction.text == "-") &&
                        instruction.args.size() == 2) {
                        const Type index_type = value_types.contains(instruction.args[1])
                                                    ? value_types.at(instruction.args[1])
                                                    : Type::builtin("isize");
                        std::string index = value_of(instruction.args[1]);
                        if (instruction.text == "-") {
                            const std::string neg = instruction.result + ".neg";
                            os << "  " << neg << " = sub " << llvm_type(index_type) << " 0, "
                               << index << "\n";
                            index = neg;
                        }
                        const Type element = instruction.type.pointee ? *instruction.type.pointee
                                                                      : Type::builtin("u8");
                        os << "  " << instruction.result << " = getelementptr "
                           << llvm_type(element) << ", ptr " << value_of(instruction.args[0])
                           << ", " << llvm_type(index_type) << ' ' << index << "\n";
                    } else if (instruction.text == "!" || instruction.text == "~") {
                        os << "  " << instruction.result << " = xor " << llvm_type(instruction.type)
                           << ' ' << value_of(instruction.args[0]) << ", "
                           << (instruction.type.kind == TypeKind::Bool ? "true" : "-1") << "\n";
                    } else {
                        os << "  " << instruction.result << " = "
                           << arithmetic_opcode(instruction.type, instruction.text) << ' '
                           << llvm_type(instruction.type) << ' ' << value_of(instruction.args[0])
                           << ", " << value_of(instruction.args[1]) << "\n";
                    }
                    values[instruction.result] = instruction.result;
                    value_types[instruction.result] = instruction.type;
                    break;
                }
                case mir::Op::Compare:
                    os << "  " << instruction.result << " = "
                       << (instruction.type.is_float() ? "fcmp " : "icmp ")
                       << compare_predicate(instruction.type, instruction.text) << ' '
                       << llvm_type(instruction.type) << ' ' << value_of(instruction.args[0])
                       << ", " << value_of(instruction.args[1]) << "\n";
                    values[instruction.result] = instruction.result;
                    value_types[instruction.result] = Type::builtin("bool");
                    break;
                case mir::Op::Call: {
                    if (auto arity = syscall_arity_from_name(instruction.text);
                        arity && instruction.args.size() == static_cast<std::size_t>(*arity) + 1) {
                        if (emit_syscall(os, instruction, values, value_types, asm_counter))
                            break;
                    }
                    const auto fit = functions.find(instruction.text);
                    std::vector<Type> params;
                    if (fit != functions.end())
                        for (const auto& param : fit->second->params)
                            params.push_back(param.type);

                    if ((instruction.text == "uinx_volatile_load_u8" ||
                         instruction.text == "uinx_volatile_load_u32" ||
                         instruction.text == "uinx_volatile_load_u64") &&
                        instruction.args.size() == 1) {
                        os << "  " << instruction.result << " = load volatile "
                           << llvm_type(instruction.type) << ", ptr "
                           << value_of(instruction.args[0]) << ", align "
                           << type_alignment(instruction.type) << "\n";
                        values[instruction.result] = instruction.result;
                        value_types[instruction.result] = instruction.type;
                        break;
                    }
                    if ((instruction.text == "uinx_volatile_store_u8" ||
                         instruction.text == "uinx_volatile_store_u32" ||
                         instruction.text == "uinx_volatile_store_u64") &&
                        instruction.args.size() == 2) {
                        const Type value_type =
                            params.size() > 1 ? params[1] : Type::builtin("u64");
                        os << "  store volatile " << llvm_type(value_type) << ' '
                           << value_of(instruction.args[1]) << ", ptr "
                           << value_of(instruction.args[0]) << ", align "
                           << type_alignment(value_type) << "\n";
                        break;
                    }
                    if (auto intrinsic = parse_atomic_intrinsic(instruction.text)) {
                        const std::string& llvm_ty = intrinsic->llvm_type;
                        const unsigned align = intrinsic->align;
                        const std::string& rmw = intrinsic->rmw_op;
                        if (rmw == "load" && instruction.args.size() == 2) {
                            if (auto order = atomic_order_from_abi(value_of(instruction.args[1]));
                                order && valid_atomic_load_order(*order)) {
                                os << "  " << instruction.result << " = load atomic " << llvm_ty
                                   << ", ptr " << value_of(instruction.args[0]) << ' ' << *order
                                   << ", align " << align << "\n";
                                values[instruction.result] = instruction.result;
                                value_types[instruction.result] = instruction.type;
                                break;
                            }
                        }
                        if (rmw == "store" && instruction.args.size() == 3) {
                            if (auto order = atomic_order_from_abi(value_of(instruction.args[2]));
                                order && valid_atomic_store_order(*order)) {
                                os << "  store atomic " << llvm_ty << ' '
                                   << value_of(instruction.args[1]) << ", ptr "
                                   << value_of(instruction.args[0]) << ' ' << *order << ", align "
                                   << align << "\n";
                                break;
                            }
                        }
                        if ((rmw == "add" || rmw == "sub" || rmw == "and" || rmw == "or" ||
                             rmw == "xor" || rmw == "xchg") &&
                            instruction.args.size() == 3) {
                            if (auto order = atomic_order_from_abi(value_of(instruction.args[2]));
                                order && valid_atomic_rmw_order(*order)) {
                                os << "  " << instruction.result << " = atomicrmw " << rmw
                                   << " ptr " << value_of(instruction.args[0]) << ", " << llvm_ty
                                   << ' ' << value_of(instruction.args[1]) << ' ' << *order
                                   << ", align " << align << "\n";
                                values[instruction.result] = instruction.result;
                                value_types[instruction.result] = instruction.type;
                                break;
                            }
                        }
                        if (rmw == "cmpxchg" && instruction.args.size() == 5) {
                            auto success = atomic_order_from_abi(value_of(instruction.args[3]));
                            auto failure = atomic_order_from_abi(value_of(instruction.args[4]));
                            if (success && failure && valid_atomic_rmw_order(*success) &&
                                (valid_atomic_load_order(*failure) || *failure == "monotonic")) {
                                const std::string failure_fixed =
                                    downgrade_cmpxchg_failure(*success, *failure);
                                const std::string id = std::to_string(atomic_counter++);
                                const std::string expected = "%cas.expected." + id;
                                const std::string pair = "%cas.pair." + id;
                                const std::string old = "%cas.old." + id;
                                const std::string ok = "%cas.ok." + id;
                                const std::string result = instruction.result.empty()
                                                               ? "%cas.result." + id
                                                               : instruction.result;
                                os << "  " << expected << " = load " << llvm_ty << ", ptr "
                                   << value_of(instruction.args[1]) << ", align " << align << "\n";
                                os << "  " << pair << " = cmpxchg ptr "
                                   << value_of(instruction.args[0]) << ", " << llvm_ty << ' '
                                   << expected << ", " << llvm_ty << ' '
                                   << value_of(instruction.args[2]) << ' ' << *success << ' '
                                   << failure_fixed << ", align " << align << "\n";
                                os << "  " << old << " = extractvalue { " << llvm_ty << ", i1 } "
                                   << pair << ", 0\n";
                                os << "  " << ok << " = extractvalue { " << llvm_ty << ", i1 } "
                                   << pair << ", 1\n";
                                os << "  store " << llvm_ty << ' ' << old << ", ptr "
                                   << value_of(instruction.args[1]) << ", align " << align << "\n";
                                os << "  " << result << " = zext i1 " << ok << " to i32\n";
                                if (!instruction.result.empty()) {
                                    values[instruction.result] = result;
                                    value_types[instruction.result] = instruction.type;
                                }
                                break;
                            }
                        }
                        // Invalid ordering falls through to runtime call (fail-closed
                        // is handled by sema/codegen validation elsewhere).
                    }

                    os << "  ";
                    if (!instruction.result.empty())
                        os << instruction.result << " = ";
                    os << "call " << llvm_type(instruction.type) << " @"
                       << llvm_name(instruction.text) << '(';
                    for (std::size_t i = 0; i < instruction.args.size(); ++i) {
                        if (i)
                            os << ", ";
                        const Type arg_type = i < params.size()
                                                  ? params[i]
                                                  : (value_types.contains(instruction.args[i])
                                                         ? value_types[instruction.args[i]]
                                                         : Type::builtin("i32"));
                        os << llvm_type(arg_type) << ' ' << value_of(instruction.args[i]);
                    }
                    os << ")\n";
                    if (!instruction.result.empty()) {
                        values[instruction.result] = instruction.result;
                        value_types[instruction.result] = instruction.type;
                    }
                    break;
                }
                case mir::Op::Cast: {
                    const Type from = value_types.contains(instruction.args[0])
                                          ? value_types[instruction.args[0]]
                                          : Type::builtin("i32");
                    if ((from.kind == TypeKind::Str && instruction.type.kind == TypeKind::RawPtr) ||
                        ((from.kind == TypeKind::Ref || from.kind == TypeKind::RawPtr) &&
                         (instruction.type.kind == TypeKind::Ref ||
                          instruction.type.kind == TypeKind::RawPtr))) {
                        values[instruction.result] = value_of(instruction.args[0]);
                        value_types[instruction.result] = instruction.type;
                        break;
                    }
                    std::string opcode;
                    if (from.kind == TypeKind::RawPtr && instruction.type.is_integer())
                        opcode = "ptrtoint";
                    else if (from.is_integer() && instruction.type.kind == TypeKind::RawPtr)
                        opcode = "inttoptr";
                    else if (from.is_integer() && instruction.type.is_integer()) {
                        const int from_bits = integer_bits(from),
                                  to_bits = integer_bits(instruction.type);
                        opcode = from_bits < to_bits
                                     ? (unsigned_integer(from.kind) ? "zext" : "sext")
                                 : from_bits > to_bits ? "trunc"
                                                       : "bitcast";
                    } else if (from.kind == TypeKind::Bool && instruction.type.is_integer()) {
                        opcode = "zext";
                    } else if (from.is_integer() && instruction.type.is_float())
                        opcode = unsigned_integer(from.kind) ? "uitofp" : "sitofp";
                    else if (from.is_float() && instruction.type.is_integer())
                        opcode = unsigned_integer(instruction.type.kind) ? "fptoui" : "fptosi";
                    else if (from.is_float() && instruction.type.is_float())
                        opcode = from.kind == TypeKind::F32 ? "fpext" : "fptrunc";
                    else
                        opcode = "bitcast";
                    os << "  " << instruction.result << " = " << opcode << ' ' << llvm_type(from)
                       << ' ' << value_of(instruction.args[0]) << " to "
                       << llvm_type(instruction.type) << "\n";
                    values[instruction.result] = instruction.result;
                    value_types[instruction.result] = instruction.type;
                    break;
                }
                case mir::Op::Await:
                    diags_.error(instruction.range,
                                 "E0602",
                                 "internal: await reached synchronous LLVM lowering");
                    break;
                case mir::Op::InlineAsm:
                    emit_inline_asm(os, instruction, values, value_types, asm_counter);
                    break;
                case mir::Op::Drop:
                    os << "  call void @" << llvm_name(instruction.type.name + "__drop") << "(ptr "
                       << value_of(instruction.args[0]) << ")\n";
                    break;
                case mir::Op::Jump:
                    os << "  br label %" << llvm_name(instruction.args[0]) << "\n";
                    break;
                case mir::Op::CondJump:
                    os << "  br i1 " << value_of(instruction.args[0]) << ", label %"
                       << llvm_name(instruction.args[1]) << ", label %"
                       << llvm_name(instruction.args[2]) << "\n";
                    break;
                case mir::Op::Return:
                    if (instruction.args.empty())
                        os << "  ret void\n";
                    else
                        os << "  ret " << llvm_type(instruction.type) << ' '
                           << value_of(instruction.args[0]) << "\n";
                    break;
            }
        }
    }
    os << "}\n\n";
}
} // namespace uinx
