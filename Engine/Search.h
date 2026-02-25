#pragma once

#include "Board.h"
#include "Move.h"
#include <string>

struct SearchResult {
    Move best_move;
    int score;       // centipawns from side-to-move's perspective
    int nodes;       // nodes searched
};

// Run negamax with alpha-beta pruning from the given position.
// Returns the best move, its score, and node count.
SearchResult search(Board& board, int depth);

// Static evaluation of the position (centipawns, positive = good for side to move).
int evaluate(Board& board);
