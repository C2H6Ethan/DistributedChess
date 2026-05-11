package main

import (
	"crypto/rand"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
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
			ExpiresAt: jwt.NewNumericDate(time.Now().Add(15 * time.Minute)),
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

func issueRefreshToken(db *sql.DB, userID int) (string, error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	token := hex.EncodeToString(raw)
	hash := sha256RefreshToken(token)
	expires := time.Now().Add(30 * 24 * time.Hour)

	_, err := db.Exec(
		`INSERT INTO refresh_tokens (user_id, token_hash, expires_at) VALUES ($1, $2, $3)`,
		userID, hash, expires,
	)
	return token, err
}

func sha256RefreshToken(token string) string {
	h := sha256.Sum256([]byte(token))
	return hex.EncodeToString(h[:])
}

func issueTokenPair(db *sql.DB, userID int, username string) (accessToken, refreshToken string, err error) {
	accessToken, err = signToken(userID, username)
	if err != nil {
		return
	}
	refreshToken, err = issueRefreshToken(db, userID)
	return
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

		access, refresh, err := issueTokenPair(db, id, body.Username)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		writeJSON(w, http.StatusCreated, map[string]string{"token": access, "refresh_token": refresh})
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

		access, refresh, err := issueTokenPair(db, id, body.Username)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		writeJSON(w, http.StatusOK, map[string]string{"token": access, "refresh_token": refresh})
	}
}

func refreshHandler(db *sql.DB) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			RefreshToken string `json:"refresh_token"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil || body.RefreshToken == "" {
			jsonError(w, "invalid request", http.StatusBadRequest)
			return
		}

		hash := sha256RefreshToken(body.RefreshToken)

		var tokenID, userID int
		var username string
		var expires time.Time
		err := db.QueryRow(`
			SELECT rt.id, rt.user_id, u.username, rt.expires_at
			FROM refresh_tokens rt
			JOIN users u ON u.id = rt.user_id
			WHERE rt.token_hash = $1
		`, hash).Scan(&tokenID, &userID, &username, &expires)
		if err == sql.ErrNoRows {
			jsonError(w, "invalid refresh token", http.StatusUnauthorized)
			return
		}
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		if time.Now().After(expires) {
			db.Exec(`DELETE FROM refresh_tokens WHERE id = $1`, tokenID)
			jsonError(w, "refresh token expired", http.StatusUnauthorized)
			return
		}

		// Rotate: delete old token — verify it was still present (reuse detection).
		result, err := db.Exec(`DELETE FROM refresh_tokens WHERE id = $1`, tokenID)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}
		if n, _ := result.RowsAffected(); n == 0 {
			jsonError(w, "refresh token already used", http.StatusUnauthorized)
			return
		}

		access, refresh, err := issueTokenPair(db, userID, username)
		if err != nil {
			jsonError(w, "internal error", http.StatusInternalServerError)
			return
		}

		writeJSON(w, http.StatusOK, map[string]string{"token": access, "refresh_token": refresh})
	}
}
