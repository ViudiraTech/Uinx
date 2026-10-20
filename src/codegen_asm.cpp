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

bool LLVMCodegen::validate_constraint(std::string_view constraint, const SourceRange& range) const {
    std::string raw(constraint);
    while (!raw.empty() && std::string("=+&~").find(raw.front()) != std::string::npos)
        raw.erase(raw.begin());
    if (raw == "memory" || raw == "cc" || raw == "r" || raw == "m" || raw == "i" || raw == "g")
        return true;
    if (raw.size() >= 2 && raw.front() == '{' && raw.back() == '}')
        raw = raw.substr(1, raw.size() - 2);

    static const std::unordered_set<std::string> x86 = {
        "rax",  "rbx",  "rcx",  "rdx",   "rsi",   "rdi",   "rbp",   "rsp",   "r8",
        "r9",   "r10",  "r11",  "r12",   "r13",   "r14",   "r15",   "eax",   "ebx",
        "ecx",  "edx",  "xmm0", "xmm1",  "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",
        "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"};
    auto numbered = [&](char prefix, int max) {
        if (raw.size() < 2 || raw.front() != prefix)
            return false;
        try {
            const int n = std::stoi(raw.substr(1));
            return n >= 0 && n <= max;
        } catch (...) {
            return false;
        }
    };

    bool valid = false;
    if (target_.arch == "x86_64") {
        valid = x86.contains(raw);
    } else if (target_.arch == "aarch64") {
        valid = numbered('x', 30) || numbered('w', 30) || numbered('v', 31) || raw == "sp";
    } else if (target_.arch == "riscv64") {
        valid = numbered('x', 31) || raw == "ra" || raw == "sp" || raw == "gp" || raw == "tp" ||
                raw == "a0" || raw == "a1" || raw == "a2" || raw == "a3" || raw == "a4" ||
                raw == "a5" || raw == "a6" || raw == "a7" || raw == "s0" || raw == "s1" ||
                raw == "t0" || raw == "t1" || raw == "t2";
    } else {
        valid = true;
    }
    if (!valid) {
        diags_.error(range,
                     "E0600",
                     "asm constraint/register '" + std::string(constraint) +
                         "' is not valid for target architecture '" + target_.arch + "'");
    }
    return valid;
}

void LLVMCodegen::emit_inline_asm(std::ostream& os,
                                  const mir::Instruction& in,
                                  std::unordered_map<std::string, std::string>& values,
                                  std::unordered_map<std::string, Type>& value_types,
                                  std::size_t& counter) const {
    std::vector<const mir::AsmOperand*> outputs;
    std::vector<const mir::AsmOperand*> inputs;
    std::vector<const mir::AsmOperand*> clobbers;
    for (const auto& operand : in.asm_operands) {
        validate_constraint(operand.constraint, in.range);
        if (operand.kind == ast::AsmOperand::Kind::Out ||
            operand.kind == ast::AsmOperand::Kind::InOut)
            outputs.push_back(&operand);
        else if (operand.kind == ast::AsmOperand::Kind::In)
            inputs.push_back(&operand);
        else
            clobbers.push_back(&operand);
    }

    std::vector<std::string> constraints;
    std::vector<std::pair<Type, std::string>> call_args;
    for (const auto* output : outputs) {
        std::string c = output->constraint;
        while (!c.empty() && c.front() == '+')
            c.erase(c.begin());
        if (c.empty() || c.front() != '=')
            c = "=" + c;
        constraints.push_back(std::move(c));
    }
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        const auto* output = outputs[index];
        if (output->kind != ast::AsmOperand::Kind::InOut)
            continue;
        std::string initial = "undef";
        if (!output->out_slot.empty()) {
            initial = "%asm.inout." + std::to_string(counter) + "." + std::to_string(index);
            os << "  " << initial << " = load " << llvm_type(output->type) << ", ptr "
               << output->out_slot << "\n";
        } else if (!output->value.empty()) {
            const auto it = values.find(output->value);
            initial = it == values.end() ? output->value : it->second;
        }
        constraints.push_back(std::to_string(index));
        call_args.push_back({output->type, initial});
    }
    for (const auto* input : inputs) {
        constraints.push_back(input->constraint);
        const auto it = values.find(input->value);
        call_args.push_back({input->type, it == values.end() ? input->value : it->second});
    }
    for (const auto* clobber : clobbers) {
        if (clobber->constraint == "memory")
            constraints.push_back("~{memory}");
        else if (clobber->constraint == "cc")
            constraints.push_back("~{cc}");
        else {
            std::string reg = clobber->constraint;
            if (reg.front() == '{' && reg.back() == '}')
                constraints.push_back("~" + reg);
            else
                constraints.push_back("~{" + reg + "}");
        }
    }

    std::string return_type = "void";
    if (outputs.size() == 1)
        return_type = llvm_type(outputs.front()->type);
    else if (outputs.size() > 1) {
        return_type = "{ ";
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (i)
                return_type += ", ";
            return_type += llvm_type(outputs[i]->type);
        }
        return_type += " }";
    }

    std::string constraint_text;
    for (std::size_t i = 0; i < constraints.size(); ++i) {
        if (i)
            constraint_text += ',';
        constraint_text += constraints[i];
    }

    const std::string call_result = outputs.empty() ? "" : "%asm.result." + std::to_string(counter);
    os << "  ";
    if (!call_result.empty())
        os << call_result << " = ";
    os << "call " << return_type << " asm ";
    if (in.flag)
        os << "sideeffect ";
    os << '"' << escape_asm(in.text) << "\", \"" << escape_asm(constraint_text) << "\"(";
    for (std::size_t i = 0; i < call_args.size(); ++i) {
        if (i)
            os << ", ";
        os << llvm_type(call_args[i].first) << ' ' << call_args[i].second;
    }
    os << ")\n";

    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto* output = outputs[i];
        if (output->out_slot.empty())
            continue;
        std::string value = call_result;
        if (outputs.size() > 1) {
            value = "%asm.out." + std::to_string(counter) + "." + std::to_string(i);
            os << "  " << value << " = extractvalue " << return_type << ' ' << call_result << ", "
               << i << "\n";
        }
        os << "  store " << llvm_type(output->type) << ' ' << value << ", ptr " << output->out_slot
           << "\n";
        value_types[output->out_slot] = output->type;
    }
    ++counter;
}

// Lowers the compiler-recognized `uinx_syscall0..6` ABI family into a raw
// kernel trap for the current target. Argument 0 is the syscall number, the
// remaining arguments map to the architecture's syscall argument registers.
// Returns false when the target has no known trap sequence so the caller can
// fall back to an ordinary call.
bool LLVMCodegen::emit_syscall(std::ostream& os,
                               const mir::Instruction& in,
                               std::unordered_map<std::string, std::string>& values,
                               std::unordered_map<std::string, Type>& value_types,
                               std::size_t& counter) const {
    const int arity = static_cast<int>(in.args.size());
    if (arity < 1 || arity > 7)
        return false;

    auto value_of = [&](const std::string& value) -> std::string {
        const auto it = values.find(value);
        return it == values.end() ? value : it->second;
    };

    // Zero-extend each argument into an i64 so register constraints are
    // satisfied regardless of the source integer width.
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(arity));
    for (const auto& arg : in.args) {
        const std::string raw = value_of(arg);
        const auto type_it = value_types.find(arg);
        const Type type = type_it == value_types.end() ? Type::builtin("i64") : type_it->second;
        if (type.str() == "i64" || type.str() == "u64" || type.str() == "usize" ||
            type.str() == "isize") {
            args.push_back(raw);
        } else {
            args.push_back("%syscall.arg." + std::to_string(counter) + "." +
                           std::to_string(args.size()));
            os << "  " << args.back() << " = zext " << llvm_type(type) << ' ' << raw << " to i64\n";
        }
    }

    std::string constraint_text;
    std::string asm_text;
    if (target_.arch == "x86_64") {
        // rax = number, rdi/rsi/rdx/r10/r8/r9 = args 1..6. Only the registers
        // actually consumed are declared; the kernel additionally clobbers rcx
        // (return RIP) and r11 (RFLAGS).
        static const char* regs[7] = {"rax", "rdi", "rsi", "rdx", "r10", "r8", "r9"};
        std::vector<std::string> constraints{"={rax}"};
        for (int i = 0; i < arity; ++i)
            constraints.push_back(std::string("{") + regs[i] + "}");
        constraints.push_back("~{rcx}");
        constraints.push_back("~{r11}");
        constraints.push_back("~{memory}");
        constraints.push_back("~{cc}");
        for (std::size_t i = 0; i < constraints.size(); ++i) {
            if (i)
                constraint_text += ',';
            constraint_text += constraints[i];
        }
        asm_text = "syscall";
    } else if (target_.arch == "aarch64") {
        // x8 = number, x0..x5 = args 1..6. The kernel may clobber x16/x17.
        static const char* regs[7] = {"x8", "x0", "x1", "x2", "x3", "x4", "x5"};
        std::vector<std::string> constraints{"={x0}"};
        for (int i = 0; i < arity; ++i)
            constraints.push_back(std::string("{") + regs[i] + "}");
        constraints.push_back("~{x16}");
        constraints.push_back("~{x17}");
        constraints.push_back("~{memory}");
        constraints.push_back("~{cc}");
        for (std::size_t i = 0; i < constraints.size(); ++i) {
            if (i)
                constraint_text += ',';
            constraint_text += constraints[i];
        }
        asm_text = "svc #0";
    } else if (target_.arch == "riscv64") {
        // a7 = number, a0..a5 = args 1..6. The kernel may clobber a6.
        static const char* regs[7] = {"a7", "a0", "a1", "a2", "a3", "a4", "a5"};
        std::vector<std::string> constraints{"={a0}"};
        for (int i = 0; i < arity; ++i)
            constraints.push_back(std::string("{") + regs[i] + "}");
        constraints.push_back("~{a6}");
        constraints.push_back("~{memory}");
        constraints.push_back("~{cc}");
        for (std::size_t i = 0; i < constraints.size(); ++i) {
            if (i)
                constraint_text += ',';
            constraint_text += constraints[i];
        }
        asm_text = "ecall";
    } else {
        diags_.error(in.range,
                     "E0601",
                     "no syscall trap sequence is defined for target architecture '" +
                         target_.arch + "'");
        return false;
    }

    const std::string call_result = "%syscall.result." + std::to_string(counter);
    os << "  " << call_result << " = call i64 asm sideeffect \"" << asm_text << "\", \""
       << constraint_text << "\"(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i)
            os << ", ";
        os << "i64 " << args[i];
    }
    os << ")\n";

    if (!in.result.empty()) {
        // The trap returns i64; narrow to the declared result type when needed.
        const std::string result_type = llvm_type(in.type);
        if (result_type == "i64") {
            values[in.result] = call_result;
        } else {
            const std::string truncated = "%syscall.trunc." + std::to_string(counter);
            os << "  " << truncated << " = trunc i64 " << call_result << " to " << result_type
               << "\n";
            values[in.result] = truncated;
        }
        value_types[in.result] = in.type;
    }
    ++counter;
    return true;
}
} // namespace uinx
