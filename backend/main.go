package main

import (
	"log"
	"net/http"
	"os"
)

func main() {
	db := initDB(os.Getenv("DATABASE_URL"))
	defer db.Close()

	mux := http.NewServeMux()

	// Metrics — unauthenticated, for the infrastructure dashboard
	mux.HandleFunc("GET /stats", statsHandler(globalMetrics))
	mux.Handle("DELETE /admin/reset", jwtMiddleware(resetHandler(db)))

	// Public routes
	mux.Handle("POST /register", tracked("POST /register", registerHandler(db)))
	mux.Handle("POST /login", tracked("POST /login", loginHandler(db)))
	mux.Handle("POST /refresh", tracked("POST /refresh", refreshHandler(db)))

	// Protected routes — JWT required
	mux.Handle("POST /game", tracked("POST /game", jwtMiddleware(createGameHandler(db))))
	mux.Handle("POST /game/bot", tracked("POST /game/bot", jwtMiddleware(createBotGameHandler(db))))
	mux.Handle("POST /move", tracked("POST /move", jwtMiddleware(moveHandler(db))))
	mux.Handle("GET /users", tracked("GET /users", jwtMiddleware(searchUsersHandler(db))))
	mux.Handle("GET /game/{id}", tracked("GET /game/{id}", jwtMiddleware(getGameHandler(db))))
	mux.Handle("GET /game/{id}/moves", tracked("GET /game/{id}/moves", jwtMiddleware(getGameMovesHandler(db))))
	mux.Handle("GET /game/{id}/hint", tracked("GET /game/{id}/hint", jwtMiddleware(hintStreamHandler(db))))
	mux.Handle("GET /game/{id}/bot-thinking", tracked("GET /game/{id}/bot-thinking", jwtMiddleware(botThinkingHandler(db))))
	mux.Handle("GET /games", tracked("GET /games", jwtMiddleware(myGamesHandler(db))))

	addr := ":8080"
	log.Printf("Referee listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, mux))
}
