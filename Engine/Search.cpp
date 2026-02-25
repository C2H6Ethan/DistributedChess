#include "Search.h"
#include <algorithm>
#include <climits>

// ============= Material Values (centipawns) =============
static constexpr int PIECE_VALUE[PIECE_TYPE_COUNT] = {
    100,   // PAWN
    320,   // KNIGHT
    330,   // BISHOP
    500,   // ROOK
    900,   // QUEEN
    20000  // KING
};

// ============= Piece-Square Tables (from white's perspective) =============
// Indexed [square] where a1=0, h8=63. Flipped for black.

static constexpr int PAWN_PST[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0,
};

static constexpr int KNIGHT_PST[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50,
};

static constexpr int BISHOP_PST[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10,-10,-10,-10,-10,-20,
};

static constexpr int ROOK_PST[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};

static constexpr int QUEEN_PST[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -10,  5,  5,  5,  5,  5,  0,-10,
     0,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
   -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20,
};

static constexpr int KING_PST[64] = {
    20, 30, 10,  0,  0, 10, 30, 20,
    20, 20,  0,  0,  0,  0, 20, 20,
   -10,-20,-20,-20,-20,-20,-20,-10,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
};

static const int* PST[PIECE_TYPE_COUNT] = {
    PAWN_PST, KNIGHT_PST, BISHOP_PST, ROOK_PST, QUEEN_PST, KING_PST
};

// Mirror a square vertically (for black's perspective).
static inline Square flip_square(Square s) {
    return Square(s ^ 56); // flips rank: rank 0 <-> rank 7
}

// ============= Evaluation =============

int evaluate(Board& board) {
    int score = 0;

    for (int sq = 0; sq < 64; sq++) {
        PieceType pt = board.get_piece_type_on_square(Square(sq));
        if (pt == NO_PIECE_TYPE) continue;

        Color c = board.get_piece_color_on_square(Square(sq));
        int value = PIECE_VALUE[pt];

        // Piece-square bonus: use the square directly for white, flip for black.
        int pst_sq = (c == WHITE) ? sq : flip_square(Square(sq));
        value += PST[pt][pst_sq];

        score += (c == WHITE) ? value : -value;
    }

    // Return relative to side to move.
    return (board.get_player_to_move() == WHITE) ? score : -score;
}

// ============= Negamax with Alpha-Beta =============

static int negamax(Board& board, int depth, int alpha, int beta, int& nodes) {
    if (depth == 0) {
        nodes++;
        return evaluate(board);
    }

    std::vector<Move> moves = board.get_legal_moves();

    // Terminal detection
    if (moves.empty()) {
        if (board.is_in_check(board.get_player_to_move())) {
            return -100000 + (100 - depth); // prefer shorter mates
        }
        return 0; // stalemate
    }

    if (board.get_halfmove_clock() >= 100 || board.is_insufficient_material()) {
        return 0;
    }

    // Simple move ordering: captures first
    std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return a.is_capture() > b.is_capture();
    });

    int best = INT_MIN;

    for (auto& m : moves) {
        board.move(m);
        int score = -negamax(board, depth - 1, -beta, -alpha, nodes);
        board.undo_move(m);

        if (score > best) best = score;
        if (score > alpha) alpha = score;
        if (alpha >= beta) break; // beta cutoff
    }

    return best;
}

// ============= Top-level Search =============

SearchResult search(Board& board, int depth) {
    SearchResult result;
    result.best_move = Move();
    result.score = INT_MIN;
    result.nodes = 0;

    std::vector<Move> moves = board.get_legal_moves();

    if (moves.empty()) {
        result.score = board.is_in_check(board.get_player_to_move()) ? -100000 : 0;
        return result;
    }

    // Captures first
    std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return a.is_capture() > b.is_capture();
    });

    int alpha = INT_MIN + 1;
    int beta  = INT_MAX;

    for (auto& m : moves) {
        board.move(m);
        int score = -negamax(board, depth - 1, -beta, -alpha, result.nodes);
        board.undo_move(m);

        if (score > result.score) {
            result.score = score;
            result.best_move = m;
        }
        if (score > alpha) alpha = score;
    }

    return result;
}
