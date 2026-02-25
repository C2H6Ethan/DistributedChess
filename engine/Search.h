#pragma once

#include "Board.h"
#include "Move.h"
#include <cstdint>
#include <cstring>
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

    void clear() {
        nodes = 0;
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        // path_hashes are written before being read, no memset needed
    }
};

// ============= Search Result =============

struct SearchResult {
    Move best_move;
    int score;       // centipawns from side-to-move's perspective
    int nodes;       // nodes searched
};

// Run iterative-deepening negamax with alpha-beta pruning from the given position.
// noise > 0 perturbs leaf evaluations (centipawns) for weaker bots.
SearchResult search(Board& board, int depth, int noise = 0);

// Static evaluation of the position (centipawns, positive = good for side to move).
// noise > 0 adds random perturbation to the evaluation.
int evaluate(Board& board, int noise = 0);

// Opening book lookup. Returns a random book move (UCI string) or empty string if no hit.
std::string book_lookup(const std::string& fen);
