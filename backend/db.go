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
			status      TEXT NOT NULL DEFAULT 'active'
		);
	`)
	if err != nil {
		log.Fatalf("db migrate: %v", err)
	}

	return db
}
