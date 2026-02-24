package main

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

// Game is the full game row joined with player usernames.
type Game struct {
	ID            int    `json:"id"`
	WhiteID       int    `json:"white_id"`
	BlackID       int    `json:"black_id"`
	WhiteUsername string `json:"white_username"`
	BlackUsername string `json:"black_username"`
	CurrentFEN    string `json:"current_fen"`
	Status        string `json:"status"`
}

func scanGame(row interface{ Scan(...any) error }) (Game, error) {
	var g Game
	return g, row.Scan(&g.ID, &g.WhiteID, &g.BlackID, &g.CurrentFEN, &g.Status, &g.WhiteUsername, &g.BlackUsername)
}

const gameQuery = `
	SELECT g.id, g.white_id, g.black_id, g.current_fen, g.status,
	       wu.username, bu.username
	FROM games g
	JOIN users wu ON wu.id = g.white_id
	JOIN users bu ON bu.id = g.black_id`

// searchUsersHandler returns users whose username starts with the query.
// GET /users?q=prefix
func searchUsersHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query().Get("q")
		if q == "" {
			writeJSON(w, http.StatusOK, []any{})
			return
		}

		rows, err := db.Query(
			`SELECT id, username FROM users WHERE username ILIKE $1 ORDER BY username LIMIT 10`,
			q+"%",
		)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		defer rows.Close()

		type UserResult struct {
			ID       int    `json:"id"`
			Username string `json:"username"`
		}
		results := []UserResult{}
		for rows.Next() {
			var u UserResult
			if err := rows.Scan(&u.ID, &u.Username); err == nil {
				results = append(results, u)
			}
		}
		writeJSON(w, http.StatusOK, results)
	}
}

// getGameHandler returns the full state of a single game.
// GET /game/{id}
func getGameHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		claims := claimsFromCtx(r)
		id, err := strconv.Atoi(r.PathValue("id"))
		if err != nil {
			jsonError(w, "invalid game id", http.StatusBadRequest)
			return
		}

		g, err := scanGame(db.QueryRow(gameQuery+` WHERE g.id = $1`, id))
		if err == sql.ErrNoRows {
			jsonError(w, "game not found", http.StatusNotFound)
			return
		}
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		if g.WhiteID != claims.UserID && g.BlackID != claims.UserID {
			jsonError(w, "not a participant", http.StatusForbidden)
			return
		}
		writeJSON(w, http.StatusOK, g)
	}
}

// myGamesHandler returns all games the authenticated user is part of.
// GET /games
func myGamesHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		claims := claimsFromCtx(r)
		rows, err := db.Query(
			gameQuery+` WHERE g.white_id = $1 OR g.black_id = $1 ORDER BY g.id DESC`,
			claims.UserID,
		)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		defer rows.Close()

		games := []Game{}
		for rows.Next() {
			if g, err := scanGame(rows); err == nil {
				games = append(games, g)
			}
		}
		writeJSON(w, http.StatusOK, games)
	}
}

// engineClient is a package-level client so the underlying TCP connection pool
// is reused across all requests (keep-alive).
var engineClient = &http.Client{
	Transport: &http.Transport{
		MaxIdleConns:        100,
		MaxIdleConnsPerHost: 100,
		IdleConnTimeout:     90 * time.Second,
		DisableKeepAlives:   false,
	},
	Timeout: 10 * time.Second,
}

func engineURL() string {
	if u := os.Getenv("ENGINE_URL"); u != "" {
		return u
	}
	return "http://localhost:8081"
}

// activeColor returns 'w' or 'b' from the second field of a FEN string.
func activeColor(fen string) (byte, error) {
	parts := strings.Fields(fen)
	if len(parts) < 2 || len(parts[1]) != 1 {
		return 0, fmt.Errorf("malformed FEN")
	}
	c := parts[1][0]
	if c != 'w' && c != 'b' {
		return 0, fmt.Errorf("invalid active color %q in FEN", string(c))
	}
	return c, nil
}

// createGameHandler pairs two existing users into a new game.
// POST /game  {"white_username":"alice","black_username":"bob"}
func createGameHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			WhiteUsername string `json:"white_username"`
			BlackUsername string `json:"black_username"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.WhiteUsername == "" || body.BlackUsername == "" {
			jsonError(w, "invalid request", http.StatusBadRequest)
			return
		}

		var whiteID, blackID int
		err := db.QueryRow(`SELECT id FROM users WHERE username = $1`, body.WhiteUsername).Scan(&whiteID)
		if err == sql.ErrNoRows {
			jsonError(w, "white player not found", http.StatusNotFound)
			return
		}
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		err = db.QueryRow(`SELECT id FROM users WHERE username = $1`, body.BlackUsername).Scan(&blackID)
		if err == sql.ErrNoRows {
			jsonError(w, "black player not found", http.StatusNotFound)
			return
		}
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		var gameID int
		err = db.QueryRow(
			`INSERT INTO games (white_id, black_id) VALUES ($1, $2) RETURNING id`,
			whiteID, blackID,
		).Scan(&gameID)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		writeJSON(w, http.StatusCreated, map[string]int{"game_id": gameID})
	}
}

// moveHandler validates the move, enforces turn order, forwards to the C++
// engine, and persists the new FEN on success.
// POST /move  {"game_id":1,"uci_move":"e2e4"}
func moveHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		claims := claimsFromCtx(r)

		var body struct {
			GameID  int    `json:"game_id"`
			UCIMove string `json:"uci_move"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.UCIMove == "" || body.GameID == 0 {
			jsonError(w, "invalid request", http.StatusBadRequest)
			return
		}

		// Load game state.
		var whiteID, blackID int
		var currentFEN, status string
		err := db.QueryRow(
			`SELECT white_id, black_id, current_fen, status FROM games WHERE id = $1`,
			body.GameID,
		).Scan(&whiteID, &blackID, &currentFEN, &status)
		if err == sql.ErrNoRows {
			jsonError(w, "game not found", http.StatusNotFound)
			return
		}
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		if status != "active" {
			jsonError(w, "game is over", http.StatusConflict)
			return
		}

		// Parse whose turn it is from FEN and verify the caller is that player.
		color, err := activeColor(currentFEN)
		if err != nil {
			jsonError(w, "corrupt game state", http.StatusInternalServerError)
			return
		}
		if color == 'w' && claims.UserID != whiteID {
			jsonError(w, "not your turn", http.StatusForbidden)
			return
		}
		if color == 'b' && claims.UserID != blackID {
			jsonError(w, "not your turn", http.StatusForbidden)
			return
		}

		// Forward to C++ engine over the keep-alive pool.
		payload, _ := json.Marshal(map[string]string{
			"fen":      currentFEN,
			"uci_move": body.UCIMove,
		})
		resp, err := engineClient.Post(engineURL()+"/move", "application/json", bytes.NewReader(payload))
		if err != nil {
			jsonError(w, "engine unreachable", http.StatusBadGateway)
			return
		}
		defer resp.Body.Close()

		var engineResp struct {
			Status    string `json:"status"`
			GameState string `json:"game_state"`
			NewFEN    string `json:"new_fen"`
			Error     string `json:"error"`
		}
		if err := json.NewDecoder(resp.Body).Decode(&engineResp); err != nil {
			jsonError(w, "invalid engine response", http.StatusBadGateway)
			return
		}
		if resp.StatusCode != http.StatusOK {
			jsonError(w, "engine error: "+engineResp.Error, http.StatusBadGateway)
			return
		}

		if engineResp.Status == "INVALID" {
			writeJSON(w, http.StatusUnprocessableEntity, map[string]string{"status": "INVALID"})
			return
		}

		// Persist new FEN and update status if the game ended.
		newStatus := "active"
		switch engineResp.GameState {
		case "CHECKMATE", "STALEMATE", "DRAW_50_MOVE", "DRAW_INSUFFICIENT":
			newStatus = "finished"
		}

		_, err = db.Exec(
			`UPDATE games SET current_fen = $1, status = $2 WHERE id = $3`,
			engineResp.NewFEN, newStatus, body.GameID,
		)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		writeJSON(w, http.StatusOK, map[string]string{
			"status":     engineResp.Status,
			"game_state": engineResp.GameState,
			"new_fen":    engineResp.NewFEN,
		})
	}
}
