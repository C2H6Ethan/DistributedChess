const BASE = '/api'

// Token is module-level so it persists for the session without prop-drilling.
let _token = localStorage.getItem('chess_token') ?? null

export function setToken(t) {
  _token = t
  t ? localStorage.setItem('chess_token', t) : localStorage.removeItem('chess_token')
}

async function req(method, path, body, { timeout = 10000 } = {}) {
  const headers = { 'Content-Type': 'application/json' }
  if (_token) headers['Authorization'] = `Bearer ${_token}`

  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeout)

  const res = await fetch(BASE + path, {
    method,
    headers,
    body: body !== undefined ? JSON.stringify(body) : undefined,
    signal: controller.signal,
  })
  clearTimeout(timer)

  const data = await res.json()
  if (!res.ok) throw new Error(data.error ?? 'Request failed')
  return data
}

export const api = {
  register:    (username, password)             => req('POST', '/register', { username, password }),
  login:       (username, password)             => req('POST', '/login',    { username, password }),
  searchUsers: (q)                              => req('GET',  `/users?q=${encodeURIComponent(q)}`),
  createGame:  (white_username, black_username) => req('POST', '/game',     { white_username, black_username }),
  getGame:     (id)                             => req('GET',  `/game/${id}`),
  myGames:     ()                               => req('GET',  '/games'),
  move:        (game_id, uci_move)              => req('POST', '/move',     { game_id, uci_move }),
  getMoves:    (id)                             => req('GET',  `/game/${id}/moves`),
  getHint:     (id)                             => req('GET',  `/game/${id}/hint`, undefined, { timeout: 120000 }),
}
