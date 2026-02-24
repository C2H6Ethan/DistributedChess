#include <iostream>
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "Validator.h"

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

    std::cout << "Chess engine listening on 0.0.0.0:8081\n";
    svr.listen("0.0.0.0", 8081);
    return 0;
}
