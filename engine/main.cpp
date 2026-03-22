#include <iostream>
#include <cstdlib>
#include <ctime>
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "Validator.h"
#include "Search.h"

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    httplib::Server svr;
    // Default thread pool is max(8, hardware_concurrency-1) which queues under
    // burst load. 32 threads per replica handles concurrent move validation
    // without timeouts at high VU counts.
    svr.new_task_queue = [] { return new httplib::ThreadPool(32); };

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
        int time_ms = body.value("time_ms", 0);

        if (depth < 1 || depth > 64) {
            res.status = 400;
            res.set_content(R"({"error":"depth must be 1-64"})", "application/json");
            return;
        }

        // Opening book: return instantly if we have a book move.
        std::string book_move = book_lookup(fen);
        if (!book_move.empty()) {
            nlohmann::json resp;
            resp["best_move"] = book_move;
            resp["score"] = 0;
            resp["depth"] = 0;
            resp["nodes"] = 0;
            resp["book"] = true;
            res.set_content(resp.dump(), "application/json");
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

        SearchResult result = search(board, depth, noise, time_ms);

        nlohmann::json resp;
        resp["best_move"] = result.best_move.to_uci();
        resp["score"] = result.score;
        resp["depth"] = result.depth_completed;
        resp["nodes"] = result.nodes;
        if (time_ms > 0) resp["time_ms"] = time_ms;
        res.set_content(resp.dump(), "application/json");
    });

    svr.Post("/search-stream", [](const httplib::Request& req, httplib::Response& res) {
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

        // Capture request params before entering the content provider.
        std::string fen = body["fen"].get<std::string>();
        int depth   = body.value("depth", 4);
        int noise   = body.value("noise", 0);
        int time_ms = body.value("time_ms", 0);

        if (depth < 1 || depth > 64) {
            res.status = 400;
            res.set_content(R"({"error":"depth must be 1-64"})", "application/json");
            return;
        }

        // Opening book: send a single SSE event with book flag.
        std::string book_move = book_lookup(fen);
        if (!book_move.empty()) {
            res.set_chunked_content_provider("text/event-stream",
                [book_move](size_t /*offset*/, httplib::DataSink& sink) {
                    nlohmann::json ev;
                    ev["best_move"] = book_move;
                    ev["score"] = 0;
                    ev["depth"] = 0;
                    ev["nodes"] = 0;
                    ev["book"] = true;
                    ev["done"] = true;
                    std::string line = "data: " + ev.dump() + "\n\n";
                    sink.write(line.data(), line.size());
                    sink.done();
                    return false;
                });
            return;
        }

        // Validate FEN before entering the content provider.
        {
            Board test_board;
            try {
                test_board.setup_with_fen(fen);
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"failed to parse FEN"})", "application/json");
                return;
            }
        }

        res.set_chunked_content_provider("text/event-stream",
            [fen, depth, noise, time_ms](size_t /*offset*/, httplib::DataSink& sink) mutable {
                Board board;
                board.setup_with_fen(fen);
                // Abort search early if the client disconnects.
                std::atomic<bool> client_gone{false};

                DepthCallback cb = [&sink, &client_gone](int d, const std::string& best_move, int score, int nodes) -> bool {
                    if (!sink.is_writable()) { client_gone.store(true); return false; }
                    nlohmann::json ev;
                    ev["depth"] = d;
                    ev["best_move"] = best_move;
                    ev["score"] = score;
                    ev["nodes"] = nodes;
                    std::string line = "data: " + ev.dump() + "\n\n";
                    sink.write(line.data(), line.size());
                    return true;
                };

                SearchResult result = search(board, depth, noise, time_ms, cb);

                if (client_gone.load() || !sink.is_writable()) return false;

                // Final event with done flag.
                nlohmann::json ev;
                ev["depth"] = result.depth_completed;
                ev["best_move"] = result.best_move.to_uci();
                ev["score"] = result.score;
                ev["nodes"] = result.nodes;
                ev["done"] = true;
                std::string line = "data: " + ev.dump() + "\n\n";
                sink.write(line.data(), line.size());
                sink.done();
                return false;
            });
    });

    std::cout << "Chess engine listening on 0.0.0.0:8081\n";
    svr.listen("0.0.0.0", 8081);
    return 0;
}
