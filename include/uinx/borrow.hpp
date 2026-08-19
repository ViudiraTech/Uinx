// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#pragma once
#include "uinx/sema.hpp"

namespace uinx {
class BorrowChecker {
  public:
    BorrowChecker(Diagnostics& d, const SemanticModel& m) : diags_(d), model_(m) {
    }
    void check(const ast::Module& module);

  private:
    struct Borrow {
        std::string place;
        std::string borrower;
        bool mut{false};
        std::size_t end_stmt{};
        SourceRange origin{};
    };
    struct RefOrigin {
        std::string place;
        SourceRange range{};
    };
    struct State {
        std::unordered_set<std::string> moved;
        std::vector<Borrow> borrows;
        std::vector<std::unordered_map<std::string, Type>> scopes;
        std::unordered_map<std::string, std::size_t> last_use;
        std::unordered_map<std::string, RefOrigin> ref_origins;
        std::size_t stmt_index{};
    };
    enum class Access { Read, Move, BorrowShared, BorrowMut, Write };
    void check_function(const ast::FunctionDecl& fn);
    void check_block(const ast::BlockStmt& block, State& state);
    void check_stmt(const ast::Stmt& stmt, State& state);
    void inspect_expr(const ast::Expr& expr, State& state, Access access = Access::Move);
    void collect_uses(const ast::Stmt& stmt,
                      std::size_t index,
                      std::unordered_map<std::string, std::size_t>& out) const;
    void collect_expr_uses(const ast::Expr& expr,
                           std::size_t index,
                           std::unordered_map<std::string, std::size_t>& out) const;
    std::optional<std::string> place_of(const ast::Expr& expr) const;
    std::optional<RefOrigin> reference_origin(const ast::Expr& expr, const State& state) const;
    Type type_of_place(std::string_view place, const State& state) const;
    bool contains_reference(const Type& type, std::unordered_set<std::string>& visiting) const;
    bool overlaps(std::string_view a, std::string_view b) const;
    bool unavailable(std::string_view place, const State& state) const;
    void
    access_place(const std::string& place, const SourceRange& range, Access access, State& state);
    void begin_borrow(const std::string& place,
                      std::string borrower,
                      bool mut,
                      std::size_t end,
                      const SourceRange& range,
                      State& state);
    Diagnostics& diags_;
    const SemanticModel& model_;
};
} // namespace uinx
