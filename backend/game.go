package main

import (
	"bufio"
	"bytes"
	"database/sql"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

// BotProgress tracks the current search progress for a bot move in flight.
type BotProgress struct {
	Depth    int    `json:"depth"`
	BestMove string `json:"best_move"`
	Score    int    `json:"score"`
	Nodes    int    `json:"nodes"`
	mu       sync.Mutex
}

// botThinking stores in-flight bot search progress keyed by game ID.
var botThinking sync.Map // map[int]*BotProgress

// BotConfig holds the search parameters for a given difficulty level.
type BotConfig struct {
	Depth  int
	Noise  int // centipawn evaluation noise; 0 = deterministic
	TimeMs int // time limit in milliseconds; 0 = depth-only
}

// botConfigs maps the 1-3 star difficulty selector to C++ search parameters.
var botConfigs = map[int]BotConfig{
	1: {Depth: 64, Noise: 400, TimeMs: 1000}, // Easy         — 1s search, misjudges by up to 4 pawns
	2: {Depth: 64, Noise: 150, TimeMs: 3000}, // Intermediate — 3s search, makes positional blunders
	3: {Depth: 64, Noise: 0,   TimeMs: 5000}, // Master       — 5s search, full strength
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
		globalMetrics.recordDB(true)

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
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.Difficulty < 1 || body.Difficulty > 3 {
			jsonError(w, "difficulty must be 1-3", http.StatusBadRequest)
			return
		}

		cfg, ok := botConfigs[body.Difficulty]
		if !ok {
			jsonError(w, "invalid difficulty", http.StatusBadRequest)
			return
		}

		var gameID int
		err := db.QueryRow(
			`INSERT INTO games (white_id, black_id, bot_depth, bot_noise, bot_time_ms) VALUES ($1, 0, $2, $3, $4) RETURNING id`,
			claims.UserID, cfg.Depth, cfg.Noise, cfg.TimeMs,
		).Scan(&gameID)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		globalMetrics.recordDB(true)

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
		var whiteID, blackID, botDepth, botNoise, botTimeMs int
		var currentFEN, status string
		err := db.QueryRow(
			`SELECT white_id, black_id, current_fen, status, bot_depth, bot_noise, bot_time_ms FROM games WHERE id = $1`,
			body.GameID,
		).Scan(&whiteID, &blackID, &currentFEN, &status, &botDepth, &botNoise, &botTimeMs)
		globalMetrics.recordDB(false)
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
		engineStart := time.Now()
		resp, err := engineClient.Post(engineURL()+"/move", "application/json", bytes.NewReader(payload))
		globalMetrics.recordEngine(time.Since(engineStart).Milliseconds(), err != nil)
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
		globalMetrics.recordDB(true) // UPDATE games
		globalMetrics.recordDB(true) // INSERT moves

		// If this is a bot game and the game is still active, fire the engine's reply.
		opponentID := blackID
		if claims.UserID == blackID {
			opponentID = whiteID
		}
		if opponentID == 0 && newStatus == "active" {
			go fireBotMove(db, body.GameID, engineResp.NewFEN, botDepth, botNoise, botTimeMs)
		}

		writeJSON(w, http.StatusOK, map[string]string{
			"status":     engineResp.Status,
			"game_state": engineResp.GameState,
			"new_fen":    engineResp.NewFEN,
		})
	}
}

// fireBotMove calls the C++ engine's streaming endpoint to pick the best move,
// updates botThinking progress as each depth completes, then persists the result.
// Runs in a goroutine so it doesn't block the human player's HTTP response.
func fireBotMove(db *sql.DB, gameID int, fen string, depth int, noise int, timeMs int) {
	progress := &BotProgress{}
	botThinking.Store(gameID, progress)
	defer botThinking.Delete(gameID)

	payload := map[string]any{
		"fen":   fen,
		"depth": depth,
		"noise": noise,
	}
	if timeMs > 0 {
		payload["time_ms"] = timeMs
	}
	searchPayload, _ := json.Marshal(payload)
	searchClient := &http.Client{Timeout: 120 * time.Second}
	resp, err := searchClient.Post(engineURL()+"/search-stream", "application/json", bytes.NewReader(searchPayload))
	if err != nil {
		return
	}
	defer resp.Body.Close()

	// Read SSE events and track the latest best move.
	var bestMove string
	scanner := bufio.NewScanner(resp.Body)
	for scanner.Scan() {
		line := scanner.Text()
		if !strings.HasPrefix(line, "data: ") {
			continue
		}
		jsonStr := strings.TrimPrefix(line, "data: ")
		var ev struct {
			Depth    int    `json:"depth"`
			BestMove string `json:"best_move"`
			Score    int    `json:"score"`
			Nodes    int    `json:"nodes"`
			Done     bool   `json:"done"`
		}
		if err := json.Unmarshal([]byte(jsonStr), &ev); err != nil {
			continue
		}
		progress.mu.Lock()
		progress.Depth = ev.Depth
		progress.BestMove = ev.BestMove
		progress.Score = ev.Score
		progress.Nodes = ev.Nodes
		progress.mu.Unlock()
		if ev.Done {
			bestMove = ev.BestMove
		}
	}

	if bestMove == "" {
		return
	}

	// Validate and apply the bot's chosen move through the engine.
	movePayload, _ := json.Marshal(map[string]string{
		"fen":      fen,
		"uci_move": bestMove,
	})
	botEngineStart := time.Now()
	mresp, err := engineClient.Post(engineURL()+"/move", "application/json", bytes.NewReader(movePayload))
	globalMetrics.recordEngine(time.Since(botEngineStart).Milliseconds(), err != nil)
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
	if _, err = tx.Exec(`INSERT INTO moves (game_id, ply, uci, fen_after) VALUES ($1, $2, $3, $4)`, gameID, ply, bestMove, engineResp.NewFEN); err != nil {
		return
	}
	_ = tx.Commit()
}

// hintStreamHandler streams SSE events from the engine as each search depth
// completes, allowing the frontend to show live depth/score progress.
// GET /game/{id}/hint
func hintStreamHandler(db *sql.DB) http.HandlerFunc {
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

		// Set SSE headers.
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("X-Accel-Buffering", "no")

		flusher, ok := w.(http.Flusher)
		if !ok {
			jsonError(w, "streaming unsupported", http.StatusInternalServerError)
			return
		}

		payload, _ := json.Marshal(map[string]any{
			"fen":     currentFEN,
			"depth":   64,
			"time_ms": 5000,
		})
		searchClient := &http.Client{Timeout: 120 * time.Second}
		resp, err := searchClient.Post(engineURL()+"/search-stream", "application/json", bytes.NewReader(payload))
		if err != nil {
			// Already sent SSE headers, so write error as SSE event.
			fmt.Fprintf(w, "data: {\"error\":\"engine unreachable\"}\n\n")
			flusher.Flush()
			return
		}
		defer resp.Body.Close()

		if resp.StatusCode != http.StatusOK {
			fmt.Fprintf(w, "data: {\"error\":\"engine error\"}\n\n")
			flusher.Flush()
			return
		}

		// Relay SSE events from engine to client.
		scanner := bufio.NewScanner(resp.Body)
		hintsDecremented := false
		for scanner.Scan() {
			line := scanner.Text()
			if !strings.HasPrefix(line, "data: ") {
				continue
			}

			// Check for done event to decrement hints and add hints_left field.
			jsonStr := strings.TrimPrefix(line, "data: ")
			var ev map[string]any
			if err := json.Unmarshal([]byte(jsonStr), &ev); err == nil {
				if done, _ := ev["done"].(bool); done && !hintsDecremented {
					hintsDecremented = true
					col := "black_hints"
					if isCallerWhite {
						col = "white_hints"
					}
					_, _ = db.Exec(
						fmt.Sprintf(`UPDATE games SET %s = %s - 1 WHERE id = $1 AND %s > 0`, col, col, col),
						id,
					)
					ev["hints_left"] = remaining - 1
					modified, _ := json.Marshal(ev)
					fmt.Fprintf(w, "data: %s\n\n", modified)
					flusher.Flush()
					continue
				}
			}

			fmt.Fprintf(w, "%s\n\n", line)
			flusher.Flush()
		}
	}
}

// botThinkingHandler returns the current search progress for a bot move.
// GET /game/{id}/bot-thinking
func botThinkingHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		claims := claimsFromCtx(r)
		id, err := strconv.Atoi(r.PathValue("id"))
		if err != nil {
			jsonError(w, "invalid game id", http.StatusBadRequest)
			return
		}

		// Participant check.
		var whiteID, blackID int
		err = db.QueryRow(`SELECT white_id, black_id FROM games WHERE id = $1`, id).Scan(&whiteID, &blackID)
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

		val, ok := botThinking.Load(id)
		if !ok {
			writeJSON(w, http.StatusOK, map[string]any{"thinking": false})
			return
		}
		p := val.(*BotProgress)
		p.mu.Lock()
		result := map[string]any{
			"thinking":  true,
			"depth":     p.Depth,
			"best_move": p.BestMove,
			"score":     p.Score,
			"nodes":     p.Nodes,
		}
		p.mu.Unlock()
		writeJSON(w, http.StatusOK, result)
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
