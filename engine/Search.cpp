#include "Search.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

// ============= Opening Book =============
// Maps FEN position key (board + side + castling + ep, no clocks) → list of good UCI moves.

static const std::unordered_map<std::string, std::vector<std::string>> OPENING_BOOK = {
    // Starting position (white moves)
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
     {"e2e4", "d2d4", "g1f3", "c2c4"}},

    // After 1.e4 (black moves)
    {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -",
     {"e7e5", "c7c5", "e7e6", "d7d5", "g8f6", "d7d6"}},

    // After 1.d4 (black moves)
    {"rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq -",
     {"d7d5", "g8f6", "e7e6", "g7g6"}},

    // After 1.Nf3 (black moves)
    {"rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq -",
     {"d7d5", "g8f6", "c7c5"}},

    // After 1.c4 (black moves)
    {"rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq -",
     {"e7e5", "g8f6", "c7c5"}},

    // === After 1.e4 e5 (white moves) ===
    {"rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
     {"g1f3", "f1c4", "b1c3"}},

    // After 1.e4 e5 2.Nf3 (black moves) — Nc6, Nf6 (Petrov), d6 (Philidor)
    {"rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq -",
     {"b8c6", "g8f6", "d7d6"}},

    // After 1.e4 e5 2.Nf3 Nc6 (white moves) — Bb5 (Ruy Lopez), Bc4 (Italian), d4 (Scotch)
    {"r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -",
     {"f1b5", "f1c4", "d2d4"}},

    // Italian: 1.e4 e5 2.Nf3 Nc6 3.Bc4 (black moves) — Bc5, Nf6 (Two Knights)
    {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq -",
     {"f8c5", "g8f6"}},

    // Ruy Lopez: 1.e4 e5 2.Nf3 Nc6 3.Bb5 (black moves) — a6 (Morphy), Nf6 (Berlin), d6
    {"r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq -",
     {"a7a6", "g8f6", "d7d6"}},

    // === After 1.e4 c5 — Sicilian (white moves) ===
    {"rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
     {"g1f3", "b1c3", "c2c3"}},

    // Sicilian Open: 1.e4 c5 2.Nf3 (black moves) — d6 (Najdorf/Dragon), Nc6, e6
    {"rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq -",
     {"d7d6", "b8c6", "e7e6"}},

    // Sicilian: 1.e4 c5 2.Nf3 d6 (white moves) — d4
    {"rnbqkbnr/pp2pppp/3p4/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -",
     {"d2d4"}},

    // Sicilian: 1.e4 c5 2.Nf3 Nc6 (white moves) — d4, Bb5 (Rossolimo)
    {"r1bqkbnr/pp1ppppp/2n5/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -",
     {"d2d4", "f1b5"}},

    // Sicilian: 1.e4 c5 2.Nf3 e6 (white moves) — d4
    {"rnbqkbnr/pp1p1ppp/4p3/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -",
     {"d2d4"}},

    // === After 1.e4 e6 — French (white moves) ===
    {"rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
     {"d2d4", "g1f3"}},

    // French: 1.e4 e6 2.d4 (black moves) — d5
    {"rnbqkbnr/pppp1ppp/4p3/8/3PP3/8/PPP2PPP/RNBQKBNR b KQkq -",
     {"d7d5"}},

    // === After 1.e4 d5 — Scandinavian (white moves) ===
    {"rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
     {"e4d5"}},

    // === After 1.e4 Nf6 — Alekhine (white moves) ===
    {"rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
     {"e4e5", "b1c3"}},

    // === After 1.e4 d6 — Pirc (white moves) ===
    {"rnbqkbnr/ppp1pppp/3p4/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -",
     {"d2d4", "g1f3"}},

    // === After 1.d4 d5 (white moves) — Queen's Gambit etc. ===
    {"rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -",
     {"c2c4", "g1f3", "c1f4"}},

    // QGD: 1.d4 d5 2.c4 (black moves) — e6 (QGD), c6 (Slav), dxc4 (QGA)
    {"rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq -",
     {"e7e6", "c7c6", "d5c4"}},

    // QGD: 1.d4 d5 2.c4 e6 (white moves) — Nc3, Nf3
    {"rnbqkbnr/ppp2ppp/4p3/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq -",
     {"b1c3", "g1f3"}},

    // Slav: 1.d4 d5 2.c4 c6 (white moves) — Nf3, Nc3
    {"rnbqkbnr/pp2pppp/2p5/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq -",
     {"g1f3", "b1c3"}},

    // === After 1.d4 Nf6 (white moves) ===
    {"rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -",
     {"c2c4", "g1f3"}},

    // Indian: 1.d4 Nf6 2.c4 (black moves) — e6 (Nimzo/QID), g6 (KID/Grunfeld), c5 (Benoni)
    {"rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq -",
     {"e7e6", "g7g6", "c7c5"}},

    // Indian: 1.d4 Nf6 2.c4 e6 (white moves) — Nc3, Nf3, g3
    {"rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq -",
     {"b1c3", "g1f3", "g2g3"}},

    // Indian: 1.d4 Nf6 2.c4 g6 (white moves) — Nc3
    {"rnbqkb1r/pppppp1p/5np1/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq -",
     {"b1c3"}},

    // KID: 1.d4 Nf6 2.c4 g6 3.Nc3 (black moves) — Bg7
    {"rnbqkb1r/pppppp1p/5np1/8/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq -",
     {"f8g7"}},

    // === After 1.d4 e6 (white moves) ===
    {"rnbqkbnr/pppp1ppp/4p3/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -",
     {"c2c4", "g1f3", "e2e4"}},

    // === After 1.d4 g6 (white moves) — Modern ===
    {"rnbqkbnr/pppppp1p/6p1/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -",
     {"c2c4", "e2e4"}},

    // === After 1.Nf3 d5 (white moves) ===
    {"rnbqkbnr/ppp1pppp/8/3p4/8/5N2/PPPPPPPP/RNBQKB1R w KQkq -",
     {"d2d4", "g2g3", "c2c4"}},

    // === After 1.Nf3 Nf6 (white moves) ===
    {"rnbqkb1r/pppppppp/5n2/8/8/5N2/PPPPPPPP/RNBQKB1R w KQkq -",
     {"d2d4", "c2c4", "g2g3"}},

    // === After 1.c4 e5 (white moves) — English/Reversed Sicilian ===
    {"rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq -",
     {"b1c3", "g2g3", "g1f3"}},

    // === After 1.c4 Nf6 (white moves) ===
    {"rnbqkb1r/pppppppp/5n2/8/2P5/8/PP1PPPPP/RNBQKBNR w KQkq -",
     {"b1c3", "g1f3", "d2d4"}},

    // === After 1.c4 c5 (white moves) — Symmetrical English ===
    {"rnbqkbnr/pp1ppppp/8/2p5/2P5/8/PP1PPPPP/RNBQKBNR w KQkq -",
     {"g1f3", "b1c3"}},
};

// Strip halfmove and fullmove clocks from FEN to get position key.
static std::string fen_position_key(const std::string& fen) {
    // FEN has 6 fields separated by spaces. We want the first 4.
    int spaces = 0;
    for (size_t i = 0; i < fen.size(); i++) {
        if (fen[i] == ' ') {
            spaces++;
            if (spaces == 4) {
                return fen.substr(0, i);
            }
        }
    }
    return fen; // fallback: return whole thing
}

// Look up the opening book. Returns a random book move (UCI string), or empty if no hit.
std::string book_lookup(const std::string& fen) {
    std::string key = fen_position_key(fen);
    auto it = OPENING_BOOK.find(key);
    if (it == OPENING_BOOK.end()) return "";
    const auto& moves = it->second;
    return moves[std::rand() % moves.size()];
}

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

int evaluate(Board& board, int noise) {
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
    int eval = (board.get_player_to_move() == WHITE) ? score : -score;

    // Noise perturbs every leaf evaluation, making weaker bots misjudge positions.
    if (noise > 0) {
        eval += (std::rand() % (noise * 2 + 1)) - noise;
    }

    return eval;
}

// ============= Transposition Table =============

static TTEntry transposition_table[TT_SIZE];

static inline int tt_index(uint64_t key) {
    return key & (TT_SIZE - 1);
}

static void tt_store(uint64_t key, int score, int depth, Move best, TTFlag flag, int ply) {
    // Adjust mate scores for storage (make them root-independent).
    int stored_score = score;
    if (stored_score > 90000)  stored_score += ply;
    else if (stored_score < -90000) stored_score -= ply;

    TTEntry& e = transposition_table[tt_index(key)];
    // Depth-preferred replacement: preserve deep results from being overwritten by
    // shallower searches on the same slot, unless the position is different (collision).
    if (e.key != key || depth >= static_cast<int>(e.depth)) {
        e.key          = key;
        e.score        = static_cast<int16_t>(stored_score);
        e.depth        = static_cast<int8_t>(depth);
        e.best_move_raw = static_cast<uint16_t>(best.to_from());
        e.flag         = flag;
    }
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
    if (tt_score > 90000)  tt_score -= ply;
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

static int quiescence_search(Board& board, int alpha, int beta, SearchContext* ctx, int noise = 0) {
    ctx->nodes++;

    // Stand-pat: static evaluation as a lower bound.
    int stand_pat = evaluate(board, noise);
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
        int score = -quiescence_search(board, -beta, -alpha, ctx, noise);
        board.undo_move(captures[i]);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

// ============= Negamax with Alpha-Beta, NMP, PVS, LMR =============

static int negamax(Board& board, int depth, int alpha, int beta,
                   SearchContext* ctx, int ply, bool no_null = false, int noise = 0) {
    bool in_check = board.is_in_check(board.get_player_to_move());

    // Check extension: don't enter QS while in check.
    if (depth <= 0 && !in_check) {
        return quiescence_search(board, alpha, beta, ctx, noise);
    }
    if (depth <= 0 && in_check) {
        depth = 1;
    }

    ctx->nodes++;

    bool is_pv = (beta - alpha) > 1;

    // ---- Repetition and Draw detection ----
    uint64_t hash = board.get_hash();

    // In-search repetition: check if the current position appeared earlier on this
    // exact search path (same side to move, hence step -2). If so, score as draw.
    for (int i = ply - 2; i >= 0; i -= 2) {
        if (ctx->path_hashes[i] == hash) return 0;
    }
    ctx->path_hashes[ply] = hash;

    if (board.get_halfmove_clock() >= 100 || board.is_insufficient_material()) {
        return 0;
    }

    // ---- TT Probe ----
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
        int null_score = -negamax(board, depth - 1 - R, -beta, -beta + 1, ctx, ply + 1, true, noise);
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

        // Don't apply LMR to moves that give check — they need full-depth verification.
        bool gives_check = board.is_in_check(board.get_player_to_move());

        int score;

        // ---- LMR ----
        bool do_lmr = (i >= 3 && depth >= 3 && !in_check && !is_capture
                       && !is_killer && !gives_check);
        int reduction = 0;
        if (do_lmr) {
            reduction = (i >= 6) ? 2 : 1;
        }

        if (i == 0) {
            // PVS: first move — full window
            score = -negamax(board, depth - 1, -beta, -alpha, ctx, ply + 1, false, noise);
        } else {
            // PVS: null window search (with LMR reduction)
            score = -negamax(board, depth - 1 - reduction, -alpha - 1, -alpha, ctx, ply + 1, false, noise);

            // Re-search at full depth if LMR reduced search failed high
            if (reduction > 0 && score > alpha) {
                score = -negamax(board, depth - 1, -alpha - 1, -alpha, ctx, ply + 1, false, noise);
            }

            // PVS re-search with full window if null window failed high
            if (score > alpha && score < beta) {
                score = -negamax(board, depth - 1, -beta, -alpha, ctx, ply + 1, false, noise);
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

SearchResult search(Board& board, int depth, int noise) {
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

    // Seed path_hashes with the root position so repetition detection in negamax
    // can see the position the bot was called from (ply 0).
    ctx.path_hashes[0] = board.get_hash();

    // TT persists across calls (static array) — no clearing needed

    // Iterative deepening with clean PVS on every iteration.
    // Noise is threaded into evaluate() at leaf nodes — no root perturbation needed.
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
                // PVS: first move — full window
                score = -negamax(board, d - 1, -beta, -alpha, &ctx, 1, false, noise);
            } else {
                // PVS: null window
                score = -negamax(board, d - 1, -alpha - 1, -alpha, &ctx, 1, false, noise);
                if (score > alpha && score < beta) {
                    score = -negamax(board, d - 1, -beta, -alpha, &ctx, 1, false, noise);
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
