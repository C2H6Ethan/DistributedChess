#pragma once

#include "Board.h"
#include <string>

//The type of the move
enum MoveFlags : int {
    QUIET = 0b0000, DOUBLE_PUSH = 0b0001,
        OO = 0b0010, OOO = 0b0011,
    CAPTURE = 0b1000,
    CAPTURES = 0b1111,
    EN_PASSANT = 0b1010,
    PROMOTIONS = 0b0111,
    PROMOTION_CAPTURES = 0b1100,
    PR_KNIGHT = 0b0100, PR_BISHOP = 0b0101, PR_ROOK = 0b0110, PR_QUEEN = 0b0111,
    PC_KNIGHT = 0b1100, PC_BISHOP = 0b1101, PC_ROOK = 0b1110, PC_QUEEN = 0b1111,
};


class Move {
private:
    //The internal representation of the move
    uint16_t move;
public:
    //Defaults to a null move (a1a1)
    inline Move() : move(0) {}

    inline Move(uint16_t m) { move = m; }

    inline Move(Square from, Square to) : move(0) {
        move = (from << 6) | to;
    }

    inline Move(Square from, Square to, MoveFlags flags) : move(0) {
        move = (flags << 12) | (from << 6) | to;
    }

    // Move(const std::string& move) {
    //     this->move = (create_square(File(move[0] - 'a'), Rank(move[1] - '1')) << 6) |
    //         create_square(File(move[2] - 'a'), Rank(move[3] - '1'));
    // }

    inline Square to() const { return Square(move & 0x3f); }
    inline Square from() const { return Square((move >> 6) & 0x3f); }
    inline int to_from() const { return move & 0xffff; }
    inline MoveFlags flags() const { return MoveFlags((move >> 12) & 0xf); }

    inline bool is_capture() const {
        return (move >> 12) & CAPTURE;
    }

    void operator=(Move m) { move = m.move; }
    bool operator==(Move a) const { return to_from() == a.to_from(); }
    bool operator!=(Move a) const { return to_from() != a.to_from(); }

    inline std::string to_uci() const {
        auto square_to_str = [](Square sq) -> std::string {
            char file = 'a' + (sq % 8);
            char rank = '1' + (sq / 8);
            return std::string{ file, rank };
        };

        std::string uci = square_to_str(from()) + square_to_str(to());

        // Handle promotion moves
        switch (flags()) {
            case PR_QUEEN:
            case PC_QUEEN:
                uci += 'q';
            break;
            case PR_ROOK:
            case PC_ROOK:
                uci += 'r';
            break;
            case PR_BISHOP:
            case PC_BISHOP:
                uci += 'b';
            break;
            case PR_KNIGHT:
            case PC_KNIGHT:
                uci += 'n';
            break;
            default:
                break; // normal move, no promotion suffix
        }

        return uci;
    }
};

