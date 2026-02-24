package main

import (
	"database/sql"
	"encoding/json"
	"net/http"
	"os"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"golang.org/x/crypto/bcrypt"
)

// Claims is the JWT payload.
type Claims struct {
	UserID   int    `json:"user_id"`
	Username string `json:"username"`
	jwt.RegisteredClaims
}

func jwtSecret() []byte {
	s := os.Getenv("JWT_SECRET")
	if s == "" {
		panic("JWT_SECRET env var is not set")
	}
	return []byte(s)
}

func signToken(userID int, username string) (string, error) {
	claims := Claims{
		UserID:   userID,
		Username: username,
		RegisteredClaims: jwt.RegisteredClaims{
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(24 * time.Hour)),
			IssuedAt:  jwt.NewNumericDate(time.Now()),
		},
	}
	return jwt.NewWithClaims(jwt.SigningMethodHS256, claims).SignedString(jwtSecret())
}

func parseToken(tokenStr string) (*Claims, error) {
	token, err := jwt.ParseWithClaims(tokenStr, &Claims{}, func(t *jwt.Token) (any, error) {
		if _, ok := t.Method.(*jwt.SigningMethodHMAC); !ok {
			return nil, jwt.ErrSignatureInvalid
		}
		return jwtSecret(), nil
	})
	if err != nil {
		return nil, err
	}
	claims, ok := token.Claims.(*Claims)
	if !ok || !token.Valid {
		return nil, jwt.ErrTokenInvalidClaims
	}
	return claims, nil
}

func registerHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Username string `json:"username"`
			Password string `json:"password"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.Username == "" || body.Password == "" {
			jsonError(w, "invalid request", http.StatusBadRequest)
			return
		}

		hash, err := bcrypt.GenerateFromPassword([]byte(body.Password), bcrypt.DefaultCost)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		var id int
		err = db.QueryRow(
			`INSERT INTO users (username, password_hash) VALUES ($1, $2) RETURNING id`,
			body.Username, string(hash),
		).Scan(&id)
		if err != nil {
			jsonError(w, "username taken", http.StatusConflict)
			return
		}

		token, err := signToken(id, body.Username)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		writeJSON(w, http.StatusCreated, map[string]string{"token": token})
	}
}

func loginHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Username string `json:"username"`
			Password string `json:"password"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.Username == "" || body.Password == "" {
			jsonError(w, "invalid request", http.StatusBadRequest)
			return
		}

		var id int
		var hash string
		err := db.QueryRow(
			`SELECT id, password_hash FROM users WHERE username = $1`,
			body.Username,
		).Scan(&id, &hash)
		if err == sql.ErrNoRows || bcrypt.CompareHashAndPassword([]byte(hash), []byte(body.Password)) != nil {
			// Deliberately identical response — prevents username enumeration.
			jsonError(w, "invalid credentials", http.StatusUnauthorized)
			return
		}
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		token, err := signToken(id, body.Username)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		writeJSON(w, http.StatusOK, map[string]string{"token": token})
	}
}
