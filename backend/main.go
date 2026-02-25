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

	// Public routes
	mux.HandleFunc("POST /register", registerHandler(db))
	mux.HandleFunc("POST /login", loginHandler(db))

	// Protected routes — JWT required
	mux.Handle("POST /game", jwtMiddleware(createGameHandler(db)))
	mux.Handle("POST /move", jwtMiddleware(moveHandler(db)))
	mux.Handle("GET /users", jwtMiddleware(searchUsersHandler(db)))
	mux.Handle("GET /game/{id}", jwtMiddleware(getGameHandler(db)))
	mux.Handle("GET /game/{id}/moves", jwtMiddleware(getGameMovesHandler(db)))
	mux.Handle("GET /game/{id}/hint", jwtMiddleware(hintHandler(db)))
	mux.Handle("GET /games", jwtMiddleware(myGamesHandler(db)))

	addr := ":8080"
	log.Printf("Referee listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, mux))
}
