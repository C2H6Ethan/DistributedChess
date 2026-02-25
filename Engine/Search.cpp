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

// ============= MVV-LVA Move Ordering =============

// Score a move for ordering. Captures are scored by MVV-LVA (victim value minus
// attacker value) and offset so they always sort above quiet moves.
static int mvv_lva_score(const Move& m, Board& board) {
    if (!m.is_capture()) return 0; // quiet moves get the lowest tier

    PieceType attacker = board.get_piece_type_on_square(m.from());
    PieceType victim   = board.get_piece_type_on_square(m.to());

    // En-passant: the target square is empty but the victim is a pawn.
    if (victim == NO_PIECE_TYPE) victim = PAWN;

    // Offset by a large constant so every capture sorts above every quiet move.
    return PIECE_VALUE[victim] - PIECE_VALUE[attacker] + 100000;
}

static void order_moves(Move* moves, int count, Board& board) {
    std::sort(moves, moves + count,
        [&board](const Move& a, const Move& b) {
            return mvv_lva_score(a, board) > mvv_lva_score(b, board);
        });
}

// ============= Quiescence Search =============

static constexpr int DELTA_MARGIN = 900; // queen value

static int quiescence_search(Board& board, int alpha, int beta, int& nodes) {
    nodes++;

    // Stand-pat: static evaluation as a lower bound.
    int stand_pat = evaluate(board);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    // Delta pruning: if even capturing a queen can't raise us to alpha, bail out.
    if (stand_pat + DELTA_MARGIN < alpha) return alpha;

    Move captures[256];
    int count = board.get_legal_captures(captures);

    order_moves(captures, count, board);

    for (int i = 0; i < count; i++) {
        board.move(captures[i]);
        int score = -quiescence_search(board, -beta, -alpha, nodes);
        board.undo_move(captures[i]);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

// ============= Negamax with Alpha-Beta =============

static int negamax(Board& board, int depth, int alpha, int beta, int& nodes) {
    bool in_check = board.is_in_check(board.get_player_to_move());

    // Check extension: don't enter QS while in check — extend to find evasions.
    if (depth <= 0 && !in_check) {
        return quiescence_search(board, alpha, beta, nodes);
    }
    if (depth <= 0 && in_check) {
        depth = 1;
    }

    Move moves[256];
    int count = board.get_legal_moves(moves);

    // Terminal detection
    if (count == 0) {
        if (in_check) {
            return -100000 + (100 - depth); // prefer shorter mates
        }
        return 0; // stalemate
    }

    if (board.get_halfmove_clock() >= 100 || board.is_insufficient_material()) {
        return 0;
    }

    order_moves(moves, count, board);

    int best = INT_MIN;

    for (int i = 0; i < count; i++) {
        board.move(moves[i]);
        int score = -negamax(board, depth - 1, -beta, -alpha, nodes);
        board.undo_move(moves[i]);

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

    Move moves[256];
    int count = board.get_legal_moves(moves);

    if (count == 0) {
        result.score = board.is_in_check(board.get_player_to_move()) ? -100000 : 0;
        return result;
    }

    order_moves(moves, count, board);

    int alpha = INT_MIN + 1;
    int beta  = INT_MAX;

    for (int i = 0; i < count; i++) {
        board.move(moves[i]);
        int score = -negamax(board, depth - 1, -beta, -alpha, result.nodes);
        board.undo_move(moves[i]);

        if (score > result.score) {
            result.score = score;
            result.best_move = moves[i];
        }
        if (score > alpha) alpha = score;
    }

    return result;
}
