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

/**
 * Stream hint via SSE. Calls onProgress({ depth, best_move, score, nodes })
 * for each intermediate depth, returns the final event (with done: true).
 */
async function streamHint(gameId, onProgress) {
  const headers = {}
  if (_token) headers['Authorization'] = `Bearer ${_token}`

  const res = await fetch(BASE + `/game/${gameId}/hint`, { headers })
  if (!res.ok) {
    const data = await res.json().catch(() => ({}))
    throw new Error(data.error ?? 'Hint request failed')
  }

  const reader = res.body.getReader()
  const decoder = new TextDecoder()
  let buffer = ''
  let finalResult = null

  while (true) {
    const { done, value } = await reader.read()
    if (done) break
    buffer += decoder.decode(value, { stream: true })

    // Process complete SSE events (delimited by double newline)
    let idx
    while ((idx = buffer.indexOf('\n\n')) !== -1) {
      const chunk = buffer.slice(0, idx)
      buffer = buffer.slice(idx + 2)

      for (const line of chunk.split('\n')) {
        if (!line.startsWith('data: ')) continue
        let parsed
        try { parsed = JSON.parse(line.slice(6)) } catch { continue }
        if (parsed.error) throw new Error(parsed.error)
        onProgress(parsed)
        if (parsed.done) finalResult = parsed
      }
    }
  }

  return finalResult
}

export const api = {
  register:      (username, password)             => req('POST', '/register',  { username, password }),
  login:         (username, password)             => req('POST', '/login',     { username, password }),
  searchUsers:   (q)                              => req('GET',  `/users?q=${encodeURIComponent(q)}`),
  createGame:    (white_username, black_username) => req('POST', '/game',      { white_username, black_username }),
  createBotGame: (difficulty)                     => req('POST', '/game/bot',  { difficulty }),
  getGame:       (id)                             => req('GET',  `/game/${id}`),
  myGames:       ()                               => req('GET',  '/games'),
  move:          (game_id, uci_move)              => req('POST', '/move',      { game_id, uci_move }),
  getMoves:      (id)                             => req('GET',  `/game/${id}/moves`),
  getHint:       (id)                             => req('GET',  `/game/${id}/hint`, undefined, { timeout: 120000 }),
  streamHint,
  getBotThinking: (id)                            => req('GET',  `/game/${id}/bot-thinking`),
  // Unauthenticated — each call may hit a different replica via the load balancer.
  stats: () => fetch(BASE + '/stats', { cache: 'no-store' }).then(r => r.ok ? r.json() : null).catch(() => null),
}
