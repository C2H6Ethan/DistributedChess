#!/usr/bin/env bash
# Load test for GET /games — a JWT-protected endpoint.
#
# Demonstrates:
#   1. Registration → JWT access token + refresh token issued
#   2. Token refresh via POST /refresh (rotation verified)
#   3. hey hammers GET /games with the access token (JWT-protected)
#
# Prerequisites:
#   brew install hey
#
# Usage:
#   ./loadtest/refresh_test.sh
#   BASE_URL=http://localhost ./loadtest/refresh_test.sh

set -euo pipefail

BASE="${BASE_URL:-http://localhost}/api"

# ── 1. Register (or login if user already exists) ─────────────────────────────
echo "==> Registering test user..."
REG=$(curl -s -X POST "$BASE/register" \
  -H "Content-Type: application/json" \
  -d '{"username":"loadtest_user","password":"testpass123"}')

if echo "$REG" | grep -q '"token"'; then
  echo "    Registered."
else
  echo "    User exists — logging in instead..."
  REG=$(curl -s -X POST "$BASE/login" \
    -H "Content-Type: application/json" \
    -d '{"username":"loadtest_user","password":"testpass123"}')
fi

if ! echo "$REG" | grep -q '"token"'; then
  echo "ERROR: could not register or login. Response: $REG"
  exit 1
fi

ACCESS_TOKEN=$(echo "$REG"  | grep -o '"token":"[^"]*"'         | cut -d'"' -f4)
REFRESH_TOKEN=$(echo "$REG" | grep -o '"refresh_token":"[^"]*"' | cut -d'"' -f4)

echo "    Access token : ${ACCESS_TOKEN:0:30}..."
echo "    Refresh token: ${REFRESH_TOKEN:0:20}..."
echo ""

# ── 2. Verify JWT is accepted on a protected endpoint ─────────────────────────
echo "==> Verifying JWT on GET /games..."
STATUS=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/games" \
  -H "Authorization: Bearer $ACCESS_TOKEN")
echo "    HTTP $STATUS"
[ "$STATUS" = "200" ] || { echo "ERROR: expected 200, got $STATUS"; exit 1; }
echo ""

# ── 3. Demonstrate token refresh (rotation) ───────────────────────────────────
echo "==> Refreshing token (POST /refresh)..."
REFRESH_RES=$(curl -sf -X POST "$BASE/refresh" \
  -H "Content-Type: application/json" \
  -d "{\"refresh_token\":\"$REFRESH_TOKEN\"}")

NEW_ACCESS=$(echo "$REFRESH_RES"  | grep -o '"token":"[^"]*"'         | cut -d'"' -f4)
NEW_REFRESH=$(echo "$REFRESH_RES" | grep -o '"refresh_token":"[^"]*"' | cut -d'"' -f4)

[ -n "$NEW_ACCESS" ]  || { echo "ERROR: no new access token. Response: $REFRESH_RES"; exit 1; }
[ -n "$NEW_REFRESH" ] || { echo "ERROR: no new refresh token. Response: $REFRESH_RES"; exit 1; }

echo "    New access token : ${NEW_ACCESS:0:30}..."
echo "    Tokens rotated   : $([ "$NEW_REFRESH" != "$REFRESH_TOKEN" ] && echo YES || echo NO)"
echo ""

# ── 4. Load test the JWT-protected endpoint with the fresh token ───────────────
echo "==> hey: 500 requests, 50 concurrent → GET /games (JWT-protected)"
echo ""
hey -n 500 -c 50 \
  -H "Authorization: Bearer $NEW_ACCESS" \
  "$BASE/games"
