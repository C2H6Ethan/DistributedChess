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

// botDepthByDifficulty maps the 1-4 star difficulty selector to C++ search depths.
// Easy (1) = very shallow for fast, weak play. Master (4) = full depth, strong play.
var botDepthByDifficulty = map[int]int{
	1: 2, // Easy        — instant, random-ish
	2: 4, // Novice      — plays reasonable moves
	3: 6, // Intermediate — solid tactical play
	4: 8, // Master      — strong, slow
}

// Game is the full game row joined with player usernames.
type Game struct {
	ID            int    `json:"id"`
	WhiteID       int    `json:"white_id"`
	BlackID       int    `json:"black_id"`
	WhiteUsername string `json:"white_username"`
	BlackUsername string `json:"black_username"`
	CurrentFEN    string `json:"current_fen"`
	Status        string `json:"status"`
	WhiteHints    int    `json:"white_hints"`
	BlackHints    int    `json:"black_hints"`
}

func scanGame(row interface{ Scan(...any) error }) (Game, error) {
	var g Game
	return g, row.Scan(&g.ID, &g.WhiteID, &g.BlackID, &g.CurrentFEN, &g.Status, &g.WhiteUsername, &g.BlackUsername, &g.WhiteHints, &g.BlackHints)
}

const gameQuery = `
	SELECT g.id, g.white_id, g.black_id, g.current_fen, g.status,
	       wu.username, bu.username, g.white_hints, g.black_hints
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

// createBotGameHandler starts a new game against the Engine bot.
// POST /game/bot  {"difficulty": 1-4}
func createBotGameHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		claims := claimsFromCtx(r)

		var body struct {
			Difficulty int `json:"difficulty"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.Difficulty < 1 || body.Difficulty > 4 {
			jsonError(w, "difficulty must be 1-4", http.StatusBadRequest)
			return
		}

		depth, ok := botDepthByDifficulty[body.Difficulty]
		if !ok {
			jsonError(w, "invalid difficulty", http.StatusBadRequest)
			return
		}

		var gameID int
		err := db.QueryRow(
			`INSERT INTO games (white_id, black_id, bot_depth) VALUES ($1, 0, $2) RETURNING id`,
			claims.UserID, depth,
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
		var whiteID, blackID, botDepth int
		var currentFEN, status string
		err := db.QueryRow(
			`SELECT white_id, black_id, current_fen, status, bot_depth FROM games WHERE id = $1`,
			body.GameID,
		).Scan(&whiteID, &blackID, &currentFEN, &status, &botDepth)
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

		// Persist new FEN, status, and move record atomically.
		newStatus := "active"
		switch engineResp.GameState {
		case "CHECKMATE", "STALEMATE", "DRAW_50_MOVE", "DRAW_INSUFFICIENT":
			newStatus = "finished"
		}

		tx, err := db.Begin()
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		defer tx.Rollback() //nolint:errcheck

		// ply = number of moves already recorded + 1
		var ply int
		if err = tx.QueryRow(
			`SELECT COUNT(*) + 1 FROM moves WHERE game_id = $1`, body.GameID,
		).Scan(&ply); err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		if _, err = tx.Exec(
			`UPDATE games SET current_fen = $1, status = $2 WHERE id = $3`,
			engineResp.NewFEN, newStatus, body.GameID,
		); err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		if _, err = tx.Exec(
			`INSERT INTO moves (game_id, ply, uci, fen_after) VALUES ($1, $2, $3, $4)`,
			body.GameID, ply, body.UCIMove, engineResp.NewFEN,
		); err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		if err = tx.Commit(); err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		// If this is a bot game and the game is still active, fire the engine's reply.
		opponentID := blackID
		if claims.UserID == blackID {
			opponentID = whiteID
		}
		if opponentID == 0 && newStatus == "active" {
			go fireBotMove(db, body.GameID, engineResp.NewFEN, botDepth)
		}

		writeJSON(w, http.StatusOK, map[string]string{
			"status":     engineResp.Status,
			"game_state": engineResp.GameState,
			"new_fen":    engineResp.NewFEN,
		})
	}
}

// fireBotMove calls the C++ engine to pick the best move, then persists it.
// Runs in a goroutine so it doesn't block the human player's HTTP response.
func fireBotMove(db *sql.DB, gameID int, fen string, depth int) {
	searchPayload, _ := json.Marshal(map[string]any{
		"fen":   fen,
		"depth": depth,
	})
	searchClient := &http.Client{Timeout: 120 * time.Second}
	resp, err := searchClient.Post(engineURL()+"/search", "application/json", bytes.NewReader(searchPayload))
	if err != nil {
		return
	}
	defer resp.Body.Close()

	var searchResp struct {
		BestMove string `json:"best_move"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&searchResp); err != nil || searchResp.BestMove == "" {
		return
	}

	// Validate and apply the bot's chosen move through the engine.
	movePayload, _ := json.Marshal(map[string]string{
		"fen":      fen,
		"uci_move": searchResp.BestMove,
	})
	mresp, err := engineClient.Post(engineURL()+"/move", "application/json", bytes.NewReader(movePayload))
	if err != nil {
		return
	}
	defer mresp.Body.Close()

	var engineResp struct {
		Status    string `json:"status"`
		GameState string `json:"game_state"`
		NewFEN    string `json:"new_fen"`
	}
	if err := json.NewDecoder(mresp.Body).Decode(&engineResp); err != nil || engineResp.Status != "VALID" {
		return
	}

	newStatus := "active"
	switch engineResp.GameState {
	case "CHECKMATE", "STALEMATE", "DRAW_50_MOVE", "DRAW_INSUFFICIENT":
		newStatus = "finished"
	}

	tx, err := db.Begin()
	if err != nil {
		return
	}
	defer tx.Rollback() //nolint:errcheck

	var ply int
	if err = tx.QueryRow(`SELECT COUNT(*) + 1 FROM moves WHERE game_id = $1`, gameID).Scan(&ply); err != nil {
		return
	}
	if _, err = tx.Exec(`UPDATE games SET current_fen = $1, status = $2 WHERE id = $3`, engineResp.NewFEN, newStatus, gameID); err != nil {
		return
	}
	if _, err = tx.Exec(`INSERT INTO moves (game_id, ply, uci, fen_after) VALUES ($1, $2, $3, $4)`, gameID, ply, searchResp.BestMove, engineResp.NewFEN); err != nil {
		return
	}
	_ = tx.Commit()
}

// hintHandler asks the C++ engine for the best move at depth 7.
// GET /game/{id}/hint
func hintHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		claims := claimsFromCtx(r)
		id, err := strconv.Atoi(r.PathValue("id"))
		if err != nil {
			jsonError(w, "invalid game id", http.StatusBadRequest)
			return
		}

		var whiteID, blackID int
		var currentFEN, status string
		var whiteHints, blackHints int
		err = db.QueryRow(
			`SELECT white_id, black_id, current_fen, status, white_hints, black_hints FROM games WHERE id = $1`, id,
		).Scan(&whiteID, &blackID, &currentFEN, &status, &whiteHints, &blackHints)
		if err == sql.ErrNoRows {
			jsonError(w, "game not found", http.StatusNotFound)
			return
		}
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		if whiteID != claims.UserID && blackID != claims.UserID {
			jsonError(w, "not a participant", http.StatusForbidden)
			return
		}
		if status != "active" {
			jsonError(w, "game is over", http.StatusConflict)
			return
		}

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

		// Check remaining hints for the caller.
		isCallerWhite := claims.UserID == whiteID
		remaining := blackHints
		if isCallerWhite {
			remaining = whiteHints
		}
		if remaining <= 0 {
			jsonError(w, "no hints remaining", http.StatusForbidden)
			return
		}

		payload, _ := json.Marshal(map[string]any{
			"fen":   currentFEN,
			"depth": 7,
		})
		// Use a long timeout — search can be slow on unoptimised engines.
		searchClient := &http.Client{Timeout: 120 * time.Second}
		resp, err := searchClient.Post(engineURL()+"/search", "application/json", bytes.NewReader(payload))
		if err != nil {
			jsonError(w, "engine unreachable", http.StatusBadGateway)
			return
		}
		defer resp.Body.Close()

		var engineResp struct {
			BestMove string `json:"best_move"`
			Score    int    `json:"score"`
			Error    string `json:"error"`
		}
		if err := json.NewDecoder(resp.Body).Decode(&engineResp); err != nil {
			jsonError(w, "invalid engine response", http.StatusBadGateway)
			return
		}
		if resp.StatusCode != http.StatusOK {
			jsonError(w, "engine error: "+engineResp.Error, http.StatusBadGateway)
			return
		}

		// Decrement hints only after a successful engine response.
		col := "black_hints"
		if isCallerWhite {
			col = "white_hints"
		}
		_, _ = db.Exec(
			fmt.Sprintf(`UPDATE games SET %s = %s - 1 WHERE id = $1 AND %s > 0`, col, col, col),
			id,
		)

		writeJSON(w, http.StatusOK, map[string]any{
			"best_move":  engineResp.BestMove,
			"score":      engineResp.Score,
			"hints_left": remaining - 1,
		})
	}
}

// MoveRecord is a single half-move in a game's history.
type MoveRecord struct {
	Ply      int    `json:"ply"`
	UCI      string `json:"uci"`
	FENAfter string `json:"fen_after"`
}

// getGameMovesHandler returns the ordered move history for a game.
// The JOIN ensures only participants can read the moves.
// GET /game/{id}/moves
func getGameMovesHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		claims := claimsFromCtx(r)
		id, err := strconv.Atoi(r.PathValue("id"))
		if err != nil {
			jsonError(w, "invalid game id", http.StatusBadRequest)
			return
		}

		rows, err := db.Query(`
			SELECT m.ply, m.uci, m.fen_after
			FROM moves m
			JOIN games g ON g.id = m.game_id
			WHERE m.game_id = $1
			  AND (g.white_id = $2 OR g.black_id = $2)
			ORDER BY m.ply`,
			id, claims.UserID,
		)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		defer rows.Close()

		records := []MoveRecord{}
		for rows.Next() {
			var m MoveRecord
			if err := rows.Scan(&m.Ply, &m.UCI, &m.FENAfter); err == nil {
				records = append(records, m)
			}
		}
		writeJSON(w, http.StatusOK, records)
	}
}
