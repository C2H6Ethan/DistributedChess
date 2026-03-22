/**
 * DistributedChess k6 Load Test
 *
 * Run:
 *   k6 run loadtest/chess.js
 *   k6 run --vus 50 --duration 60s loadtest/chess.js          (quick blast)
 *   k6 run -e BASE_URL=http://localhost loadtest/chess.js     (custom host)
 *
 * Install k6:
 *   brew install k6   (macOS)
 *   https://k6.io/docs/get-started/installation/
 */

import http  from 'k6/http'
import { check, sleep } from 'k6'
import { Counter, Rate, Trend } from 'k6/metrics'

// ─── Custom metrics (visible in summary + infra dashboard) ────────────────────
const moveLatency   = new Trend('chess_move_ms',        true)
const engineErrors  = new Counter('chess_engine_errors')
const moveOkRate    = new Rate('chess_move_ok_rate')
const gamesCreated  = new Counter('chess_games_created')

// ─── Test shape ───────────────────────────────────────────────────────────────
export const options = {
  scenarios: {
    // Main scenario: full game flows — register → create game → play moves
    game_flow: {
      executor:  'ramping-vus',
      startVUs:  0,
      stages: [
        { duration: '15s', target: 5  },  // warm-up
        { duration: '30s', target: 20 },  // ramp to load
        { duration: '60s', target: 20 },  // sustain — watch replicas in dashboard
        { duration: '15s', target: 0  },  // cool-down
      ],
    },

    // Background scenario: simulate users polling their game list (dashboard 3s poll)
    dashboard_poll: {
      executor:        'constant-arrival-rate',
      rate:            10,          // 10 req/s
      timeUnit:        '1s',
      duration:        '120s',
      preAllocatedVUs: 5,
      maxVUs:          20,
      exec:            'pollDashboard',
    },

    // Background scenario: infra dashboard polling /stats
    stats_poll: {
      executor:        'constant-arrival-rate',
      rate:            4,           // 4 probes/s (matches InfraDashboard PROBE_COUNT)
      timeUnit:        '1s',
      duration:        '120s',
      preAllocatedVUs: 2,
      maxVUs:          8,
      exec:            'pollStats',
    },
  },

  thresholds: {
    http_req_duration:    ['p(95)<2000', 'p(99)<5000'],
    http_req_failed:      ['rate<0.05'],
    chess_move_ok_rate:   ['rate>0.90'],
    chess_move_ms:        ['p(95)<1500'],
  },
}

// ─── Opening sequences (UCI notation) ────────────────────────────────────────
// Each row alternates white/black moves. Chosen to be legal from start position.
const OPENINGS = [
  // Ruy Lopez
  ['e2e4', 'e7e5', 'g1f3', 'b8c6', 'f1b5', 'a7a6'],
  // Queen's Gambit
  ['d2d4', 'd7d5', 'c2c4', 'e7e6', 'b1c3', 'g8f6'],
  // Sicilian Defence
  ['e2e4', 'c7c5', 'g1f3', 'd7d6', 'd2d4', 'c5d4'],
  // Italian Game
  ['e2e4', 'e7e5', 'g1f3', 'b8c6', 'f1c4', 'f8c5'],
  // London System
  ['d2d4', 'd7d5', 'g1f3', 'g8f6', 'c1f4', 'e7e6'],
  // King's Indian
  ['d2d4', 'g8f6', 'c2c4', 'g7g6', 'b1c3', 'f8g7'],
  // Caro-Kann
  ['e2e4', 'c7c6', 'd2d4', 'd7d5', 'b1c3', 'd5e4'],
  // French Defence
  ['e2e4', 'e7e6', 'd2d4', 'd7d5', 'b1c3', 'g8f6'],
]

// ─── Helpers ──────────────────────────────────────────────────────────────────
const BASE = __ENV.BASE_URL || 'http://localhost'
const API  = `${BASE}/api`

function json(token) {
  const h = { 'Content-Type': 'application/json' }
  if (token) h['Authorization'] = `Bearer ${token}`
  return { headers: h }
}

function post(path, body, token) {
  return http.post(`${API}${path}`, JSON.stringify(body), json(token))
}

function get(path, token) {
  return http.get(`${API}${path}`, json(token))
}

// ─── Main scenario: full game flow ────────────────────────────────────────────
export default function () {
  // Unique usernames per VU × iteration. Adding Math.random() tail avoids the
  // rare collision when multiple VUs share the same __VU+__ITER combination
  // at burst start (100 VUs all on iteration 0 simultaneously).
  const uid = `${__VU}_${__ITER}_${Math.floor(Math.random() * 1e9)}`
  const wu  = `w${uid}`
  const bu  = `b${uid}`
  const pw  = 'k6test'

  // 1. Register both players
  const regW = post('/register', { username: wu, password: pw })
  const regB = post('/register', { username: bu, password: pw })

  if (!check(regW, { 'white registered': r => r.status === 201 })) return
  if (!check(regB, { 'black registered': r => r.status === 201 })) return

  const tokW = regW.json('token')
  const tokB = regB.json('token')
  if (!tokW || !tokB) return

  // 2. Create a game
  const gameRes = post('/game', { white_username: wu, black_username: bu }, tokW)
  if (!check(gameRes, { 'game created': r => r.status === 201 })) return
  gamesCreated.add(1)

  const gameId  = gameRes.json('game_id')
  const opening = OPENINGS[Math.floor(Math.random() * OPENINGS.length)]
  // Tokens alternate white/black each half-move
  const toks    = [tokW, tokB, tokW, tokB, tokW, tokB, tokW, tokB]

  // 3. Play through the opening — this is what hammers the C++ engine
  for (let i = 0; i < opening.length; i++) {
    const start   = Date.now()
    const moveRes = post('/move', { game_id: gameId, uci_move: opening[i] }, toks[i])
    const ms      = Date.now() - start

    moveLatency.add(ms)

    const ok = check(moveRes, {
      'move 200':   r => r.status === 200,
      'move VALID': r => r.json('status') === 'VALID',
    })
    moveOkRate.add(ok)

    if (!ok) {
      if (moveRes.status === 502) engineErrors.add(1)
      break
    }

    sleep(0.05) // ~50 ms between moves — realistic enough, not too fast
  }

  // 4. Read back game state + move history (simulates opponent polling)
  const gs = get(`/game/${gameId}`,       tokW)
  const mv = get(`/game/${gameId}/moves`, tokB)
  check(gs, { 'game readable':  r => r.status === 200 })
  check(mv, { 'moves readable': r => r.status === 200 })

  sleep(0.5)
}

// ─── Background: game list poll (like Dashboard's 3s interval) ────────────────
export function pollDashboard() {
  // Re-use one of the game-flow tokens if available; otherwise just hit login
  // to generate some auth traffic. We create a fresh pair per exec call.
  const uid = `poll_${__VU}_${__ITER}`
  const reg = post('/register', { username: uid, password: 'k6poll' })
  if (reg.status !== 201) { sleep(1); return }

  const tok = reg.json('token')
  const res = get('/games', tok)
  check(res, { 'games list 200': r => r.status === 200 })

  sleep(1)
}

// ─── Background: infra stats poll (like InfraDashboard's PROBE_COUNT probes) ──
export function pollStats() {
  const res = http.get(`${API}/stats`)
  check(res, {
    'stats 200':     r => r.status === 200,
    'stats hostname': r => r.json('hostname') !== '',
  })
  sleep(0.25)
}
