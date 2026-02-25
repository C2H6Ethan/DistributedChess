package main

import (
	"database/sql"
	"log"

	_ "github.com/lib/pq"
)

func initDB(dsn string) *sql.DB {
	db, err := sql.Open("postgres", dsn)
	if err != nil {
		log.Fatalf("db open: %v", err)
	}
	if err := db.Ping(); err != nil {
		log.Fatalf("db ping: %v", err)
	}

	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS users (
			id            SERIAL PRIMARY KEY,
			username      TEXT UNIQUE NOT NULL,
			password_hash TEXT NOT NULL
		);

		CREATE TABLE IF NOT EXISTS games (
			id          SERIAL PRIMARY KEY,
			white_id    INTEGER NOT NULL REFERENCES users(id),
			black_id    INTEGER NOT NULL REFERENCES users(id),
			current_fen TEXT NOT NULL DEFAULT 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
			status      TEXT NOT NULL DEFAULT 'active',
			white_hints INTEGER NOT NULL DEFAULT 3,
			black_hints INTEGER NOT NULL DEFAULT 3,
			bot_depth   INTEGER NOT NULL DEFAULT 0
		);

		CREATE TABLE IF NOT EXISTS moves (
			id        SERIAL PRIMARY KEY,
			game_id   INTEGER NOT NULL REFERENCES games(id),
			ply       INTEGER NOT NULL,  -- 1-indexed half-move: 1=white's first, 2=black's first, ...
			uci       TEXT NOT NULL,     -- e.g. "e2e4" or "e7e8q"
			fen_after TEXT NOT NULL,     -- board state after this move
			UNIQUE (game_id, ply)
		);
	`)
	if err != nil {
		log.Fatalf("db migrate: %v", err)
	}

	// Seed the Engine bot user at id=0. Password is intentionally unloginnable.
	_, err = db.Exec(`INSERT INTO users (id, username, password_hash) VALUES (0, 'Engine', 'NO_LOGIN') ON CONFLICT DO NOTHING`)
	if err != nil {
		log.Fatalf("db seed engine user: %v", err)
	}

	// Idempotent column addition for existing deployments.
	_, err = db.Exec(`ALTER TABLE games ADD COLUMN IF NOT EXISTS bot_depth INTEGER NOT NULL DEFAULT 0`)
	if err != nil {
		log.Fatalf("db migrate bot_depth: %v", err)
	}

	return db
}
