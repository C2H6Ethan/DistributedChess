#pragma once
#include <cstdint>
#include <iostream>

#include "Board.h"

namespace BitboardUtil {
    // Convert square to bitboard (compile-time)
    constexpr Bitboard square_to_bitboard(Square s) noexcept {
        return 1ULL << s;
    }

    // Convert bitboard to square (compile-time)
    constexpr Square bitboard_to_square(Bitboard b) noexcept {
        return static_cast<Square>(__builtin_ctzll(b));
    }

    // Print bitboard visualization
    inline void print_bitboard(Bitboard bitboard) noexcept {
        for (int rank = 7; rank >= 0; rank--) {
            std::cout << rank + 1 << " ";
            for (int file = 0; file < 8; file++) {
                int square = rank * 8 + file;
                std::cout << ((bitboard >> square) & 1 ? '1' : '.') << " ";
            }
            std::cout << '\n';
        }
        std::cout << "  a b c d e f g h\n";
    }
}
