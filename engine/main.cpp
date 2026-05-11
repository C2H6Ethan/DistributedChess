#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <thread>
#include <chrono>
#include <unistd.h>
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "Validator.h"
#include "Search.h"

// ── Engine-wide metrics ───────────────────────────────────────────────────────
static std::atomic<int>    g_searches_in_flight{0};
// cpu_percent stored as integer * 10 (e.g. 753 = 75.3%) for atomic portability
static std::atomic<int>    g_cpu_percent_x10{0};

static long long read_cpu_ticks() {
    std::ifstream f("/proc/self/stat");
    if (!f.is_open()) return -1;
    std::string line;
    std::getline(f, line);
    std::istringstream ss(line);
    std::string tok;
    // fields are 1-indexed; utime=14, stime=15
    for (int i = 1; i <= 13; i++) ss >> tok;
    long long utime = 0, stime = 0;
    ss >> utime >> stime;
    return utime + stime;
}

static void track_cpu() {
    long long prev = read_cpu_ticks();
    auto prev_t = std::chrono::steady_clock::now();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        long long cur = read_cpu_ticks();
        auto cur_t    = std::chrono::steady_clock::now();
        if (prev >= 0 && cur >= 0) {
            double elapsed = std::chrono::duration<double>(cur_t - prev_t).count();
            int cores = std::max(1, (int)std::thread::hardware_concurrency());
            double pct = (cur - prev) / (elapsed * 100.0 * cores) * 100.0;
            if (pct > 100.0) pct = 100.0;
            if (pct < 0.0)   pct = 0.0;
            g_cpu_percent_x10.store(static_cast<int>(pct * 10));
        }
        prev = cur;
        prev_t = cur_t;
    }
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    std::thread(track_cpu).detach();

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

    svr.Get("/stats", [](const httplib::Request& /*req*/, httplib::Response& res) {
        char hostname_buf[256] = {};
        gethostname(hostname_buf, sizeof(hostname_buf));
        nlohmann::json snap;
        snap["hostname"]           = std::string(hostname_buf);
        snap["cpu_percent"]        = g_cpu_percent_x10.load() / 10.0;
        snap["searches_in_flight"] = g_searches_in_flight.load();
        res.set_header("Cache-Control", "no-cache");
        res.set_content(snap.dump(), "application/json");
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

        g_searches_in_flight.fetch_add(1);
        SearchResult result = search(board, depth, noise, time_ms);
        g_searches_in_flight.fetch_sub(1);

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

                g_searches_in_flight.fetch_add(1);
                SearchResult result = search(board, depth, noise, time_ms, cb);
                g_searches_in_flight.fetch_sub(1);

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
