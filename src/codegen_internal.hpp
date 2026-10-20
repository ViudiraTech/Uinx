// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

// Internal helpers shared by the translation units split out of codegen.cpp.

#pragma once

#include "uinx/codegen.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace uinx::codegen_detail {

inline bool unsigned_integer(TypeKind kind) {
    return kind == TypeKind::U8 || kind == TypeKind::U16 || kind == TypeKind::U32 ||
           kind == TypeKind::U64 || kind == TypeKind::U128 || kind == TypeKind::Usize;
}

inline int integer_bits(const Type& type) {
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

inline std::string llvm_integer_constant(std::string_view literal) {
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

inline std::string arithmetic_opcode(const Type& type, std::string_view op) {
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

inline unsigned type_alignment(const Type& type) {
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

inline std::string atomic_rmw_opcode(std::string_view op) {
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

inline std::optional<std::string> atomic_order_from_abi(std::string_view value) {
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

inline std::string cmpxchg_failure_order(std::string order) {
    if (order == "release")
        return "monotonic";
    if (order == "acq_rel")
        return "acquire";
    return order;
}

// Industrial C++20 ordering validation (Rust + C++20 + LKMM fusion).
// load: relaxed/acquire/seq_cst; store: relaxed/release/seq_cst;
// rmw/exchange: all five; cmpxchg failure: never release/acq_rel.
inline bool valid_atomic_load_order(std::string_view order) {
    return order == "monotonic" || order == "acquire" || order == "seq_cst";
}

inline bool valid_atomic_store_order(std::string_view order) {
    return order == "monotonic" || order == "release" || order == "seq_cst";
}

inline bool valid_atomic_rmw_order(std::string_view order) {
    return order == "monotonic" || order == "acquire" || order == "release" || order == "acq_rel" ||
           order == "seq_cst";
}

inline int order_strength(std::string_view order) {
    if (order == "monotonic")
        return 0;
    if (order == "acquire" || order == "release")
        return 1;
    if (order == "acq_rel")
        return 2;
    if (order == "seq_cst")
        return 3;
    return -1;
}

inline std::string downgrade_cmpxchg_failure(std::string success, std::string failure) {
    failure = cmpxchg_failure_order(std::move(failure));
    // Failure ordering must not exceed success ordering.
    if (order_strength(failure) > order_strength(success)) {
        if (success == "monotonic" || success == "release")
            return "monotonic";
        return "acquire";
    }
    return failure;
}

struct AtomicIntrinsic {
    std::string llvm_type;
    unsigned align;
    std::string rmw_op; // add/sub/and/or/xor/xchg/cmpxchg/load/store
};

inline std::optional<AtomicIntrinsic> parse_atomic_intrinsic(std::string_view name) {
    // uinx_atomic_{load,store,fetch_add,fetch_sub,fetch_and,fetch_or,fetch_xor,
    // exchange,compare_exchange}_{u8,u16,u32,u64}
    static constexpr std::string_view prefix = "uinx_atomic_";
    if (!name.starts_with(prefix))
        return std::nullopt;
    std::string_view rest = name.substr(prefix.size());
    std::string op;
    for (std::string_view candidate : {"compare_exchange",
                                       "fetch_add",
                                       "fetch_sub",
                                       "fetch_and",
                                       "fetch_or",
                                       "fetch_xor",
                                       "exchange",
                                       "load",
                                       "store"}) {
        if (rest.starts_with(candidate)) {
            op = std::string(candidate);
            rest = rest.substr(candidate.size());
            break;
        }
    }
    if (op.empty() || !rest.starts_with("_"))
        return std::nullopt;
    std::string_view width = rest.substr(1);
    std::string llvm_type;
    unsigned align = 1;
    if (width == "u8") {
        llvm_type = "i8";
        align = 1;
    } else if (width == "u16") {
        llvm_type = "i16";
        align = 2;
    } else if (width == "u32") {
        llvm_type = "i32";
        align = 4;
    } else if (width == "u64") {
        llvm_type = "i64";
        align = 8;
    } else {
        return std::nullopt;
    }
    std::string rmw;
    if (op == "load")
        rmw = "load";
    else if (op == "store")
        rmw = "store";
    else if (op == "fetch_add")
        rmw = "add";
    else if (op == "fetch_sub")
        rmw = "sub";
    else if (op == "fetch_and")
        rmw = "and";
    else if (op == "fetch_or")
        rmw = "or";
    else if (op == "fetch_xor")
        rmw = "xor";
    else if (op == "exchange")
        rmw = "xchg";
    else if (op == "compare_exchange")
        rmw = "cmpxchg";
    return AtomicIntrinsic{llvm_type, align, rmw};
}

// Number of arguments encoded in a uinx_syscallN intrinsic name, or
// std::nullopt when the symbol is an ordinary runtime call.
inline std::optional<int> syscall_arity_from_name(std::string_view name) {
    static constexpr std::string_view prefix = "uinx_syscall";
    if (!name.starts_with(prefix))
        return std::nullopt;
    const std::string_view suffix = name.substr(prefix.size());
    if (suffix.empty() || suffix.size() > 1)
        return std::nullopt;
    if (suffix.front() < '0' || suffix.front() > '6')
        return std::nullopt;
    return suffix.front() - '0';
}

inline std::string compare_predicate(const Type& type, std::string_view op) {
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
} // namespace uinx::codegen_detail
