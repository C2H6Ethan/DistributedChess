#include "Search.h"
#include <algorithm>
#include <climits>
#include <cstring>

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

// ============= Transposition Table =============

static TTEntry transposition_table[TT_SIZE];

static inline int tt_index(uint64_t key) {
    return key & (TT_SIZE - 1);
}

static void tt_store(uint64_t key, int score, int depth, Move best, TTFlag flag, int ply) {
    // Adjust mate scores for storage (relative to root)
    int stored_score = score;
    if (stored_score > 90000) stored_score += ply;
    else if (stored_score < -90000) stored_score -= ply;

    TTEntry& e = transposition_table[tt_index(key)];
    e.key = key;
    e.score = static_cast<int16_t>(stored_score);
    e.depth = static_cast<int8_t>(depth);
    e.best_move_raw = static_cast<uint16_t>(best.to_from());
    e.flag = flag;
}

// Returns true if we found a usable TT entry. Sets hash_move always if key matches.
static bool tt_probe(uint64_t key, int depth, int alpha, int beta, int& score,
                     Move& hash_move, int ply) {
    const TTEntry& e = transposition_table[tt_index(key)];
    if (e.key != key) return false;

    // Always extract hash move for ordering
    hash_move = Move(e.best_move_raw);

    // Only use score if depth is sufficient
    if (e.depth < depth) return false;

    int tt_score = e.score;
    // Adjust mate scores back from storage
    if (tt_score > 90000) tt_score -= ply;
    else if (tt_score < -90000) tt_score += ply;

    if (e.flag == TT_EXACT) {
        score = tt_score;
        return true;
    }
    if (e.flag == TT_BETA && tt_score >= beta) {
        score = tt_score;
        return true;
    }
    if (e.flag == TT_ALPHA && tt_score <= alpha) {
        score = tt_score;
        return true;
    }

    return false;
}

// ============= Move Scoring =============

static constexpr int HASH_MOVE_SCORE  = 10000000;
static constexpr int CAPTURE_BASE     = 1000000;
static constexpr int KILLER1_SCORE    = 900000;
static constexpr int KILLER2_SCORE    = 800000;

static int score_move(const Move& m, Board& board, const SearchContext* ctx,
                      int ply, Move hash_move) {
    // Hash move highest priority
    if (m.to_from() == hash_move.to_from() && hash_move.to_from() != 0) {
        return HASH_MOVE_SCORE;
    }

    // Captures: MVV-LVA
    if (m.is_capture()) {
        PieceType attacker = board.get_piece_type_on_square(m.from());
        PieceType victim   = board.get_piece_type_on_square(m.to());
        if (victim == NO_PIECE_TYPE) victim = PAWN; // en passant
        return CAPTURE_BASE + PIECE_VALUE[victim] - PIECE_VALUE[attacker];
    }

    // Killer moves
    if (ply < 64) {
        if (m.to_from() == ctx->killers[ply][0].to_from()) return KILLER1_SCORE;
        if (m.to_from() == ctx->killers[ply][1].to_from()) return KILLER2_SCORE;
    }

    // History heuristic
    Color c = board.get_player_to_move();
    return ctx->history[c][m.from()][m.to()];
}

static void order_moves_scored(Move* moves, int* scores, int count) {
    // Selection sort — good enough for small arrays, avoids allocation
    for (int i = 0; i < count - 1; i++) {
        int best_idx = i;
        for (int j = i + 1; j < count; j++) {
            if (scores[j] > scores[best_idx]) best_idx = j;
        }
        if (best_idx != i) {
            std::swap(moves[i], moves[best_idx]);
            std::swap(scores[i], scores[best_idx]);
        }
    }
}

// ============= Quiescence Search =============

static constexpr int DELTA_MARGIN = 900; // queen value

static int quiescence_search(Board& board, int alpha, int beta, SearchContext* ctx) {
    ctx->nodes++;

    // Stand-pat: static evaluation as a lower bound.
    int stand_pat = evaluate(board);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    // Delta pruning: if even capturing a queen can't raise us to alpha, bail out.
    if (stand_pat + DELTA_MARGIN < alpha) return alpha;

    Move captures[256];
    int count = board.get_legal_captures(captures);

    // Simple MVV-LVA ordering for captures
    int scores[256];
    for (int i = 0; i < count; i++) {
        PieceType attacker = board.get_piece_type_on_square(captures[i].from());
        PieceType victim   = board.get_piece_type_on_square(captures[i].to());
        if (victim == NO_PIECE_TYPE) victim = PAWN;
        scores[i] = PIECE_VALUE[victim] - PIECE_VALUE[attacker];
    }
    order_moves_scored(captures, scores, count);

    for (int i = 0; i < count; i++) {
        board.move(captures[i]);
        int score = -quiescence_search(board, -beta, -alpha, ctx);
        board.undo_move(captures[i]);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

// ============= Negamax with Alpha-Beta, NMP, PVS, LMR =============

static int negamax(Board& board, int depth, int alpha, int beta,
                   SearchContext* ctx, int ply, bool no_null = false) {
    bool in_check = board.is_in_check(board.get_player_to_move());

    // Check extension: don't enter QS while in check
    if (depth <= 0 && !in_check) {
        return quiescence_search(board, alpha, beta, ctx);
    }
    if (depth <= 0 && in_check) {
        depth = 1;
    }

    ctx->nodes++;

    bool is_pv = (beta - alpha) > 1;

    // Draw detection
    if (board.get_halfmove_clock() >= 100 || board.is_insufficient_material()) {
        return 0;
    }

    // ---- TT Probe ----
    uint64_t hash = board.get_hash();
    Move hash_move;
    int tt_score;
    if (tt_probe(hash, depth, alpha, beta, tt_score, hash_move, ply)) {
        return tt_score;
    }

    // ---- Null Move Pruning ----
    if (!in_check && depth >= 3 && !is_pv && !no_null &&
        board.has_non_pawn_material(board.get_player_to_move())) {
        int R = 3;
        board.make_null_move();
        int null_score = -negamax(board, depth - 1 - R, -beta, -beta + 1, ctx, ply + 1, true);
        board.undo_null_move();

        if (null_score >= beta) {
            return beta;
        }
    }

    // ---- Move Generation ----
    Move moves[256];
    int count = board.get_legal_moves(moves);

    // Terminal detection
    if (count == 0) {
        if (in_check) {
            return -100000 + ply; // prefer shorter mates
        }
        return 0; // stalemate
    }

    // ---- Move Ordering ----
    int scores[256];
    for (int i = 0; i < count; i++) {
        scores[i] = score_move(moves[i], board, ctx, ply, hash_move);
    }
    order_moves_scored(moves, scores, count);

    // ---- Search Moves ----
    Move best_move = moves[0];
    int best = INT_MIN;
    TTFlag tt_flag = TT_ALPHA;

    for (int i = 0; i < count; i++) {
        bool is_capture = moves[i].is_capture();
        bool is_killer = (ply < 64) &&
            (moves[i].to_from() == ctx->killers[ply][0].to_from() ||
             moves[i].to_from() == ctx->killers[ply][1].to_from());

        board.move(moves[i]);

        int score;

        // ---- LMR ----
        bool do_lmr = (i >= 3 && depth >= 3 && !in_check && !is_capture && !is_killer);
        int reduction = 0;
        if (do_lmr) {
            reduction = (i >= 6) ? 2 : 1;
        }

        if (i == 0) {
            // PVS: first move - full window
            score = -negamax(board, depth - 1, -beta, -alpha, ctx, ply + 1);
        } else {
            // PVS: null window search (with LMR reduction)
            score = -negamax(board, depth - 1 - reduction, -alpha - 1, -alpha, ctx, ply + 1);

            // Re-search at full depth if LMR reduced search failed high
            if (reduction > 0 && score > alpha) {
                score = -negamax(board, depth - 1, -alpha - 1, -alpha, ctx, ply + 1);
            }

            // PVS re-search with full window if null window failed high
            if (score > alpha && score < beta) {
                score = -negamax(board, depth - 1, -beta, -alpha, ctx, ply + 1);
            }
        }

        board.undo_move(moves[i]);

        if (score > best) {
            best = score;
            best_move = moves[i];
        }
        if (score > alpha) {
            alpha = score;
            tt_flag = TT_EXACT;
        }
        if (alpha >= beta) {
            tt_flag = TT_BETA;

            // Update killers and history for quiet beta cutoffs
            if (!is_capture && ply < 64) {
                // Shift killer slots
                if (moves[i].to_from() != ctx->killers[ply][0].to_from()) {
                    ctx->killers[ply][1] = ctx->killers[ply][0];
                    ctx->killers[ply][0] = moves[i];
                }

                // History bonus — after undo, player_to_move is the side that made the move
                Color mover = board.get_player_to_move();
                int bonus = depth * depth;
                int& h = ctx->history[mover][moves[i].from()][moves[i].to()];
                h += bonus;
                if (h > 1000000) h = 1000000;
            }

            break;
        }
    }

    // ---- TT Store ----
    tt_store(hash, best, depth, best_move, tt_flag, ply);

    return best;
}

// ============= Top-level Search (Iterative Deepening) =============

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

    // Clear search context (killers + history) per search call
    SearchContext ctx;
    ctx.clear();

    // TT persists across calls (static array) — no clearing needed

    // Iterative deepening
    for (int d = 1; d <= depth; d++) {
        ctx.nodes = 0;

        int alpha = INT_MIN + 1;
        int beta  = INT_MAX;
        int best_score = INT_MIN;
        Move best_move = moves[0];

        // Score and order moves using TT from previous iteration
        uint64_t hash = board.get_hash();
        Move hash_move;
        int dummy;
        tt_probe(hash, 0, alpha, beta, dummy, hash_move, 0);

        int scores[256];
        for (int i = 0; i < count; i++) {
            scores[i] = score_move(moves[i], board, &ctx, 0, hash_move);
        }
        order_moves_scored(moves, scores, count);

        for (int i = 0; i < count; i++) {
            board.move(moves[i]);

            int score;
            if (i == 0) {
                score = -negamax(board, d - 1, -beta, -alpha, &ctx, 1);
            } else {
                // PVS null window
                score = -negamax(board, d - 1, -alpha - 1, -alpha, &ctx, 1);
                if (score > alpha && score < beta) {
                    score = -negamax(board, d - 1, -beta, -alpha, &ctx, 1);
                }
            }

            board.undo_move(moves[i]);

            if (score > best_score) {
                best_score = score;
                best_move = moves[i];
            }
            if (score > alpha) alpha = score;
        }

        result.best_move = best_move;
        result.score = best_score;
        result.nodes += ctx.nodes;

        // Store root position in TT
        tt_store(hash, best_score, d, best_move, TT_EXACT, 0);
    }

    return result;
}
