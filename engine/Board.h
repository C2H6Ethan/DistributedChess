#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Move;
// ============= Basic Types =============
using Bitboard = uint64_t;

enum Square : uint8_t {
    a1, b1, c1, d1, e1, f1, g1, h1,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a8, b8, c8, d8, e8, f8, g8, h8,
    NO_SQUARE
};

enum PieceType : uint8_t {
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    PIECE_TYPE_COUNT, NO_PIECE_TYPE
};

enum Color : uint8_t {
    WHITE, BLACK
};

enum Direction {
    NORTH = 8,
    SOUTH = -8,
    EAST = 1,
    WEST = -1,
    NORTH_EAST = NORTH + EAST,
    NORTH_WEST = NORTH + WEST,
    SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST
};

// Separate constant for occupancy tracking
constexpr uint8_t BOTH = 2;

// ============= Utility Structures =============
struct Piece {
    PieceType type;
    Color color;
};


struct CastlingRights {
    bool white_king_side : 1;
    bool white_queen_side : 1;
    bool black_king_side : 1;
    bool black_queen_side : 1;
};

//Stores position information which cannot be recovered on undo-ing a move
struct UndoInfo {
    Bitboard entry = 0;
    Piece captured = {NO_PIECE_TYPE, WHITE};
    Square epsq = NO_SQUARE;
    CastlingRights castling_rights = {true, true, true, true};
    int halfmove_clock = 0;
    uint16_t full_move_counter = 1;
    uint64_t zobrist_key = 0;

    UndoInfo() = default;

    UndoInfo(const UndoInfo& prev)
        : entry(prev.entry) {}
};


// ============= Board Class =============
class Board {
private:
    // Bitboards [color][piece_type]
    Bitboard bitboards[BOTH][PIECE_TYPE_COUNT];

    // Occupancy [WHITE, BLACK, BOTH]
    Bitboard occupancy[3];

    // Game state
    Piece mailbox[64];
    Color player_to_move;
    CastlingRights castling_rights;
    int game_ply;
    uint16_t full_move_counter;
    int halfmove_clock;
    uint64_t zobrist_key;

    // Move Generation
    Move* generate_pawn_moves(Move *list, Square from_square);
    Move* generate_knight_moves(Move *list, Square from_square);
    Move* generate_bishop_moves(Move *list, Square from_square);
    Move* generate_rook_moves(Move *list, Square from_square);
    Move* generate_queen_moves(Move *list, Square from_square);
    Move* generate_king_moves(Move *list, Square from_square);

    int get_occupancy_index(Square from_square, const std::array<Bitboard, 64>& masks);
    bool is_square_under_attack(Square square, Color player_under_attack);


    // Move helpers
    void remove_piece(Square s);
    void put_piece(Square s, Piece p);
    void make_move(Square from, Square to);
    void make_quiet_move(Square from, Square to);
    int relative_dir(Direction dir);
    Square pop_lsb(Bitboard &bb);
public:
    // Construction
    Board();

    //The history of non-recoverable information
    UndoInfo history[256];

    // Setup
    void setup();
    void setup_with_fen(std::string fen);

    // Game operations
    void move(Move m);
    void undo_move(Move m);

    // Move generation
    Move* generate_pseudo_legal_moves(Move *list);

    // Accessors
    PieceType get_piece_type_on_square(Square s);
    Color get_piece_color_on_square(Square s);
    Color get_player_to_move();
    bool is_in_check(Color player);
    int get_halfmove_clock() const;
    uint64_t get_hash() const;
    bool has_non_pawn_material(Color c) const;

    // Null move
    void make_null_move();
    void undo_null_move();

    // Move validation
    Move parse_uci_move(const std::string& uci);
    int get_legal_moves(Move* list);
    int get_legal_captures(Move* list);
    bool is_insufficient_material();

    // FEN output
    std::string to_fen();

    // Display
    void print();
};

// ============= Utility Functions =============
inline std::unordered_map<std::string, Square> SquareMap = {
    {"a1", a1}, {"b1", b1}, {"c1", c1}, {"d1", d1}, {"e1", e1}, {"f1", f1}, {"g1", g1}, {"h1", h1},
    {"a2", a2}, {"b2", b2}, {"c2", c2}, {"d2", d2}, {"e2", e2}, {"f2", f2}, {"g2", g2}, {"h2", h2},
    {"a3", a3}, {"b3", b3}, {"c3", c3}, {"d3", d3}, {"e3", e3}, {"f3", f3}, {"g3", g3}, {"h3", h3},
    {"a4", a4}, {"b4", b4}, {"c4", c4}, {"d4", d4}, {"e4", e4}, {"f4", f4}, {"g4", g4}, {"h4", h4},
    {"a5", a5}, {"b5", b5}, {"c5", c5}, {"d5", d5}, {"e5", e5}, {"f5", f5}, {"g5", g5}, {"h5", h5},
    {"a6", a6}, {"b6", b6}, {"c6", c6}, {"d6", d6}, {"e6", e6}, {"f6", f6}, {"g6", g6}, {"h6", h6},
    {"a7", a7}, {"b7", b7}, {"c7", c7}, {"d7", d7}, {"e7", e7}, {"f7", f7}, {"g7", g7}, {"h7", h7},
    {"a8", a8}, {"b8", b8}, {"c8", c8}, {"d8", d8}, {"e8", e8}, {"f8", f8}, {"g8", g8}, {"h8", h8}
};


constexpr uint64_t square_diff(Square a, Square b) {
    return (a > b) ? (a - b) : (b - a);
}