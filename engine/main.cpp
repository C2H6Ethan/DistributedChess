#include <iostream>
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "Validator.h"
#include "Search.h"

int main() {
    httplib::Server svr;

    svr.Post("/move", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error&) {
            res.status = 400;
            res.set_content(R"({"error":"invalid JSON"})", "application/json");
            return;
        }

        if (!body.contains("fen") || !body.contains("uci_move")) {
            res.status = 400;
            res.set_content(R"({"error":"missing fen or uci_move"})", "application/json");
            return;
        }

        std::string fen      = body["fen"].get<std::string>();
        std::string uci_move = body["uci_move"].get<std::string>();

        std::string result = process_move(fen, uci_move);

        if (result == "SYSTEM_ERROR") {
            res.status = 400;
            res.set_content(R"({"error":"failed to parse FEN"})", "application/json");
            return;
        }

        res.set_content(result, "application/json");
    });

    svr.Post("/search", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error&) {
            res.status = 400;
            res.set_content(R"({"error":"invalid JSON"})", "application/json");
            return;
        }

        if (!body.contains("fen")) {
            res.status = 400;
            res.set_content(R"({"error":"missing fen"})", "application/json");
            return;
        }

        std::string fen = body["fen"].get<std::string>();
        int depth = body.value("depth", 4);
        int noise = body.value("noise", 0);

        if (depth < 1 || depth > 20) {
            res.status = 400;
            res.set_content(R"({"error":"depth must be 1-20"})", "application/json");
            return;
        }

        Board board;
        try {
            board.setup_with_fen(fen);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"failed to parse FEN"})", "application/json");
            return;
        }

        SearchResult result = search(board, depth, noise);

        nlohmann::json resp;
        resp["best_move"] = result.best_move.to_uci();
        resp["score"] = result.score;
        resp["depth"] = depth;
        resp["nodes"] = result.nodes;
        res.set_content(resp.dump(), "application/json");
    });

    std::cout << "Chess engine listening on 0.0.0.0:8081\n";
    svr.listen("0.0.0.0", 8081);
    return 0;
}
