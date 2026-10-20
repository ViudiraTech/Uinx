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

void LLVMCodegen::emit_async_function(
    std::ostream& os,
    const mir::Function& fn,
    const std::unordered_map<std::string, const mir::Function*>& functions) const {
    struct FrameField {
        Type type;
        std::size_t index{};
        bool address{false};
    };

    std::unordered_map<std::string, FrameField> fields;
    std::vector<std::pair<std::string, FrameField>> ordered;
    std::size_t next_field = 1;
    auto add_field = [&](const std::string& key, Type type, bool address) {
        if (key.empty() || fields.contains(key))
            return;
        FrameField field{std::move(type), next_field++, address};
        fields.emplace(key, field);
        ordered.push_back({key, field});
    };

    for (const auto& param : fn.params)
        add_field("%arg." + param.name, param.type, false);
    std::vector<std::pair<const mir::Instruction*, std::size_t>> awaits;
    std::size_t await_count = 0;
    for (const auto& block : fn.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.op == mir::Op::Await)
                awaits.push_back({&instruction, ++await_count});
            if (instruction.result.empty())
                continue;
            if (instruction.op == mir::Op::Alloca)
                add_field(instruction.result, instruction.type, true);
            else if (instruction.op == mir::Op::FieldAddr || instruction.op == mir::Op::IndexAddr)
                add_field(instruction.result, Type::raw_ptr(instruction.type, true), false);
            else
                add_field(instruction.result, instruction.type, false);
        }
    }

    const std::string name = llvm_name(fn.name);
    const std::string frame_type = "%uinx.async.frame." + name;
    os << frame_type << " = type { i32";
    for (const auto& [key, field] : ordered) {
        (void)key;
        os << ", " << llvm_type(field.type);
    }
    os << " }\n";
    os << "@" << name << "__vtable = private constant %uinx.future.vtable { ptr @" << name
       << "__poll, ptr @" << name << "__drop }\n";

    os << "define %uinx.future @" << name << '(';
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i)
            os << ", ";
        os << llvm_type(fn.params[i].type) << " %arg." << llvm_name(fn.params[i].name);
    }
    os << ") {\nentry:\n";
    os << "  %frame.size.ptr = getelementptr " << frame_type << ", ptr null, i32 1\n";
    os << "  %frame.size = ptrtoint ptr %frame.size.ptr to i64\n";
    os << "  %frame = call ptr @malloc(i64 %frame.size)\n";
    os << "  %oom = icmp eq ptr %frame, null\n";
    os << "  br i1 %oom, label %async.oom, label %async.init\n";
    os << "async.oom:\n  call void @abort()\n  unreachable\n";
    os << "async.init:\n";
    os << "  %state.addr = getelementptr inbounds " << frame_type << ", ptr %frame, i32 0, i32 0\n";
    os << "  store i32 0, ptr %state.addr\n";
    for (const auto& param : fn.params) {
        const auto& field = fields.at("%arg." + param.name);
        os << "  %param.addr." << field.index << " = getelementptr inbounds " << frame_type
           << ", ptr %frame, i32 0, i32 " << field.index << "\n";
        os << "  store " << llvm_type(param.type) << " %arg." << llvm_name(param.name)
           << ", ptr %param.addr." << field.index << "\n";
    }
    os << "  %future.0 = insertvalue %uinx.future poison, ptr %frame, 0\n";
    os << "  %future.1 = insertvalue %uinx.future %future.0, ptr @" << name << "__vtable, 1\n";
    os << "  ret %uinx.future %future.1\n}\n\n";

    os << "define i32 @" << name << "__poll(ptr %frame, ptr %output) {\nentry:\n";
    os << "  %state.addr = getelementptr inbounds " << frame_type << ", ptr %frame, i32 0, i32 0\n";
    os << "  %state = load i32, ptr %state.addr\n";
    const std::string first_label = fn.blocks.empty() ? "entry" : fn.blocks.front().label;
    os << "  switch i32 %state, label %async.invalid [ i32 0, label %mir."
       << llvm_name(first_label);
    for (const auto& [instruction, id] : awaits) {
        (void)instruction;
        os << " i32 " << id << ", label %async.resume." << id;
    }
    os << " i32 -1, label %async.completed ]\n";
    os << "async.invalid:\n  ret i32 -22\n";
    os << "async.completed:\n  ret i32 1\n";
    for (const auto& [instruction, id] : awaits) {
        (void)instruction;
        os << "async.resume." << id << ":\n  br label %async.await." << id << ".poll\n";
    }

    std::size_t temp_counter = 0;
    std::size_t asm_counter = 0;
    std::size_t bounds_counter = 0;
    std::size_t current_await = 0;
    std::unordered_map<std::string, Type> value_types;
    for (const auto& [key, field] : ordered)
        value_types[key] = field.type;

    auto frame_addr = [&](const std::string& key) -> std::string {
        const auto it = fields.find(key);
        if (it == fields.end())
            return key;
        const std::string temp = "%af.addr." + std::to_string(temp_counter++);
        os << "  " << temp << " = getelementptr inbounds " << frame_type
           << ", ptr %frame, i32 0, i32 " << it->second.index << "\n";
        return temp;
    };
    auto value_of = [&](const std::string& value) -> std::string {
        const auto it = fields.find(value);
        if (it == fields.end())
            return value;
        const std::string address = frame_addr(value);
        if (it->second.address)
            return address;
        const std::string temp = "%af.load." + std::to_string(temp_counter++);
        os << "  " << temp << " = load " << llvm_type(it->second.type) << ", ptr " << address
           << "\n";
        return temp;
    };
    auto store_result = [&](const mir::Instruction& instruction, const std::string& value) {
        if (instruction.result.empty())
            return;
        const auto it = fields.find(instruction.result);
        if (it == fields.end() || it->second.address)
            return;
        const std::string address = frame_addr(instruction.result);
        os << "  store " << llvm_type(it->second.type) << ' ' << value << ", ptr " << address
           << "\n";
    };
    auto mir_label = [&](const std::string& label) { return "mir." + llvm_name(label); };

    for (const auto& block : fn.blocks) {
        os << mir_label(block.label) << ":\n";
        for (const auto& instruction : block.instructions) {
            switch (instruction.op) {
                case mir::Op::Alloca:
                    break;
                case mir::Op::ConstInt:
                    store_result(instruction, llvm_integer_constant(instruction.text));
                    break;
                case mir::Op::ConstFloat:
                case mir::Op::ConstBool:
                case mir::Op::ConstChar:
                    store_result(instruction, instruction.text);
                    break;
                case mir::Op::ConstString: {
                    store_result(instruction,
                                 "getelementptr inbounds (i8, ptr @" + llvm_name(instruction.text) +
                                     ", i64 0)");
                    break;
                }
                case mir::Op::StructInit: {
                    if (instruction.args.empty()) {
                        store_result(instruction, "zeroinitializer");
                        break;
                    }
                    std::string aggregate = "poison";
                    for (std::size_t index = 0; index < instruction.args.size(); ++index) {
                        const std::string arg = value_of(instruction.args[index]);
                        const Type arg_type = value_types.contains(instruction.args[index])
                                                  ? value_types[instruction.args[index]]
                                                  : Type::builtin("i32");
                        const std::string next = "%af.insert." + std::to_string(temp_counter++);
                        os << "  " << next << " = insertvalue " << llvm_type(instruction.type)
                           << ' ' << aggregate << ", " << llvm_type(arg_type) << ' ' << arg << ", "
                           << index << "\n";
                        aggregate = next;
                    }
                    store_result(instruction, aggregate);
                    break;
                }
                case mir::Op::Load: {
                    const std::string address = value_of(instruction.args[0]);
                    const std::string result = "%af.op." + std::to_string(temp_counter++);
                    os << "  " << result << " = load ";
                    if (instruction.flag)
                        os << "atomic ";
                    os << llvm_type(instruction.type) << ", ptr " << address;
                    if (instruction.flag)
                        os << ' ' << instruction.text << ", align "
                           << type_alignment(instruction.type);
                    os << "\n";
                    store_result(instruction, result);
                    break;
                }
                case mir::Op::Store: {
                    const std::string value = value_of(instruction.args[0]);
                    const std::string address = value_of(instruction.args[1]);
                    os << "  store ";
                    if (instruction.flag)
                        os << "atomic ";
                    os << llvm_type(instruction.type) << ' ' << value << ", ptr " << address;
                    if (instruction.flag)
                        os << ' ' << instruction.text << ", align "
                           << type_alignment(instruction.type);
                    os << "\n";
                    break;
                }
                case mir::Op::AtomicRmw: {
                    const std::string address = value_of(instruction.args[0]);
                    const std::string value = value_of(instruction.args[1]);
                    const std::string result = "%af.atomic." + std::to_string(temp_counter++);
                    os << "  " << result << " = atomicrmw " << atomic_rmw_opcode(instruction.text)
                       << " ptr " << address << ", " << llvm_type(instruction.type) << ' ' << value
                       << ' ' << instruction.args[2] << ", align "
                       << type_alignment(instruction.type) << "\n";
                    break;
                }
                case mir::Op::Fence:
                    os << "  fence " << instruction.text << "\n";
                    break;
                case mir::Op::CompilerFence:
                    os << "  fence syncscope(\"singlethread\") " << instruction.text << "\n";
                    break;
                case mir::Op::FieldAddr: {
                    const std::string base = value_of(instruction.args[0]);
                    const std::string result = "%af.field." + std::to_string(temp_counter++);
                    os << "  " << result << " = getelementptr inbounds "
                       << llvm_type(instruction.auxiliary_type) << ", ptr " << base
                       << ", i32 0, i32 " << instruction.text << "\n";
                    store_result(instruction, result);
                    break;
                }
                case mir::Op::IndexAddr: {
                    const std::string data = value_of(instruction.args[0]),
                                      index = value_of(instruction.args[1]),
                                      len = value_of(instruction.args[2]);
                    const int bits = integer_bits(instruction.auxiliary_type);
                    const bool is_unsigned = unsigned_integer(instruction.auxiliary_type.kind);
                    const std::string suffix = std::to_string(bounds_counter++);
                    std::string index64 = index, ok;
                    if (bits > 64) {
                        const std::string len_ext = "%af.bound.len." + suffix;
                        os << "  " << len_ext << " = zext i64 " << len << " to "
                           << llvm_type(instruction.auxiliary_type) << "\n";
                        const std::string lt = "%af.bound.lt." + suffix;
                        os << "  " << lt << " = icmp ult " << llvm_type(instruction.auxiliary_type)
                           << " " << index << ", " << len_ext << "\n";
                        ok = lt;
                        if (!is_unsigned) {
                            const std::string nonneg = "%af.bound.nonneg." + suffix;
                            os << "  " << nonneg << " = icmp sge "
                               << llvm_type(instruction.auxiliary_type) << " " << index << ", 0\n";
                            const std::string both = "%af.bound.ok." + suffix;
                            os << "  " << both << " = and i1 " << nonneg << ", " << lt << "\n";
                            ok = both;
                        }
                        index64 = "%af.bound.index." + suffix;
                        os << "  " << index64 << " = trunc "
                           << llvm_type(instruction.auxiliary_type) << " " << index << " to i64\n";
                    } else {
                        if (bits < 64) {
                            index64 = "%af.bound.index." + suffix;
                            os << "  " << index64 << " = " << (is_unsigned ? "zext" : "sext") << " "
                               << llvm_type(instruction.auxiliary_type) << " " << index
                               << " to i64\n";
                        }
                        ok = "%af.bound.ok." + suffix;
                        os << "  " << ok << " = icmp ult i64 " << index64 << ", " << len << "\n";
                    }
                    os << "  br i1 " << ok << ", label %async.bounds.ok." << suffix
                       << ", label %async.bounds.fail." << suffix << "\n";
                    os << "async.bounds.fail." << suffix
                       << ":\n  call void @llvm.trap()\n  unreachable\n";
                    os << "async.bounds.ok." << suffix << ":\n";
                    const std::string result = "%af.index." + suffix;
                    os << "  " << result << " = getelementptr inbounds "
                       << llvm_type(instruction.type) << ", ptr " << data << ", i64 " << index64
                       << "\n";
                    store_result(instruction, result);
                    break;
                }
                case mir::Op::AddressOf:
                    store_result(instruction, value_of(instruction.args[0]));
                    break;
                case mir::Op::Binary: {
                    const std::string lhs = value_of(instruction.args[0]);
                    const std::string result = "%af.bin." + std::to_string(temp_counter++);
                    if (instruction.type.kind == TypeKind::RawPtr &&
                        (instruction.text == "+" || instruction.text == "-") &&
                        instruction.args.size() == 2) {
                        const std::string rhs = value_of(instruction.args[1]);
                        const Type index_type = value_types.contains(instruction.args[1])
                                                    ? value_types.at(instruction.args[1])
                                                    : Type::builtin("isize");
                        std::string index = rhs;
                        if (instruction.text == "-") {
                            index = "%af.ptr.neg." + std::to_string(temp_counter++);
                            os << "  " << index << " = sub " << llvm_type(index_type) << " 0, "
                               << rhs << "\n";
                        }
                        const Type element = instruction.type.pointee ? *instruction.type.pointee
                                                                      : Type::builtin("u8");
                        os << "  " << result << " = getelementptr " << llvm_type(element)
                           << ", ptr " << lhs << ", " << llvm_type(index_type) << ' ' << index
                           << "\n";
                    } else if (instruction.text == "!" || instruction.text == "~") {
                        os << "  " << result << " = xor " << llvm_type(instruction.type) << ' '
                           << lhs << ", "
                           << (instruction.type.kind == TypeKind::Bool ? "true" : "-1") << "\n";
                    } else {
                        const std::string rhs = value_of(instruction.args[1]);
                        os << "  " << result << " = "
                           << arithmetic_opcode(instruction.type, instruction.text) << ' '
                           << llvm_type(instruction.type) << ' ' << lhs << ", " << rhs << "\n";
                    }
                    store_result(instruction, result);
                    break;
                }
                case mir::Op::Compare: {
                    const std::string lhs = value_of(instruction.args[0]);
                    const std::string rhs = value_of(instruction.args[1]);
                    const std::string result = "%af.cmp." + std::to_string(temp_counter++);
                    os << "  " << result << " = "
                       << (instruction.type.is_float() ? "fcmp " : "icmp ")
                       << compare_predicate(instruction.type, instruction.text) << ' '
                       << llvm_type(instruction.type) << ' ' << lhs << ", " << rhs << "\n";
                    store_result(instruction, result);
                    break;
                }
                case mir::Op::Call: {
                    if (auto arity = syscall_arity_from_name(instruction.text);
                        arity && instruction.args.size() == static_cast<std::size_t>(*arity) + 1) {
                        std::unordered_map<std::string, std::string> syscall_values;
                        for (const auto& arg : instruction.args)
                            syscall_values[arg] = value_of(arg);
                        if (emit_syscall(
                                os, instruction, syscall_values, value_types, temp_counter)) {
                            if (!instruction.result.empty()) {
                                const auto it = syscall_values.find(instruction.result);
                                if (it != syscall_values.end())
                                    store_result(instruction, it->second);
                            }
                            break;
                        }
                    }
                    const auto fit = functions.find(instruction.text);
                    std::vector<Type> parameter_types;
                    if (fit != functions.end())
                        for (const auto& param : fit->second->params)
                            parameter_types.push_back(param.type);
                    std::vector<std::string> args;
                    for (const auto& arg : instruction.args)
                        args.push_back(value_of(arg));
                    const std::string result = instruction.result.empty()
                                                   ? ""
                                                   : "%af.call." + std::to_string(temp_counter++);
                    os << "  ";
                    if (!result.empty())
                        os << result << " = ";
                    os << "call " << llvm_type(instruction.type) << " @"
                       << llvm_name(instruction.text) << '(';
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        if (i)
                            os << ", ";
                        const Type arg_type = i < parameter_types.size()
                                                  ? parameter_types[i]
                                                  : (value_types.contains(instruction.args[i])
                                                         ? value_types[instruction.args[i]]
                                                         : Type::builtin("i32"));
                        os << llvm_type(arg_type) << ' ' << args[i];
                    }
                    os << ")\n";
                    if (!result.empty())
                        store_result(instruction, result);
                    break;
                }
                case mir::Op::Cast: {
                    const std::string value = value_of(instruction.args[0]);
                    const Type from = value_types.contains(instruction.args[0])
                                          ? value_types[instruction.args[0]]
                                          : Type::builtin("i32");
                    if (from.kind == TypeKind::Str && instruction.type.kind == TypeKind::RawPtr) {
                        store_result(instruction, value);
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
                    const std::string result = "%af.cast." + std::to_string(temp_counter++);
                    os << "  " << result << " = " << opcode << ' ' << llvm_type(from) << ' '
                       << value << " to " << llvm_type(instruction.type) << "\n";
                    store_result(instruction, result);
                    break;
                }
                case mir::Op::Await: {
                    const std::size_t id = ++current_await;
                    os << "  br label %async.await." << id << ".poll\n";
                    os << "async.await." << id << ".poll:\n";
                    const std::string future = value_of(instruction.args[0]);
                    const std::string child_state = "%await.state." + std::to_string(id);
                    const std::string vtable = "%await.vtable." + std::to_string(id);
                    const std::string poll_addr = "%await.poll.addr." + std::to_string(id);
                    const std::string drop_addr = "%await.drop.addr." + std::to_string(id);
                    const std::string poll = "%await.pollfn." + std::to_string(id);
                    const std::string drop = "%await.dropfn." + std::to_string(id);
                    os << "  " << child_state << " = extractvalue %uinx.future " << future
                       << ", 0\n";
                    os << "  " << vtable << " = extractvalue %uinx.future " << future << ", 1\n";
                    os << "  " << poll_addr << " = getelementptr inbounds %uinx.future.vtable, ptr "
                       << vtable << ", i32 0, i32 0\n";
                    os << "  " << drop_addr << " = getelementptr inbounds %uinx.future.vtable, ptr "
                       << vtable << ", i32 0, i32 1\n";
                    os << "  " << poll << " = load ptr, ptr " << poll_addr << "\n";
                    os << "  " << drop << " = load ptr, ptr " << drop_addr << "\n";
                    const std::string output = "%await.out." + std::to_string(id);
                    os << "  " << output << " = alloca " << llvm_type(instruction.type) << "\n";
                    const std::string rc = "%await.rc." + std::to_string(id);
                    os << "  " << rc << " = call i32 " << poll << "(ptr " << child_state << ", ptr "
                       << output << ")\n";
                    const std::string pending = "%await.pending." + std::to_string(id);
                    os << "  " << pending << " = icmp eq i32 " << rc << ", 0\n";
                    os << "  br i1 " << pending << ", label %async.await." << id
                       << ".pending, label %async.await." << id << ".nonpending\n";
                    os << "async.await." << id << ".pending:\n";
                    os << "  store i32 " << id << ", ptr %state.addr\n";
                    os << "  ret i32 0\n";
                    os << "async.await." << id << ".nonpending:\n";
                    const std::string failed = "%await.failed." + std::to_string(id);
                    os << "  " << failed << " = icmp slt i32 " << rc << ", 0\n";
                    os << "  br i1 " << failed << ", label %async.await." << id
                       << ".error, label %async.await." << id << ".ready\n";
                    os << "async.await." << id << ".error:\n  ret i32 " << rc << "\n";
                    os << "async.await." << id << ".ready:\n";
                    const std::string ready = "%await.value." + std::to_string(id);
                    os << "  " << ready << " = load " << llvm_type(instruction.type) << ", ptr "
                       << output << "\n";
                    os << "  call void " << drop << "(ptr " << child_state << ")\n";
                    os << "  store i32 0, ptr %state.addr\n";
                    store_result(instruction, ready);
                    os << "  br label %async.await." << id << ".cont\n";
                    os << "async.await." << id << ".cont:\n";
                    break;
                }
                case mir::Op::InlineAsm: {
                    mir::Instruction copy = instruction;
                    std::unordered_map<std::string, std::string> values;
                    for (auto& operand : copy.asm_operands) {
                        if (!operand.value.empty())
                            values[operand.value] = value_of(operand.value);
                        if (!operand.out_slot.empty())
                            operand.out_slot = value_of(operand.out_slot);
                    }
                    emit_inline_asm(os, copy, values, value_types, asm_counter);
                    break;
                }
                case mir::Op::Drop: {
                    const std::string address = value_of(instruction.args[0]);
                    os << "  call void @" << llvm_name(instruction.type.name + "__drop") << "(ptr "
                       << address << ")\n";
                    break;
                }
                case mir::Op::Jump:
                    os << "  br label %" << mir_label(instruction.args[0]) << "\n";
                    break;
                case mir::Op::CondJump: {
                    const std::string condition = value_of(instruction.args[0]);
                    os << "  br i1 " << condition << ", label %" << mir_label(instruction.args[1])
                       << ", label %" << mir_label(instruction.args[2]) << "\n";
                    break;
                }
                case mir::Op::Return: {
                    if (!instruction.args.empty()) {
                        const std::string value = value_of(instruction.args[0]);
                        os << "  store " << llvm_type(instruction.type) << ' ' << value
                           << ", ptr %output\n";
                    }
                    os << "  store i32 -1, ptr %state.addr\n";
                    os << "  ret i32 1\n";
                    break;
                }
            }
        }
    }
    os << "}\n\n";

    os << "define void @" << name << "__drop(ptr %frame) {\nentry:\n";
    os << "  %state.addr = getelementptr inbounds " << frame_type << ", ptr %frame, i32 0, i32 0\n";
    os << "  %state = load i32, ptr %state.addr\n";
    if (awaits.empty()) {
        os << "  br label %free\n";
    } else {
        os << "  switch i32 %state, label %free [";
        for (const auto& [instruction, id] : awaits) {
            (void)instruction;
            os << " i32 " << id << ", label %cleanup." << id;
        }
        os << " ]\n";
        for (const auto& [instruction, id] : awaits) {
            os << "cleanup." << id << ":\n";
            if (!instruction->args.empty()) {
                const auto fit = fields.find(instruction->args[0]);
                if (fit != fields.end()) {
                    os << "  %c.addr." << id << " = getelementptr inbounds " << frame_type
                       << ", ptr %frame, i32 0, i32 " << fit->second.index << "\n";
                    os << "  %c.future." << id << " = load %uinx.future, ptr %c.addr." << id
                       << "\n";
                    os << "  %c.state." << id << " = extractvalue %uinx.future %c.future." << id
                       << ", 0\n";
                    os << "  %c.vtable." << id << " = extractvalue %uinx.future %c.future." << id
                       << ", 1\n";
                    os << "  %c.drop.addr." << id
                       << " = getelementptr inbounds %uinx.future.vtable, ptr %c.vtable." << id
                       << ", i32 0, i32 1\n";
                    os << "  %c.drop." << id << " = load ptr, ptr %c.drop.addr." << id << "\n";
                    os << "  call void %c.drop." << id << "(ptr %c.state." << id << ")\n";
                }
            }
            os << "  br label %free\n";
        }
    }
    os << "free:\n  call void @free(ptr %frame)\n  ret void\n}\n\n";
}
} // namespace uinx
