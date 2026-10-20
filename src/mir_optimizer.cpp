// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "mir_internal.hpp"
#include "uinx/mir.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace uinx {
using namespace mir_detail;

void MIROptimizer::remove_unreachable(mir::Function& f) const {
    std::unordered_set<std::string> reachable;
    if (f.blocks.empty())
        return;
    std::unordered_map<std::string, std::size_t> labels;
    for (std::size_t i = 0; i < f.blocks.size(); ++i)
        labels[f.blocks[i].label] = i;
    std::vector<std::size_t> work{0};
    while (!work.empty()) {
        const std::size_t index = work.back();
        work.pop_back();
        if (index >= f.blocks.size() || !reachable.insert(f.blocks[index].label).second)
            continue;
        const auto& block = f.blocks[index];
        if (block.instructions.empty()) {
            if (index + 1 < f.blocks.size())
                work.push_back(index + 1);
            continue;
        }
        const auto& tail = block.instructions.back();
        if (tail.op == mir::Op::Jump && !tail.args.empty()) {
            if (auto it = labels.find(tail.args[0]); it != labels.end())
                work.push_back(it->second);
        } else if (tail.op == mir::Op::CondJump && tail.args.size() >= 3) {
            if (auto it = labels.find(tail.args[1]); it != labels.end())
                work.push_back(it->second);
            if (auto it = labels.find(tail.args[2]); it != labels.end())
                work.push_back(it->second);
        } else if (tail.op != mir::Op::Return && index + 1 < f.blocks.size()) {
            work.push_back(index + 1);
        }
    }
    f.blocks.erase(std::remove_if(f.blocks.begin(),
                                  f.blocks.end(),
                                  [&](const mir::BasicBlock& block) {
                                      return !reachable.contains(block.label);
                                  }),
                   f.blocks.end());
}

void MIROptimizer::forward_local_loads(mir::Function& fn) const {
    for (auto& block : fn.blocks) {
        std::unordered_map<std::string, std::string> slots;
        std::unordered_map<std::string, std::string> aliases;
        auto resolve = [&](std::string value) {
            for (int depth = 0; depth < 32; ++depth) {
                auto it = aliases.find(value);
                if (it == aliases.end() || it->second == value)
                    break;
                value = it->second;
            }
            return value;
        };
        std::vector<mir::Instruction> output;
        output.reserve(block.instructions.size());
        for (auto instruction : block.instructions) {
            for (auto& arg : instruction.args)
                arg = resolve(arg);
            for (auto& operand : instruction.asm_operands) {
                if (!operand.value.empty())
                    operand.value = resolve(operand.value);
            }
            const auto is_local_slot = [](const std::string& address) {
                return !address.empty() && address.front() == '%' &&
                       address.find(".slot.") != std::string::npos;
            };
            if (instruction.op == mir::Op::Load && !instruction.flag && !instruction.args.empty()) {
                const auto it = slots.find(instruction.args[0]);
                if (it != slots.end()) {
                    aliases[instruction.result] = resolve(it->second);
                    continue;
                }
            }
            if (instruction.op == mir::Op::Store && !instruction.flag &&
                instruction.args.size() >= 2) {
                const std::string& address = instruction.args[1];
                if (is_local_slot(address))
                    slots[address] = instruction.args[0];
                else
                    slots.clear();
            } else if (instruction.op == mir::Op::AddressOf) {
                if (!instruction.args.empty())
                    slots.erase(instruction.args[0]);
            } else if (instruction.op == mir::Op::Call || instruction.op == mir::Op::InlineAsm ||
                       instruction.op == mir::Op::AtomicRmw || instruction.op == mir::Op::Fence ||
                       instruction.op == mir::Op::CompilerFence ||
                       (instruction.op == mir::Op::Load && instruction.flag) ||
                       (instruction.op == mir::Op::Store && instruction.flag)) {
                slots.clear();
            }
            output.push_back(std::move(instruction));
        }
        block.instructions = std::move(output);
    }
}

void MIROptimizer::constant_fold(mir::Function& fn) const {
    for (auto& block : fn.blocks) {
        std::unordered_map<std::string, std::int64_t> constants;
        auto get = [&](const std::string& value, std::int64_t& out) {
            if (auto it = constants.find(value); it != constants.end()) {
                out = it->second;
                return true;
            }
            char* end = nullptr;
            const long long parsed = std::strtoll(value.c_str(), &end, 0);
            if (end && *end == '\0' && end != value.c_str()) {
                out = static_cast<std::int64_t>(parsed);
                return true;
            }
            return false;
        };
        for (auto& instruction : block.instructions) {
            if ((instruction.op == mir::Op::ConstInt || instruction.op == mir::Op::ConstBool ||
                 instruction.op == mir::Op::ConstChar) &&
                !instruction.result.empty()) {
                std::int64_t value{};
                if (get(instruction.text, value))
                    constants[instruction.result] = value;
                continue;
            }
            if (instruction.type.kind == TypeKind::I128 || instruction.type.kind == TypeKind::U128)
                continue;
            if (instruction.op == mir::Op::Binary && !instruction.result.empty()) {
                std::int64_t a{}, b{};
                if (instruction.text == "!" && instruction.args.size() == 1 &&
                    get(instruction.args[0], a)) {
                    const std::int64_t result = instruction.type.kind == TypeKind::Bool ? !a : ~a;
                    instruction.op = instruction.type.kind == TypeKind::Bool ? mir::Op::ConstBool
                                                                             : mir::Op::ConstInt;
                    instruction.args.clear();
                    instruction.text = std::to_string(result);
                    constants[instruction.result] = result;
                    continue;
                }
                if (instruction.args.size() != 2 || !get(instruction.args[0], a) ||
                    !get(instruction.args[1], b))
                    continue;
                std::int64_t result{};
                bool ok = true;
                if (instruction.text == "+")
                    result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) +
                                                       static_cast<std::uint64_t>(b));
                else if (instruction.text == "-")
                    result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) -
                                                       static_cast<std::uint64_t>(b));
                else if (instruction.text == "*")
                    result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) *
                                                       static_cast<std::uint64_t>(b));
                else if (instruction.text == "/") {
                    if (b == 0)
                        ok = false;
                    else
                        result = unsigned_integer_kind(instruction.type.kind)
                                     ? static_cast<std::int64_t>(static_cast<std::uint64_t>(a) /
                                                                 static_cast<std::uint64_t>(b))
                                     : a / b;
                } else if (instruction.text == "%") {
                    if (b == 0)
                        ok = false;
                    else
                        result = unsigned_integer_kind(instruction.type.kind)
                                     ? static_cast<std::int64_t>(static_cast<std::uint64_t>(a) %
                                                                 static_cast<std::uint64_t>(b))
                                     : a % b;
                } else if (instruction.text == "&" || instruction.text == "&&")
                    result = a & b;
                else if (instruction.text == "|" || instruction.text == "||")
                    result = a | b;
                else if (instruction.text == "^")
                    result = a ^ b;
                else if (instruction.text == "<<") {
                    if (b < 0 || b >= 64)
                        ok = false;
                    else
                        result = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) << b);
                } else if (instruction.text == ">>") {
                    if (b < 0 || b >= 64)
                        ok = false;
                    else
                        result = unsigned_integer_kind(instruction.type.kind)
                                     ? static_cast<std::int64_t>(static_cast<std::uint64_t>(a) >> b)
                                     : (a >> b);
                } else
                    ok = false;
                if (ok) {
                    instruction.op = mir::Op::ConstInt;
                    instruction.args.clear();
                    instruction.text = std::to_string(result);
                    constants[instruction.result] = result;
                }
            } else if (instruction.op == mir::Op::Compare && instruction.args.size() == 2 &&
                       !instruction.result.empty()) {
                std::int64_t a{}, b{};
                if (!get(instruction.args[0], a) || !get(instruction.args[1], b))
                    continue;
                const bool u = unsigned_integer_kind(instruction.type.kind);
                bool result = false;
                if (instruction.text == "==")
                    result = a == b;
                else if (instruction.text == "!=")
                    result = a != b;
                else if (instruction.text == "<")
                    result =
                        u ? static_cast<std::uint64_t>(a) < static_cast<std::uint64_t>(b) : a < b;
                else if (instruction.text == "<=")
                    result =
                        u ? static_cast<std::uint64_t>(a) <= static_cast<std::uint64_t>(b) : a <= b;
                else if (instruction.text == ">")
                    result =
                        u ? static_cast<std::uint64_t>(a) > static_cast<std::uint64_t>(b) : a > b;
                else if (instruction.text == ">=")
                    result =
                        u ? static_cast<std::uint64_t>(a) >= static_cast<std::uint64_t>(b) : a >= b;
                instruction.op = mir::Op::ConstBool;
                instruction.type = Type::builtin("bool");
                instruction.args.clear();
                instruction.text = result ? "1" : "0";
                constants[instruction.result] = result ? 1 : 0;
            }
        }
    }
}

void MIROptimizer::eliminate_dead_values(mir::Function& fn) const {
    bool changed = true;
    while (changed) {
        changed = false;
        std::unordered_map<std::string, std::size_t> uses;
        for (const auto& block : fn.blocks)
            for (const auto& instruction : block.instructions) {
                for (const auto& arg : instruction.args)
                    ++uses[arg];
                for (const auto& operand : instruction.asm_operands)
                    if (!operand.value.empty())
                        ++uses[operand.value];
            }
        for (auto& block : fn.blocks) {
            auto it = std::remove_if(
                block.instructions.begin(),
                block.instructions.end(),
                [&](const mir::Instruction& instruction) {
                    if (instruction.result.empty() || uses[instruction.result] != 0)
                        return false;
                    bool pure =
                        instruction.op == mir::Op::Alloca || instruction.op == mir::Op::ConstInt ||
                        instruction.op == mir::Op::ConstFloat ||
                        instruction.op == mir::Op::ConstBool ||
                        instruction.op == mir::Op::ConstChar ||
                        instruction.op == mir::Op::ConstString ||
                        instruction.op == mir::Op::StructInit ||
                        instruction.op == mir::Op::FieldAddr || instruction.op == mir::Op::Binary ||
                        instruction.op == mir::Op::Compare || instruction.op == mir::Op::Cast ||
                        instruction.op == mir::Op::AddressOf ||
                        (instruction.op == mir::Op::Load && !instruction.flag);
                    if (pure)
                        changed = true;
                    return pure;
                });
            block.instructions.erase(it, block.instructions.end());
        }
    }
}

void MIROptimizer::run(mir::Module& m) const {
    if (level_ <= 0)
        return;
    for (auto& f : m.functions) {
        if (f.is_extern)
            continue;
        remove_unreachable(f);
        forward_local_loads(f);
        constant_fold(f);
        eliminate_dead_values(f);
        if (level_ >= 2) {
            forward_local_loads(f);
            eliminate_dead_values(f);
        }
    }
}
} // namespace uinx
