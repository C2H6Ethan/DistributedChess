#include "Validator.h"
#include "Board.h"
#include "Move.h"


std::string process_move(const std::string& current_fen, const std::string& uci_move) {
    Board board;
    try {
        board.setup_with_fen(current_fen);
    } catch (...) {
        return "SYSTEM_ERROR";
    }

    Move m = board.parse_uci_move(uci_move);

    if (m == Move()) {
        return "{\"status\": \"INVALID\"}";
    }

    board.move(m);

    Move legal_moves[256];
    int legal_count = board.get_legal_moves(legal_moves);
    bool in_check = board.is_in_check(board.get_player_to_move());

    std::string game_state;
    if (legal_count == 0 && in_check) {
        game_state = "CHECKMATE";
    } else if (legal_count == 0 && !in_check) {
        game_state = "STALEMATE";
    } else if (board.get_halfmove_clock() >= 100) {
        game_state = "DRAW_50_MOVE";
    } else if (board.is_insufficient_material()) {
        game_state = "DRAW_INSUFFICIENT";
    } else {
        game_state = "ACTIVE";
    }

    return "{\"status\": \"VALID\", \"game_state\": \"" + game_state + "\", \"new_fen\": \"" + board.to_fen() + "\"}";
}
