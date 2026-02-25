#include "Board.h"

#include <array>
#include <cassert>

#include "BitboardUtils.h"
#include <sstream>
#include <vector>
#include <iostream>

#include "Move.h"

// Constants
namespace {
    const std::string whitePiecesString = "PNBRQK";
    const std::string blackPiecesString = "pnbrqk";
}

// Precompute attack/move tables
consteval auto init_pawn_pushes_table() {
    std::array<std::array<Bitboard, 64>, 2> table = {};

    for (int sq = 0; sq < 64; ++sq) {
        const Bitboard bb = 1ULL << sq;

        // White pawn pushes
        table[WHITE][sq] = (bb << 8); // Single push
        if (bb & 0xFF00ULL) {
            // 2nd rank
            table[WHITE][sq] |= (bb << 16); // Add double push
        }

        // Black pawn pushes
        table[BLACK][sq] = (bb >> 8); // Single push
        if (bb & 0xFF000000000000ULL) {
            // 7th rank
            table[BLACK][sq] |= (bb >> 16); // Add double push
        }
    }
    return table;
}

consteval auto init_pawn_attacks() {
    std::array<std::array<Bitboard, 64>, 2> table = {};
    const Bitboard not_a_file = ~0x0101010101010101ULL;
    const Bitboard not_h_file = ~0x8080808080808080ULL;

    for (int square = 0; square < 64; square++) {
        // White pawn attacks
        Bitboard west_attack = BitboardUtil::square_to_bitboard(static_cast<Square>(square)) << 7;
        Bitboard east_attack = BitboardUtil::square_to_bitboard(static_cast<Square>(square)) << 9;
        table[WHITE][square] = (west_attack & not_h_file) | (east_attack & not_a_file);

        // Black pawn attacks
        east_attack = BitboardUtil::square_to_bitboard(static_cast<Square>(square)) >> 7;
        west_attack = BitboardUtil::square_to_bitboard(static_cast<Square>(square)) >> 9;
        table[BLACK][square] = (west_attack & not_h_file) | (east_attack & not_a_file);
    }

    return table;
}

consteval auto init_knight_attacks() {
    std::array<Bitboard, 64> table = {};

    const Bitboard not_a_file = 0xfefefefefefefefeULL;
    const Bitboard not_ab_file = 0xfcfcfcfcfcfcfcfcULL;
    const Bitboard not_h_file = 0x7f7f7f7f7f7f7f7fULL;
    const Bitboard not_gh_file = 0x3f3f3f3f3f3f3f3fULL;

    for (int square = 0; square < 64; square++) {
        Bitboard bb = BitboardUtil::square_to_bitboard(static_cast<Square>(square));
        Bitboard attacks = 0ULL;

        // 8 possible knight jumps
        attacks |= (bb << 17) & not_a_file;
        attacks |= (bb << 15) & not_h_file;
        attacks |= (bb << 10) & not_ab_file;
        attacks |= (bb << 6) & not_gh_file;
        attacks |= (bb >> 17) & not_h_file;
        attacks |= (bb >> 15) & not_a_file;
        attacks |= (bb >> 10) & not_gh_file;
        attacks |= (bb >> 6) & not_ab_file;

        table[square] = attacks;
    }

    return table;
}

consteval auto init_bishop_masks() {
    std::array<Bitboard, 64> masks = {};
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = 0ULL;
        int rank = sq / 8;
        int file = sq % 8;

        // Diagonal directions: SE, SW, NE, NW
        // stop before the board edges (i.e. r < 7/f < 7 and r > 0/f > 0)
        for (int r = rank + 1, f = file + 1; r < 7 && f < 7; ++r, ++f) {
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
        }
        for (int r = rank + 1, f = file - 1; r < 7 && f > 0; ++r, --f) {
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
        }
        for (int r = rank - 1, f = file + 1; r > 0 && f < 7; --r, ++f) {
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
        }
        for (int r = rank - 1, f = file - 1; r > 0 && f > 0; --r, --f) {
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
        }
        masks[sq] = mask;
    }
    return masks;
}

constexpr auto BISHOP_MASKS = init_bishop_masks();

inline std::array<std::array<Bitboard, 512>, 64> init_bishop_attacks() {
    std::array<std::array<Bitboard, 512>, 64> table = {};
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = BISHOP_MASKS[sq];
        int num_bits = __builtin_popcountll(mask);
        int num_occupancies = 1 << num_bits;

        for (int occ_idx = 0; occ_idx < num_occupancies; ++occ_idx) {
            Bitboard occ = 0ULL;
            Bitboard attacks = 0ULL;

            // Set occupancy bits
            Bitboard temp_mask = mask;
            for (int i = 0; i < num_bits; ++i) {
                int lsb = __builtin_ctzll(temp_mask);
                if (occ_idx & (1 << i)) {
                    occ |= BitboardUtil::square_to_bitboard(static_cast<Square>(lsb));
                }
                temp_mask &= temp_mask - 1; // Clear LSB
            }

            // Simulate bishop attacks with occupancy
            int rank = sq / 8;
            int file = sq % 8;
            for (int r = rank + 1, f = file + 1; r < 8 && f < 8; ++r, ++f) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f))) break;
            }
            for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; ++r, --f) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f))) break;
            }
            for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; --r, ++f) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f))) break;
            }
            for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; --r, --f) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + f))) break;
            }
            table[sq][occ_idx] = attacks;
        }
    }
    return table;
}

consteval auto init_rook_masks() {
    std::array<Bitboard, 64> masks = {};
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = 0ULL;
        int rank = sq / 8;
        int file = sq % 8;

        // North
        for (int r = rank + 1; r < 7; ++r)
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + file));

        // South
        for (int r = rank - 1; r > 0; --r)
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + file));

        // East
        for (int f = file + 1; f < 7; ++f)
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(rank * 8 + f));

        // West
        for (int f = file - 1; f > 0; --f)
            mask |= BitboardUtil::square_to_bitboard(static_cast<Square>(rank * 8 + f));

        masks[sq] = mask;
    }
    return masks;
}


constexpr auto ROOK_MASKS = init_rook_masks();

inline std::array<std::array<Bitboard, 4096>, 64> init_rook_attacks() {
    std::array<std::array<Bitboard, 4096>, 64> table = {};
    for (int sq = 0; sq < 64; ++sq) {
        Bitboard mask = ROOK_MASKS[sq];
        int num_bits = __builtin_popcountll(mask);
        int num_occupancies = 1 << num_bits;

        for (int occ_idx = 0; occ_idx < num_occupancies; ++occ_idx) {
            Bitboard occ = 0ULL;
            Bitboard attacks = 0ULL;

            // Set occupancy bits
            Bitboard temp_mask = mask;
            for (int i = 0; i < num_bits; ++i) {
                int lsb = __builtin_ctzll(temp_mask);
                if (occ_idx & (1 << i)) {
                    occ |= BitboardUtil::square_to_bitboard(static_cast<Square>(lsb));
                }
                temp_mask &= temp_mask - 1; // Clear LSB
            }

            // Simulate rook attacks with occupancy
            int rank = sq / 8;
            int file = sq % 8;

            for (int r = rank + 1; r < 8; ++r) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + file));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + file))) break;
            }
            for (int r = rank - 1; r >= 0; --r) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + file));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(r * 8 + file))) break;
            }
            for (int f = file + 1; f < 8; ++f) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(rank * 8 + f));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(rank * 8 + f))) break;
            }
            for (int f = file - 1; f >= 0; --f) {
                attacks |= BitboardUtil::square_to_bitboard(static_cast<Square>(rank * 8 + f));
                if (occ & BitboardUtil::square_to_bitboard(static_cast<Square>(rank * 8 + f))) break;
            }

            table[sq][occ_idx] = attacks;
        }
    }
    return table;
}

consteval auto init_king_attacks() {
    std::array<Bitboard, 64> table = {};

    const Bitboard not_a_file = 0xfefefefefefefefeULL;
    const Bitboard not_h_file = 0x7f7f7f7f7f7f7f7fULL;

    const Bitboard not_first_rank = ~0xFFULL;
    const Bitboard not_last_rank = ~0xFF00000000000000ULL;

    for (int square = 0; square < 64; square++) {
        Bitboard bb = BitboardUtil::square_to_bitboard(static_cast<Square>(square));
        Bitboard attacks = 0ULL;

        // 8 possible king moves
        attacks |= (bb << 9) & not_a_file & not_first_rank; // up-right
        attacks |= (bb << 8) & not_first_rank; // up
        attacks |= (bb << 7) & not_h_file & not_first_rank; //up-left

        attacks |= (bb << 1) & not_a_file; // left
        attacks |= (bb >> 1) & not_h_file; // right

        attacks |= (bb >> 7) & not_a_file & not_last_rank; // down-right
        attacks |= (bb >> 8) & not_last_rank; // down
        attacks |= (bb >> 9) & not_h_file & not_last_rank; // down-left

        table[square] = attacks;
    }

    return table;
}

constexpr auto PAWN_PUSHES = init_pawn_pushes_table();
constexpr auto PAWN_ATTACKS = init_pawn_attacks();
constexpr auto KNIGHT_ATTACKS = init_knight_attacks();
std::array<std::array<Bitboard, 512>, 64> BISHOP_ATTACKS = init_bishop_attacks();
std::array<std::array<Bitboard, 4096>, 64> ROOK_ATTACKS = init_rook_attacks();
constexpr auto KING_ATTACKS = init_king_attacks();


// ============= Initialization Methods =============


Board::Board() {
    // Clear bitboards
    for (auto &color: bitboards) {
        for (auto &pieceBitboard: color) {
            pieceBitboard = 0ULL;
        }
    }

    // Clear occupancies
    for (auto &occ: occupancy) {
        occ = 0ULL;
    }

    // Clear mailbox
    for (auto &piece: mailbox) {
        piece = {NO_PIECE_TYPE, WHITE};
    }

    // Set default game state
    player_to_move = WHITE;
    castling_rights = {true, true, true, true};
    game_ply = 0;
    full_move_counter = 1;
    halfmove_clock = 0;
}

// ============= Setup Methods =============


void Board::setup() {
    setup_with_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}


void Board::setup_with_fen(std::string fen) {
    std::istringstream fen_stream(fen);
    std::vector<std::string> tokens;
    std::string token;

    // Split FEN into tokens
    while (std::getline(fen_stream, token, ' ')) {
        tokens.push_back(token);
    }

    if (tokens.size() < 6) return;

    // Parse piece placement
    std::istringstream pieces_stream(tokens[0]);
    std::vector<std::string> ranks;
    while (std::getline(pieces_stream, token, '/')) {
        ranks.insert(ranks.begin(), token);
    }

    int square = 0;
    for (const std::string &rank: ranks) {
        for (char c: rank) {
            if (whitePiecesString.contains(c)) {
                size_t type_idx = whitePiecesString.find(c);
                Bitboard bb = BitboardUtil::square_to_bitboard(static_cast<Square>(square));
                bitboards[WHITE][type_idx] |= bb;
                occupancy[WHITE] |= bb;
                occupancy[BOTH] |= bb;
                mailbox[square] = {static_cast<PieceType>(type_idx), WHITE};
                square++;
            } else if (blackPiecesString.contains(c)) {
                size_t type_idx = blackPiecesString.find(c);
                Bitboard bb = BitboardUtil::square_to_bitboard(static_cast<Square>(square));
                bitboards[BLACK][type_idx] |= bb;
                occupancy[BLACK] |= bb;
                occupancy[BOTH] |= bb;
                mailbox[square] = {static_cast<PieceType>(type_idx), BLACK};
                square++;
            } else {
                int empty_squares = c - '0';
                for (int i = 0; i < empty_squares; i++) {
                    mailbox[square++] = {NO_PIECE_TYPE, WHITE};
                }
            }
        }
    }

    // Parse active color
    player_to_move = (tokens[1] == "w") ? WHITE : BLACK;

    // Parse castling rights
    castling_rights = {false, false, false, false};
    for (char c: tokens[2]) {
        if (c == 'K') castling_rights.white_king_side = true;
        else if (c == 'Q') castling_rights.white_queen_side = true;
        else if (c == 'k') castling_rights.black_king_side = true;
        else if (c == 'q') castling_rights.black_queen_side = true;
    }


    // Parse move counters
    game_ply = 0;
    halfmove_clock = std::stoi(tokens[4]);
    full_move_counter = std::stoi(tokens[5]);

    // Parse en passant (Bug 1 fix: write to history[0] directly)
    history[0].epsq = (tokens[3] == "-") ? NO_SQUARE : SquareMap.at(tokens[3]);

    // Bug 3 fix: save parsed castling rights into history[0] so undo_move restores correctly
    history[0].castling_rights = castling_rights;
    history[0].halfmove_clock = halfmove_clock;
    history[0].full_move_counter = full_move_counter;
}

// ============= Move Execution =============

void Board::move(Move m) {

    const Square from = m.from();
    const Square to = m.to();
    const MoveFlags type = m.flags();

    game_ply++;
    history[game_ply] = UndoInfo(history[game_ply - 1]);
    history[game_ply].entry |= BitboardUtil::square_to_bitboard(to) | BitboardUtil::square_to_bitboard(from);

    auto piece_type = get_piece_type_on_square(from);
    switch (piece_type) {
        case KING:
            if (player_to_move == WHITE) {
                castling_rights.white_king_side = false;
                castling_rights.white_queen_side = false;
            } else {
                castling_rights.black_king_side = false;
                castling_rights.black_queen_side = false;
            }
            break;
        case ROOK:
            if (player_to_move == WHITE) {
                if (from == a1) castling_rights.white_queen_side = false;
                if (from == h1) castling_rights.white_king_side = false;
            } else {
                if (from == a8) castling_rights.black_queen_side = false;
                if (from == h8) castling_rights.black_king_side = false;
            }
            break;
    }

    auto to_piece_type = get_piece_type_on_square(to);
    if (to_piece_type == ROOK) {
        if (player_to_move == WHITE) {
            if (to == a8) castling_rights.black_queen_side = false;
            if (to == h8) castling_rights.black_king_side = false;
        } else {
            if (to == a1) castling_rights.white_queen_side = false;
            if (to == h1) castling_rights.white_king_side = false;
        }
    }

    switch (type) {
        case QUIET:
            // no piece at destination
            make_quiet_move(from, to);
            break;
        case DOUBLE_PUSH:
            // double pawn push
            make_quiet_move(from, to);
            history[game_ply].epsq = Square(m.from() + relative_dir(NORTH));
            break;
        case OO:
            // king side castle
            if (player_to_move == WHITE) {
                make_quiet_move(e1, g1);
                make_quiet_move(h1, f1);
            } else {
                make_quiet_move(e8, g8);
                make_quiet_move(h8, f8);
            }
            break;
        case OOO:
            // queen side castle
            if (player_to_move == WHITE) {
                make_quiet_move(e1, c1);
                make_quiet_move(a1, d1);
            } else {
                make_quiet_move(e8, c8);
                make_quiet_move(a8, d8);
            }
            break;
        case EN_PASSANT:
            make_quiet_move(from, to);
            remove_piece(static_cast<Square>(to + relative_dir(SOUTH)));
            break;
        case PR_KNIGHT:
            remove_piece(from);
            put_piece(to, Piece(KNIGHT, player_to_move));
            break;
        case PR_BISHOP:
            remove_piece(from);
            put_piece(to, Piece(BISHOP, player_to_move));
            break;
        case PR_ROOK:
            remove_piece(from);
            put_piece(to, Piece(ROOK, player_to_move));
            break;
        case PR_QUEEN:
            remove_piece(from);
            put_piece(to, Piece(QUEEN, player_to_move));
            break;
        case PC_KNIGHT:
            remove_piece(from);
            history[game_ply].captured = mailbox[to];
            remove_piece(to);
            put_piece(to, Piece(KNIGHT, player_to_move));
            break;
        case PC_BISHOP:
            remove_piece(from);
            history[game_ply].captured = mailbox[to];
            remove_piece(to);
            put_piece(to, Piece(BISHOP, player_to_move));
            break;
        case PC_ROOK:
            remove_piece(from);
            history[game_ply].captured = mailbox[to];
            remove_piece(to);
            put_piece(to, Piece(ROOK, player_to_move));
            break;
        case PC_QUEEN:
            remove_piece(from);
            history[game_ply].captured = mailbox[to];
            remove_piece(to);
            put_piece(to, Piece(QUEEN, player_to_move));
            break;
        case CAPTURE:
            history[game_ply].captured = mailbox[to];
            make_move(from, to);
            break;
    }

    history[game_ply].castling_rights = castling_rights;

    // Halfmove clock: reset on pawn move or capture, otherwise increment
    if (piece_type == PAWN || (type & CAPTURE)) {
        halfmove_clock = 0;
    } else {
        halfmove_clock++;
    }

    // Full-move counter: increment after Black's move
    if (player_to_move == BLACK) {
        full_move_counter++;
    }

    history[game_ply].halfmove_clock = halfmove_clock;
    history[game_ply].full_move_counter = full_move_counter;

    player_to_move = static_cast<Color>(!static_cast<bool>(player_to_move));
}

void Board::undo_move(Move m) {
    // move must be last move made
    const Square from = m.from();
    const Square to = m.to();
    const MoveFlags type = m.flags();

    player_to_move = static_cast<Color>(!static_cast<bool>(player_to_move));
    // switch player to move so it's the one who made the move

    switch (type) {
        case QUIET:
        case DOUBLE_PUSH:
            make_quiet_move(to, from);
            break;
        case OO:
            if (player_to_move == WHITE) {
                make_quiet_move(g1, e1);
                make_quiet_move(f1, h1);
            } else {
                make_quiet_move(g8, e8);
                make_quiet_move(f8, h8);
            }
            break;
        case OOO:
            if (player_to_move == WHITE) {
                make_quiet_move(c1, e1);
                make_quiet_move(d1, a1);
            } else {
                make_quiet_move(c8, e8);
                make_quiet_move(d8, a8);
            }
            break;
        case EN_PASSANT:
            make_quiet_move(to, from);
            put_piece(static_cast<Square>(to + relative_dir(SOUTH)),
                      Piece(PAWN, player_to_move == WHITE ? BLACK : WHITE));
            break;
        case PR_KNIGHT:
        case PR_BISHOP:
        case PR_ROOK:
        case PR_QUEEN:
            remove_piece(to);
            put_piece(from, Piece(PAWN, player_to_move));
            break;
        case PC_KNIGHT:
        case PC_BISHOP:
        case PC_ROOK:
        case PC_QUEEN:
            remove_piece(to);
            put_piece(to, history[game_ply].captured);
            put_piece(from, Piece(PAWN, player_to_move));
            break;
        case CAPTURE:
            make_quiet_move(to, from);
            put_piece(to, history[game_ply].captured);
            break;
    }

    game_ply--;
    castling_rights = history[game_ply].castling_rights;
    halfmove_clock = history[game_ply].halfmove_clock;
    full_move_counter = history[game_ply].full_move_counter;
}

// ============= Move Generation =============
Move *Board::generate_pseudo_legal_moves(Move *list) {
    // generates all possible moves, does not check for checks

    //todo: optmize by checking if is in check or checking pinned pieces

    auto pawns = bitboards[player_to_move][PAWN];
    while (pawns) {
        list = generate_pawn_moves(list, pop_lsb(pawns));
    }

    auto knights = bitboards[player_to_move][KNIGHT];
    while (knights) {
        list = generate_knight_moves(list, pop_lsb(knights));
    }

    auto bishops = bitboards[player_to_move][BISHOP];
    while (bishops) {
        list = generate_bishop_moves(list, pop_lsb(bishops));
    }

    auto rooks = bitboards[player_to_move][ROOK];
    while (rooks) {
        list = generate_rook_moves(list, pop_lsb(rooks));
    }

    auto queens = bitboards[player_to_move][QUEEN];
    while (queens) {
        list = generate_queen_moves(list, pop_lsb(queens));
    }

    auto king = bitboards[player_to_move][KING];
    auto king_sq = BitboardUtil::bitboard_to_square(king); // should never be more than one king per color
    list = generate_king_moves(list, king_sq);

    return list;
}

Move *Board::generate_pawn_moves(Move *list, const Square from_square) {
    const auto pushes_bb = PAWN_PUSHES[player_to_move][from_square];
    auto attacks_bb = PAWN_ATTACKS[player_to_move][from_square];
    const auto enemy = player_to_move == WHITE ? BLACK : WHITE;

    const Bitboard from_bb = BitboardUtil::square_to_bitboard(from_square);
    const Bitboard rank2 = 0x000000000000FF00ULL;
    const Bitboard rank7 = 0x00FF000000000000ULL;

    const auto dir = relative_dir(NORTH);

    if ((player_to_move == WHITE && from_bb & rank2) || (player_to_move == BLACK && from_bb & rank7)) {
        auto const first_square = static_cast<Square>(from_square + dir);
        auto const second_square = static_cast<Square>(first_square + dir);

        if (!(BitboardUtil::square_to_bitboard(first_square) & occupancy[BOTH])) {
            // no one on first square, add quiet move
            *list++ = Move(from_square, first_square, QUIET);

            if (!(BitboardUtil::square_to_bitboard(second_square) & occupancy[BOTH])) {
                // no one on second square, add double push
                *list++ = Move(from_square, second_square, DOUBLE_PUSH);
            }
        }
    } else if ((player_to_move == WHITE && from_bb & rank7) || (player_to_move == BLACK && from_bb & rank2)) {
        // pawn is about to promote
        if (!(pushes_bb & occupancy[BOTH])) {
            *list++ = Move(from_square, static_cast<Square>(from_square + dir), PR_KNIGHT);
            *list++ = Move(from_square, static_cast<Square>(from_square + dir), PR_BISHOP);
            *list++ = Move(from_square, static_cast<Square>(from_square + dir), PR_ROOK);
            *list++ = Move(from_square, static_cast<Square>(from_square + dir), PR_QUEEN);
        }
    } else {
        // only single pushes possible
        if (!(pushes_bb & occupancy[BOTH])) {
            *list++ = Move(from_square, BitboardUtil::bitboard_to_square(pushes_bb), QUIET);
        }
    }

    // attacks
    if ((player_to_move == WHITE && from_bb & rank7) || (player_to_move == BLACK && from_bb & rank2)) {
        // promotion captures
        while (attacks_bb) {
            auto to_square = pop_lsb(attacks_bb);
            if (BitboardUtil::square_to_bitboard(to_square) & occupancy[enemy]) {
                // enemy is on target square, capture move
                *list++ = Move(from_square, to_square, PC_KNIGHT);
                *list++ = Move(from_square, to_square, PC_BISHOP);
                *list++ = Move(from_square, to_square, PC_ROOK);
                *list++ = Move(from_square, to_square, PC_QUEEN);
            }
        }
    } else {
        // normal captures
        while (attacks_bb) {
            auto to_square = pop_lsb(attacks_bb);
            if (BitboardUtil::square_to_bitboard(to_square) & occupancy[enemy]) {
                // enemy is on target square, capture move
                *list++ = Move(from_square, to_square, CAPTURE);
            } else if (BitboardUtil::square_to_bitboard(to_square) & BitboardUtil::square_to_bitboard(
                           history[game_ply].epsq)) {
                // en passant
                *list++ = Move(from_square, to_square, EN_PASSANT);
            }
        }
    }

    return list;
}

Move *Board::generate_knight_moves(Move *list, const Square from_square) {
    auto attacks = KNIGHT_ATTACKS[from_square];
    attacks &= ~occupancy[player_to_move]; // remove bits with friendlies on them
    const auto enemy = player_to_move == WHITE ? BLACK : WHITE;

    while (attacks) {
        auto to_square = pop_lsb(attacks);
        if (BitboardUtil::square_to_bitboard(to_square) & occupancy[enemy]) {
            *list++ = Move(from_square, to_square, CAPTURE);
        } else {
            *list++ = Move(from_square, to_square, QUIET);
        }
    }

    return list;
}

Move *Board::generate_bishop_moves(Move *list, const Square from_square) {
    auto occ_idx = get_occupancy_index(from_square, BISHOP_MASKS);
    auto attacks = BISHOP_ATTACKS[from_square][occ_idx] & ~occupancy[player_to_move];

    const auto enemy = player_to_move == WHITE ? BLACK : WHITE;

    while (attacks) {
        auto to_square = pop_lsb(attacks);
        if (BitboardUtil::square_to_bitboard(to_square) & occupancy[enemy]) {
            *list++ = Move(from_square, to_square, CAPTURE);
        } else {
            *list++ = Move(from_square, to_square, QUIET);
        }
    }

    return list;
}

Move *Board::generate_rook_moves(Move *list, const Square from_square) {
    auto occ_idx = get_occupancy_index(from_square, ROOK_MASKS);
    auto attacks = ROOK_ATTACKS[from_square][occ_idx] & ~occupancy[player_to_move];

    const auto enemy = player_to_move == WHITE ? BLACK : WHITE;

    while (attacks) {
        auto to_square = pop_lsb(attacks);
        if (BitboardUtil::square_to_bitboard(to_square) & occupancy[enemy]) {
            *list++ = Move(from_square, to_square, CAPTURE);
        } else {
            *list++ = Move(from_square, to_square, QUIET);
        }
    }

    return list;
}

Move *Board::generate_queen_moves(Move *list, const Square from_square) {
    auto bishop_occ_idx = get_occupancy_index(from_square, BISHOP_MASKS);
    auto bishop_attacks = BISHOP_ATTACKS[from_square][bishop_occ_idx];

    auto rook_occ_idx = get_occupancy_index(from_square, ROOK_MASKS);
    auto rook_attacks = ROOK_ATTACKS[from_square][rook_occ_idx];

    auto attacks = (bishop_attacks | rook_attacks) & ~occupancy[player_to_move];
    const auto enemy = player_to_move == WHITE ? BLACK : WHITE;

    while (attacks) {
        auto to_square = pop_lsb(attacks);
        if (BitboardUtil::square_to_bitboard(to_square) & occupancy[enemy]) {
            *list++ = Move(from_square, to_square, CAPTURE);
        } else {
            *list++ = Move(from_square, to_square, QUIET);
        }
    }

    return list;
}

Move *Board::generate_king_moves(Move *list, Square from_square) {
    auto attacks = KING_ATTACKS[from_square];
    attacks &= ~occupancy[player_to_move]; // remove bits with friendlies on them
    const auto enemy = player_to_move == WHITE ? BLACK : WHITE;

    while (attacks) {
        auto to_square = pop_lsb(attacks);
        if (BitboardUtil::square_to_bitboard(to_square) & occupancy[enemy]) {
            *list++ = Move(from_square, to_square, CAPTURE);
        } else {
            *list++ = Move(from_square, to_square, QUIET);
        }
    }

    // check castling moves
    if (player_to_move == WHITE) {
        if (castling_rights.white_king_side) {
            // f1,g1 must be empty
            if (mailbox[f1].type == NO_PIECE_TYPE && mailbox[g1].type == NO_PIECE_TYPE) {
                // e1,f1,g1 must be safe
                if (!is_square_under_attack(e1, WHITE) && !is_square_under_attack(f1, WHITE) && !
                    is_square_under_attack(g1, WHITE)) {
                    *list++ = Move(e1, g1, OO);
                }
            }
        }
        if (castling_rights.white_queen_side) {
            // b1,d1,c1 must be empty
            if (mailbox[b1].type == NO_PIECE_TYPE && mailbox[d1].type == NO_PIECE_TYPE && mailbox[c1].type ==
                NO_PIECE_TYPE) {
                // e1,d1,c1 must be safe
                if (!is_square_under_attack(e1, WHITE) && !is_square_under_attack(d1, WHITE) && !
                    is_square_under_attack(c1, WHITE)) {
                    *list++ = Move(e1, c1, OOO);
                }
            }
        }

        // king square may be checked twice but in games the castling_rights are fast all null
        // only check if king square is safe, if some castling right is actually true and squares between are empty
    } else {
        if (castling_rights.black_king_side) {
            if (mailbox[f8].type == NO_PIECE_TYPE && mailbox[g8].type == NO_PIECE_TYPE) {
                if (!is_square_under_attack(e8, BLACK) && !is_square_under_attack(f8, BLACK) && !
                    is_square_under_attack(g8, BLACK)) {
                    *list++ = Move(e8, g8, OO);
                }
            }
        }
        if (castling_rights.black_queen_side) {
            if (mailbox[b8].type == NO_PIECE_TYPE && mailbox[d8].type == NO_PIECE_TYPE && mailbox[c8].type ==
                NO_PIECE_TYPE) {
                if (!is_square_under_attack(e8, BLACK) && !is_square_under_attack(d8, BLACK) && !
                    is_square_under_attack(c8, BLACK)) {
                    *list++ = Move(e8, c8, OOO);
                }
            }
        }
    }

    return list;
}


int Board::get_occupancy_index(Square from_square, const std::array<Bitboard, 64> &masks) {
    // Calculate the occupancy index
    Bitboard mask = masks[from_square];
    Bitboard relevant_blocking_squares = occupancy[BOTH] & mask;
    int num_bits = __builtin_popcountll(mask);
    int occ_idx = 0;

    // Calculate the occupancy index
    Bitboard temp_mask = mask;
    for (int i = 0; i < num_bits; ++i) {
        int lsb = __builtin_ctzll(temp_mask);
        if (relevant_blocking_squares & (1ULL << lsb)) {
            occ_idx |= (1 << i);
        }
        temp_mask &= temp_mask - 1; // Clear LSB
    }

    return occ_idx;
}

bool Board::is_square_under_attack(Square square, Color player_under_attack) {
    Color attacker = player_under_attack == WHITE ? BLACK : WHITE;

    // pawns
    if (PAWN_ATTACKS[player_under_attack][square] & bitboards[attacker][PAWN]) return true;

    // knights
    if (KNIGHT_ATTACKS[square] & bitboards[attacker][KNIGHT]) return true;

    // king
    if (KING_ATTACKS[square] & bitboards[attacker][KING]) return true;

    // sliding pieces
    auto bishop_occ_idx = get_occupancy_index(square, BISHOP_MASKS);
    auto bishop_attacks = BISHOP_ATTACKS[square][bishop_occ_idx] & ~occupancy[player_under_attack];
    // exlude attacks on own pieces

    auto rook_occ_idx = get_occupancy_index(square, ROOK_MASKS);
    auto rook_attacks = ROOK_ATTACKS[square][rook_occ_idx] & ~occupancy[player_under_attack];

    auto queen_attacks = bishop_attacks | rook_attacks;

    // bishops
    if (bishop_attacks & bitboards[attacker][BISHOP]) return true;

    // rooks
    if (rook_attacks & bitboards[attacker][ROOK]) return true;

    // queens
    if (queen_attacks & bitboards[attacker][QUEEN]) return true;


    return false;
}

bool Board::is_in_check(Color player) {
    auto king_bb = bitboards[player][KING];
    auto king_square = BitboardUtil::bitboard_to_square(king_bb);

    return is_square_under_attack(king_square, player);
}

Color Board::get_player_to_move() {
    return player_to_move;
}




// ============= Helper Methods =============

void Board::make_move(Square from, Square to) {
    // a piece may be at target square
    Piece moving_piece = mailbox[from];

    Piece destination_piece = mailbox[to];
    if (destination_piece.type != NO_PIECE_TYPE) {
        remove_piece(to);
    }

    remove_piece(from);
    put_piece(to, moving_piece);
}

void Board::make_quiet_move(Square from, Square to) {
    // no piece at destination square
    Piece moving_piece = mailbox[from];
    remove_piece(from);
    put_piece(to, moving_piece);
}


void Board::put_piece(Square s, Piece p) {
    assert(mailbox[s].type == NO_PIECE_TYPE && "Square not empty");
    Bitboard bb = BitboardUtil::square_to_bitboard(s);
    bitboards[p.color][p.type] |= bb;
    occupancy[p.color] |= bb;
    occupancy[BOTH] |= bb;
    mailbox[s] = p;
}

void Board::remove_piece(Square s) {
    assert(mailbox[s].type != NO_PIECE_TYPE && "No piece to remove");
    Piece p = mailbox[s];
    Bitboard bb = BitboardUtil::square_to_bitboard(s);
    bitboards[p.color][p.type] &= ~bb;
    occupancy[p.color] &= ~bb;
    occupancy[BOTH] &= ~bb;
    mailbox[s].type = NO_PIECE_TYPE;
}


int Board::relative_dir(Direction dir) {
    return player_to_move == WHITE ? dir : -dir;
}

Square Board::pop_lsb(Bitboard &bb) {
    int lsb = __builtin_ctzll(bb);
    Square lsb_square = static_cast<Square>(lsb);
    Bitboard lsb_bb = BitboardUtil::square_to_bitboard(lsb_square);
    bb ^= lsb_bb;
    return lsb_square;
}


// ============= New Validator Methods =============

int Board::get_halfmove_clock() const {
    return halfmove_clock;
}

Move Board::parse_uci_move(const std::string& uci) {
    for (const Move& m : get_legal_moves()) {
        if (m.to_uci() == uci) return m;
    }
    return Move();
}

std::vector<Move> Board::get_legal_moves() {
    Move moves[256];
    Move* end = generate_pseudo_legal_moves(moves);
    std::vector<Move> legal;

    for (Move* m = moves; m < end; ++m) {
        move(*m);
        Color us = (player_to_move == WHITE) ? BLACK : WHITE;
        if (!is_in_check(us)) {
            legal.push_back(*m);
        }
        undo_move(*m);
    }

    return legal;
}

std::vector<Move> Board::get_legal_captures() {
    Move moves[256];
    Move* end = generate_pseudo_legal_moves(moves);
    std::vector<Move> legal;

    for (Move* m = moves; m < end; ++m) {
        if (!m->is_capture()) continue;
        move(*m);
        Color us = (player_to_move == WHITE) ? BLACK : WHITE;
        if (!is_in_check(us)) {
            legal.push_back(*m);
        }
        undo_move(*m);
    }

    return legal;
}

bool Board::is_insufficient_material() {
    int white_count = __builtin_popcountll(occupancy[WHITE]);
    int black_count = __builtin_popcountll(occupancy[BLACK]);

    // K vs K
    if (white_count == 1 && black_count == 1) return true;

    // K+N vs K
    if (white_count == 2 && black_count == 1 && bitboards[WHITE][KNIGHT]) return true;
    if (white_count == 1 && black_count == 2 && bitboards[BLACK][KNIGHT]) return true;

    // K+B vs K
    if (white_count == 2 && black_count == 1 && bitboards[WHITE][BISHOP]) return true;
    if (white_count == 1 && black_count == 2 && bitboards[BLACK][BISHOP]) return true;

    return false;
}

std::string Board::to_fen() {
    std::string fen;

    // 1. Piece placement (rank 8 down to rank 1)
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            Piece p = mailbox[sq];
            if (p.type == NO_PIECE_TYPE) {
                empty++;
            } else {
                if (empty > 0) {
                    fen += static_cast<char>('0' + empty);
                    empty = 0;
                }
                fen += (p.color == WHITE) ? whitePiecesString[p.type] : blackPiecesString[p.type];
            }
        }
        if (empty > 0) fen += static_cast<char>('0' + empty);
        if (rank > 0) fen += '/';
    }

    // 2. Active color
    fen += ' ';
    fen += (player_to_move == WHITE) ? 'w' : 'b';

    // 3. Castling rights
    fen += ' ';
    std::string castling;
    if (castling_rights.white_king_side)  castling += 'K';
    if (castling_rights.white_queen_side) castling += 'Q';
    if (castling_rights.black_king_side)  castling += 'k';
    if (castling_rights.black_queen_side) castling += 'q';
    fen += castling.empty() ? "-" : castling;

    // 4. En passant square
    fen += ' ';
    Square epsq = history[game_ply].epsq;
    if (epsq == NO_SQUARE) {
        fen += '-';
    } else {
        fen += static_cast<char>('a' + (epsq % 8));
        fen += static_cast<char>('1' + (epsq / 8));
    }

    // 5. Halfmove clock
    fen += ' ';
    fen += std::to_string(halfmove_clock);

    // 6. Full-move counter
    fen += ' ';
    fen += std::to_string(full_move_counter);

    return fen;
}

// ============= Display Methods =============

void Board::print() {
    for (int rank = 7; rank >= 0; rank--) {
        std::cout << rank + 1 << " ";
        for (int file = 0; file < 8; file++) {
            Square s = static_cast<Square>(rank * 8 + file);
            PieceType type = get_piece_type_on_square(s);

            if (type == NO_PIECE_TYPE) {
                std::cout << ". ";
            } else {
                Color color = get_piece_color_on_square(s);
                std::cout << (color == WHITE ? whitePiecesString[type] : blackPiecesString[type]) << " ";
            }
        }
        std::cout << '\n';
    }
    std::cout << "  a b c d e f g h\n";
}

// ============= Accessors =============

PieceType Board::get_piece_type_on_square(Square s) {
    return mailbox[s].type;
}

Color Board::get_piece_color_on_square(Square s) {
    return mailbox[s].color;
}