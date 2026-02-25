#pragma once
#include <string>

// Takes a FEN and a UCI move, returns the JSON output string
std::string process_move(const std::string& current_fen, const std::string& uci_move);
