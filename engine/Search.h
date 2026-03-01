#pragma once

#include "Board.h"
#include "Move.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

// ============= Transposition Table =============

enum TTFlag : uint8_t {
    TT_EXACT,
    TT_ALPHA, // upper bound (failed low)
    TT_BETA   // lower bound (failed high)
};

struct TTEntry {
    uint64_t key;
    int16_t score;
    int8_t depth;
    uint16_t best_move_raw;
    TTFlag flag;
    uint8_t pad;
};

static_assert(sizeof(TTEntry) == 16, "TTEntry should be 16 bytes");

constexpr int TT_SIZE = 1 << 24; // ~256MB, supports Depth 14+ without overwriting root nodes

// ============= Search Context =============

struct SearchContext {
    int nodes;
    Move killers[64][2];    // [ply][slot]
    int history[2][64][64]; // [color][from][to]
    uint64_t path_hashes[256]; // Zobrist hashes of positions on the current search path,
                                // indexed by ply. Used to detect in-search repetitions.
    std::atomic<bool> stop_flag{false}; // set when time limit expires
    std::chrono::steady_clock::time_point start_time;
    int time_ms = 0; // 0 = no time limit

    void clear() {
        nodes = 0;
        time_ms = 0;
        stop_flag.store(false, std::memory_order_relaxed);
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        // path_hashes are written before being read, no memset needed
    }

    // Check elapsed time every N nodes; set stop_flag if over budget.
    inline void check_time() {
        if (time_ms > 0 && (nodes & 4095) == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= time_ms)
                stop_flag.store(true, std::memory_order_relaxed);
        }
    }
};

// ============= Search Result =============

struct SearchResult {
    Move best_move;
    int score;            // centipawns from side-to-move's perspective
    int nodes;            // nodes searched
    int depth_completed;  // deepest fully completed iteration
};

// Callback invoked after each completed depth iteration during iterative deepening.
using DepthCallback = std::function<void(int depth, const std::string& best_move, int score, int nodes)>;

// Run iterative-deepening negamax with alpha-beta pruning from the given position.
// noise > 0 perturbs leaf evaluations (centipawns) for weaker bots.
// time_ms > 0 enables time-limited search (breaks after each completed depth iteration).
// on_depth, if set, is called after each completed depth iteration.
SearchResult search(Board& board, int depth, int noise = 0, int time_ms = 0,
                    DepthCallback on_depth = nullptr);

// Static evaluation of the position (centipawns, positive = good for side to move).
// noise > 0 adds random perturbation to the evaluation.
int evaluate(Board& board, int noise = 0);

// Opening book lookup. Returns a random book move (UCI string) or empty string if no hit.
std::string book_lookup(const std::string& fen);
