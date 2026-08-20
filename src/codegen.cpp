// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Uinx Language

#include "uinx/codegen.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace uinx {
namespace {

bool unsigned_integer(TypeKind kind) {
    return kind == TypeKind::U8 || kind == TypeKind::U16 || kind == TypeKind::U32 ||
           kind == TypeKind::U64 || kind == TypeKind::U128 || kind == TypeKind::Usize;
}

int integer_bits(const Type& type) {
    switch (type.kind) {
        case TypeKind::I8:
        case TypeKind::U8:
            return 8;
        case TypeKind::I16:
        case TypeKind::U16:
            return 16;
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::Char:
            return 32;
        case TypeKind::I128:
        case TypeKind::U128:
            return 128;
        default:
            return 64;
    }
}

std::string llvm_integer_constant(std::string_view literal) {
    bool negative = false;
    std::size_t offset = 0;
    if (!literal.empty() && literal.front() == '-') {
        negative = true;
        offset = 1;
    }

    unsigned base = 10;
    if (literal.size() >= offset + 2 && literal[offset] == '0') {
        switch (literal[offset + 1]) {
            case 'x':
            case 'X':
                base = 16;
                offset += 2;
                break;
            case 'b':
            case 'B':
                base = 2;
                offset += 2;
                break;
            case 'o':
            case 'O':
                base = 8;
                offset += 2;
                break;
            default:
                break;
        }
    }

    // Store the magnitude in base 1,000,000,000 so this also handles u128
    // without narrowing through a host integer type.
    std::vector<std::uint32_t> digits{0};
    bool has_digit = false;
    for (std::size_t i = offset; i < literal.size(); ++i) {
        const char c = literal[i];
        if (c == '_')
            continue;
        unsigned value = 0;
        if (c >= '0' && c <= '9')
            value = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value = static_cast<unsigned>(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F')
            value = static_cast<unsigned>(c - 'A') + 10;
        else
            return std::string(literal);
        if (value >= base)
            return std::string(literal);

        has_digit = true;
        std::uint64_t carry = value;
        for (auto& digit : digits) {
            const std::uint64_t next = static_cast<std::uint64_t>(digit) * base + carry;
            digit = static_cast<std::uint32_t>(next % 1000000000U);
            carry = next / 1000000000U;
        }
        if (carry != 0)
            digits.push_back(static_cast<std::uint32_t>(carry));
    }
    if (!has_digit)
        return std::string(literal);

    while (digits.size() > 1 && digits.back() == 0)
        digits.pop_back();
    if (digits.size() == 1 && digits.front() == 0)
        return "0";

    std::string result = std::to_string(digits.back());
    for (std::size_t i = digits.size() - 1; i > 0; --i) {
        const std::string chunk = std::to_string(digits[i - 1]);
        result.append(9 - chunk.size(), '0');
        result += chunk;
    }
    if (negative)
        result.insert(result.begin(), '-');
    return result;
}

std::string arithmetic_opcode(const Type& type, std::string_view op) {
    if (op == "+")
        return type.is_float() ? "fadd" : "add";
    if (op == "-")
        return type.is_float() ? "fsub" : "sub";
    if (op == "*")
        return type.is_float() ? "fmul" : "mul";
    if (op == "/") {
        if (type.is_float())
            return "fdiv";
        return unsigned_integer(type.kind) ? "udiv" : "sdiv";
    }
    if (op == "%")
        return unsigned_integer(type.kind) ? "urem" : "srem";
    if (op == "&&" || op == "&")
        return "and";
    if (op == "||" || op == "|")
        return "or";
    if (op == "^")
        return "xor";
    if (op == "<<")
        return "shl";
    if (op == ">>")
        return unsigned_integer(type.kind) ? "lshr" : "ashr";
    return "add";
}

unsigned type_alignment(const Type& type) {
    switch (type.kind) {
        case TypeKind::Bool:
        case TypeKind::I8:
        case TypeKind::U8:
            return 1;
        case TypeKind::I16:
        case TypeKind::U16:
            return 2;
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::Char:
        case TypeKind::F32:
            return 4;
        case TypeKind::I128:
        case TypeKind::U128:
            return 16;
        default:
            return 8;
    }
}

std::string atomic_rmw_opcode(std::string_view op) {
    if (op == "+")
        return "add";
    if (op == "-")
        return "sub";
    if (op == "&")
        return "and";
    if (op == "|")
        return "or";
    if (op == "^")
        return "xor";
    return "xchg";
}

std::optional<std::string> atomic_order_from_abi(std::string_view value) {
    if (value == "0")
        return "monotonic";
    if (value == "1")
        return "acquire";
    if (value == "2")
        return "release";
    if (value == "3")
        return "acq_rel";
    if (value == "4")
        return "seq_cst";
    return std::nullopt;
}

std::string cmpxchg_failure_order(std::string order) {
    if (order == "release")
        return "monotonic";
    if (order == "acq_rel")
        return "acquire";
    return order;
}

std::string compare_predicate(const Type& type, std::string_view op) {
    if (type.is_float()) {
        if (op == "==")
            return "oeq";
        if (op == "!=")
            return "une";
        if (op == "<")
            return "olt";
        if (op == "<=")
            return "ole";
        if (op == ">")
            return "ogt";
        return "oge";
    }
    const bool u = unsigned_integer(type.kind);
    if (op == "==")
        return "eq";
    if (op == "!=")
        return "ne";
    if (op == "<")
        return u ? "ult" : "slt";
    if (op == "<=")
        return u ? "ule" : "sle";
    if (op == ">")
        return u ? "ugt" : "sgt";
    return u ? "uge" : "sge";
}

} // namespace

TargetInfo TargetInfo::from_triple(std::string triple) {
    TargetInfo target;
    target.triple = std::move(triple);
    const auto dash = target.triple.find('-');
    target.arch = target.triple.substr(0, dash);
    if (target.arch == "x86_64") {
        target.data_layout =
            "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
    } else if (target.arch == "aarch64") {
        target.data_layout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128";
    } else if (target.arch == "riscv64") {
        target.data_layout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128";
    } else {
        target.data_layout = "e-p:64:64-i64:64-n8:16:32:64-S128";
    }
    return target;
}

std::string LLVMCodegen::llvm_type(const Type& type) const {
    switch (type.kind) {
        case TypeKind::Unit:
        case TypeKind::Never:
            return "void";
        case TypeKind::Bool:
            return "i1";
        case TypeKind::I8:
        case TypeKind::U8:
            return "i8";
        case TypeKind::I16:
        case TypeKind::U16:
            return "i16";
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::Char:
            return "i32";
        case TypeKind::I64:
        case TypeKind::U64:
        case TypeKind::Isize:
        case TypeKind::Usize:
            return "i64";
        case TypeKind::I128:
        case TypeKind::U128:
            return "i128";
        case TypeKind::F32:
            return "float";
        case TypeKind::F64:
            return "double";
        case TypeKind::Ref:
        case TypeKind::RawPtr:
        case TypeKind::Str:
            return "ptr";
        case TypeKind::Named: {
            if (type.name == "Future" && !type.args.empty())
                return "%uinx.future";
            std::string name = type.name;
            if (!type.args.empty()) {
                name += '$';
                for (std::size_t i = 0; i < type.args.size(); ++i) {
                    if (i)
                        name += '$';
                    name += type.args[i].str();
                }
            }
            return "%struct." + llvm_name(name);
        }
        case TypeKind::Generic:
            return "ptr";
        case TypeKind::Error:
            return "i8";
    }
    return "i8";
}

std::string LLVMCodegen::llvm_name(std::string_view name) const {
    std::string out;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
            out += c;
        } else {
            out += '_';
            out += std::to_string(static_cast<unsigned char>(c));
            out += '_';
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())))
        out = "_" + out;
    return out;
}

std::string LLVMCodegen::escape_ir_string(std::string_view value) const {
    std::ostringstream out;
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        if (c >= 32 && c <= 126 && c != '"' && c != '\\') {
            out << static_cast<char>(c);
        } else {
            out << '\\' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(c) << std::nouppercase << std::dec;
        }
    }
    return out.str();
}

std::string LLVMCodegen::escape_asm(std::string_view value) const {
    std::string out;
    for (const char c : value) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

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
                    if (instruction.text == "uinx_atomic_load_u64" &&
                        instruction.args.size() == 2) {
                        if (auto order = atomic_order_from_abi(value_of(instruction.args[1]));
                            order && *order != "release" && *order != "acq_rel") {
                            os << "  " << instruction.result << " = load atomic i64, ptr "
                               << value_of(instruction.args[0]) << ' ' << *order << ", align 8\n";
                            values[instruction.result] = instruction.result;
                            value_types[instruction.result] = instruction.type;
                            break;
                        }
                    }
                    if (instruction.text == "uinx_atomic_store_u64" &&
                        instruction.args.size() == 3) {
                        if (auto order = atomic_order_from_abi(value_of(instruction.args[2]));
                            order && *order != "acquire" && *order != "acq_rel") {
                            os << "  store atomic i64 " << value_of(instruction.args[1]) << ", ptr "
                               << value_of(instruction.args[0]) << ' ' << *order << ", align 8\n";
                            break;
                        }
                    }
                    if (instruction.text == "uinx_atomic_fetch_add_u64" &&
                        instruction.args.size() == 3) {
                        if (auto order = atomic_order_from_abi(value_of(instruction.args[2]))) {
                            os << "  " << instruction.result << " = atomicrmw add ptr "
                               << value_of(instruction.args[0]) << ", i64 "
                               << value_of(instruction.args[1]) << ' ' << *order << ", align 8\n";
                            values[instruction.result] = instruction.result;
                            value_types[instruction.result] = instruction.type;
                            break;
                        }
                    }
                    if (instruction.text == "uinx_atomic_compare_exchange_u64" &&
                        instruction.args.size() == 5) {
                        auto success = atomic_order_from_abi(value_of(instruction.args[3]));
                        auto failure = atomic_order_from_abi(value_of(instruction.args[4]));
                        if (success && failure) {
                            const std::string id = std::to_string(atomic_counter++);
                            const std::string expected = "%cas.expected." + id;
                            const std::string pair = "%cas.pair." + id;
                            const std::string old = "%cas.old." + id;
                            const std::string ok = "%cas.ok." + id;
                            const std::string result = instruction.result.empty()
                                                           ? "%cas.result." + id
                                                           : instruction.result;
                            os << "  " << expected << " = load i64, ptr "
                               << value_of(instruction.args[1]) << ", align 8\n";
                            os << "  " << pair << " = cmpxchg ptr " << value_of(instruction.args[0])
                               << ", i64 " << expected << ", i64 " << value_of(instruction.args[2])
                               << ' ' << *success << ' ' << cmpxchg_failure_order(*failure)
                               << ", align 8\n";
                            os << "  " << old << " = extractvalue { i64, i1 } " << pair << ", 0\n";
                            os << "  " << ok << " = extractvalue { i64, i1 } " << pair << ", 1\n";
                            os << "  store i64 " << old << ", ptr " << value_of(instruction.args[1])
                               << ", align 8\n";
                            os << "  " << result << " = zext i1 " << ok << " to i32\n";
                            if (!instruction.result.empty()) {
                                values[instruction.result] = result;
                                value_types[instruction.result] = instruction.type;
                            }
                            break;
                        }
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
                    if ((from.kind == TypeKind::Ref || from.kind == TypeKind::RawPtr) &&
                        (instruction.type.kind == TypeKind::Ref ||
                         instruction.type.kind == TypeKind::RawPtr)) {
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

std::string LLVMCodegen::emit(const mir::Module& module) {
    std::ostringstream os;
    os << "; Uinx LLVM IR - generated by JiTianYu391\n";
    os << "target datalayout = \"" << target_.data_layout << "\"\n";
    os << "target triple = \"" << target_.triple << "\"\n\n";

    bool has_future = false;
    bool has_async_definition = false;
    bool has_bounds_checks = false;
    for (const auto& function : module.functions) {
        if (function.is_async) {
            has_future = true;
            if (!function.is_extern)
                has_async_definition = true;
        }
        if (function.result.kind == TypeKind::Named && function.result.name == "Future")
            has_future = true;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.op == mir::Op::Await ||
                    (instruction.type.kind == TypeKind::Named && instruction.type.name == "Future"))
                    has_future = true;
                if (instruction.op == mir::Op::IndexAddr)
                    has_bounds_checks = true;
            }
        }
    }
    if (has_future)
        os << "%uinx.future.vtable = type { ptr, ptr }\n%uinx.future = type { ptr, ptr }\n";
    if (has_async_definition) {
        os << "declare noalias ptr @malloc(i64)\n";
        os << "declare void @free(ptr)\n";
        os << "declare void @abort()\n";
    }
    if (has_bounds_checks)
        os << "declare void @llvm.trap()\n";
    if (has_future || has_async_definition || has_bounds_checks)
        os << '\n';

    std::unordered_map<std::string, const ast::StructDecl*> struct_decls;
    for (const auto* declaration : module.structs)
        struct_decls[declaration->name] = declaration;
    std::unordered_map<std::string, Type> needed_structs;
    std::function<void(const Type&)> collect_type = [&](const Type& type) {
        if (type.kind == TypeKind::Named && type.name != "Future") {
            if (struct_decls.contains(type.name))
                needed_structs[type.str()] = type;
            for (const auto& arg : type.args)
                collect_type(arg);
        }
    };
    for (const auto& global : module.globals)
        collect_type(global.type);
    for (const auto& function : module.functions) {
        collect_type(function.result);
        for (const auto& param : function.params)
            collect_type(param.type);
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                collect_type(instruction.type);
                collect_type(instruction.auxiliary_type);
                for (const auto& operand : instruction.asm_operands)
                    collect_type(operand.type);
            }
        }
    }
    for (const auto& [key, type] : needed_structs) {
        (void)key;
        const auto declaration_it = struct_decls.find(type.name);
        if (declaration_it == struct_decls.end())
            continue;
        const auto* declaration = declaration_it->second;
        std::unordered_map<std::string, Type> substitution;
        for (std::size_t i = 0; i < declaration->generics.size() && i < type.args.size(); ++i) {
            substitution[declaration->generics[i].name] = type.args[i];
        }
        os << llvm_type(type) << " = type { ";
        std::unordered_set<std::string> generics;
        for (const auto& generic : declaration->generics)
            generics.insert(generic.name);
        for (std::size_t i = 0; i < declaration->fields.size(); ++i) {
            if (i)
                os << ", ";
            Type field_type = type_from_ast(declaration->fields[i].type, generics);
            field_type = substitute_type(field_type, substitution);
            os << llvm_type(field_type);
        }
        os << " }\n";
    }
    if (!needed_structs.empty())
        os << '\n';

    for (const auto& global : module.globals) {
        os << '@' << llvm_name(global.name) << " = ";
        if (global.is_percpu)
            os << "thread_local(localexec) ";
        const std::string initializer = global.type.is_integer()
                                            ? llvm_integer_constant(global.initializer)
                                            : global.initializer;
        os << (global.is_mut ? "global " : "constant ") << llvm_type(global.type) << ' '
           << initializer << ", align " << type_alignment(global.type) << "\n";
    }
    if (!module.globals.empty())
        os << '\n';

    for (const auto& string : module.strings) {
        std::string bytes = string.value;
        bytes.push_back('\0');
        os << '@' << llvm_name(string.symbol) << " = private unnamed_addr constant ["
           << bytes.size() << " x i8] c\"" << escape_ir_string(bytes) << "\", align 1\n";
    }
    if (!module.strings.empty())
        os << '\n';

    std::unordered_map<std::string, const mir::Function*> functions;
    for (const auto& function : module.functions)
        functions[function.name] = &function;

    std::unordered_set<std::string> drop_types;
    for (const auto& function : module.functions) {
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.op == mir::Op::Drop)
                    drop_types.insert(instruction.type.name);
            }
        }
    }
    for (const auto& type : drop_types) {
        const std::string drop_name = type + "__drop";
        if (!functions.contains(drop_name))
            os << "declare void @" << llvm_name(drop_name) << "(ptr)\n";
    }
    if (!drop_types.empty())
        os << '\n';

    for (const auto& function : module.functions)
        emit_function(os, function, functions);
    return os.str();
}

} // namespace uinx
