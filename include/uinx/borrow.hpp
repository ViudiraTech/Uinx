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
    using LiveSet = std::unordered_set<std::string>;

    struct Borrow {
        std::string place;
        std::string borrower;
        bool mut{false};
        SourceRange origin{};
    };
    struct RefOrigin {
        std::string place;
        SourceRange range{};
        bool mut{false};
    };
    struct State {
        std::unordered_set<std::string> moved;
        std::vector<Borrow> borrows;
        std::vector<std::unordered_map<std::string, Type>> scopes;
        std::unordered_map<std::string, std::vector<RefOrigin>> ref_origins;
        bool async_context{false};
        bool reachable{true};
    };
    struct LoopFrame {
        std::size_t outer_scope_depth{};
        std::vector<State> breaks;
        std::vector<State> continues;
    };
    enum class Access { Read, Move, BorrowShared, BorrowMut, Write };

    void check_function(const ast::FunctionDecl& fn, const FunctionSig* signature = nullptr);
    void check_block(const ast::BlockStmt& block, State& state);
    void check_stmt(const ast::Stmt& stmt, State& state);
    void inspect_expr(const ast::Expr& expr, State& state, Access access = Access::Move);

    void prepare_liveness(const ast::FunctionDecl& fn);
    LiveSet liveness_block(const ast::BlockStmt& block,
                           const LiveSet& live_after,
                           const LiveSet& break_live,
                           const LiveSet& continue_live);
    LiveSet liveness_stmt(const ast::Stmt& stmt,
                          const LiveSet& live_after,
                          const LiveSet& break_live,
                          const LiveSet& continue_live);
    LiveSet expression_uses(const ast::Expr& expr) const;
    void collect_expr_uses(const ast::Expr& expr,
                           std::size_t index,
                           std::unordered_map<std::string, std::size_t>& out) const;

    std::optional<std::string> place_of(const ast::Expr& expr) const;
    std::string binding_key(const ast::LetStmt& binding) const;
    std::string parameter_key(const ast::Param& parameter) const;
    std::string for_binding_key(const ast::ForStmt& loop) const;
    std::vector<RefOrigin> reference_origins(const ast::Expr& expr, const State& state) const;
    Type type_of_place(std::string_view place, const State& state) const;
    bool contains_reference(const Type& type, std::unordered_set<std::string>& visiting) const;
    bool overlaps(std::string_view a, std::string_view b) const;
    bool same_origin(const RefOrigin& a, const RefOrigin& b) const;
    bool same_borrow(const Borrow& a, const Borrow& b) const;
    bool unavailable(std::string_view place, const State& state) const;
    bool has_moved_ancestor(std::string_view place, const State& state) const;
    bool is_stack_owned(std::string_view place, const State& state) const;
    bool is_copy_type(const Type& type) const;
    bool states_equivalent(const State& a, const State& b) const;

    void access_place(const std::string& place,
                      const SourceRange& range,
                      Access access,
                      State& state);
    void begin_borrow(const std::string& place,
                      std::string borrower,
                      bool mut,
                      const SourceRange& range,
                      State& state);
    void mark_initialized(std::string_view place, State& state);
    void kill_borrower(std::string_view borrower, State& state);
    void kill_borrowers_under(std::string_view borrower, State& state);
    void bind_reference_expression(const std::string& destination,
                                   const ast::Expr& expression,
                                   State& state);
    void promote_reference_value(const std::string& borrower,
                                 const std::vector<RefOrigin>& origins,
                                 State& state);
    void set_reference_origins(const std::string& place,
                               std::vector<RefOrigin> origins,
                               State& state);
    void merge_states(State& out, const State& left, const State& right) const;
    void merge_into(std::optional<State>& accumulator, const State& state) const;
    void truncate_scopes(State& state, std::size_t depth);
    void expire_dead_loans(const ast::Stmt& stmt, State& state);
    void report_error(const SourceRange& range, std::string code, std::string message);

    Diagnostics& diags_;
    const SemanticModel& model_;
    std::unordered_map<const ast::Stmt*, LiveSet> live_before_;
    std::unordered_map<const ast::Stmt*, LiveSet> live_after_;
    std::vector<LoopFrame*> loop_stack_;
    std::unordered_set<std::string> emitted_errors_;
    std::unordered_set<std::string> copy_generics_;
};
} // namespace uinx
