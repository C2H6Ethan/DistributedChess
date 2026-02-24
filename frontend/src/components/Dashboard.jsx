import { useState, useEffect, useRef } from 'react'
import { api } from '../api'

function GameRow({ game, user, onResume }) {
  const isWhite = game.white_id === user.id
  const opponent = isWhite ? game.black_username : game.white_username
  const color = isWhite ? 'White' : 'Black'
  const myColor = isWhite ? 'w' : 'b'
  const activeColor = game.current_fen.split(' ')[1]
  const isMyTurn = game.status === 'active' && activeColor === myColor

  return (
    <button
      onClick={() => onResume(game)}
      className="w-full flex items-center justify-between bg-zinc-900 border border-zinc-800 hover:border-zinc-700 rounded-xl px-4 py-3 transition-colors group text-left"
    >
      <div>
        <p className="text-zinc-100 text-sm font-medium">vs {opponent}</p>
        <p className="text-zinc-500 text-xs mt-0.5">
          {color} ·{' '}
          {game.status !== 'active'
            ? 'Finished'
            : isMyTurn
            ? 'Your turn'
            : "Opponent's turn"}
        </p>
      </div>
      <div className="flex items-center gap-2">
        {isMyTurn && (
          <span className="w-2 h-2 rounded-full bg-indigo-500 animate-pulse" />
        )}
        <svg
          className="w-4 h-4 text-zinc-600 group-hover:text-zinc-400 transition-colors"
          fill="none"
          viewBox="0 0 24 24"
          stroke="currentColor"
          strokeWidth={2}
        >
          <path strokeLinecap="round" strokeLinejoin="round" d="M9 5l7 7-7 7" />
        </svg>
      </div>
    </button>
  )
}

export default function Dashboard({ user, onStartGame, onLogout }) {
  const [query, setQuery] = useState('')
  const [results, setResults] = useState([])
  const [searching, setSearching] = useState(false)
  const [showDropdown, setShowDropdown] = useState(false)
  const [myGames, setMyGames] = useState([])
  const [challenging, setChallenging] = useState(null)
  const inputRef = useRef(null)
  const dropdownRef = useRef(null)

  // Load games on mount
  useEffect(() => {
    api.myGames().then(setMyGames).catch(() => {})
  }, [])

  // Debounced search — fires 300 ms after the user stops typing
  useEffect(() => {
    const q = query.trim()
    if (!q) {
      setResults([])
      setShowDropdown(false)
      return
    }

    setSearching(true)
    const timer = setTimeout(async () => {
      try {
        const data = await api.searchUsers(q)
        // Exclude yourself from results
        setResults(data.filter(u => u.id !== user.id))
        setShowDropdown(true)
      } catch {
        setResults([])
      } finally {
        setSearching(false)
      }
    }, 300)

    return () => clearTimeout(timer)
  }, [query, user.id])

  // Close dropdown on outside click
  useEffect(() => {
    function handle(e) {
      if (!dropdownRef.current?.contains(e.target) && !inputRef.current?.contains(e.target)) {
        setShowDropdown(false)
      }
    }
    document.addEventListener('mousedown', handle)
    return () => document.removeEventListener('mousedown', handle)
  }, [])

  async function challenge(opponent) {
    setChallenging(opponent.id)
    try {
      const { game_id } = await api.createGame(user.username, opponent.username)
      const game = await api.getGame(game_id)
      onStartGame(game)
    } catch (err) {
      alert(err.message)
    } finally {
      setChallenging(null)
    }
  }

  async function resume(game) {
    try {
      const fresh = await api.getGame(game.id)
      onStartGame(fresh)
    } catch {
      onStartGame(game)
    }
  }

  const activeGames = myGames.filter(g => g.status === 'active')
  const finishedGames = myGames.filter(g => g.status !== 'active')

  return (
    <div className="min-h-screen flex flex-col">
      {/* Header */}
      <header className="border-b border-zinc-800 px-6 py-4 flex items-center justify-between">
        <span className="font-semibold text-zinc-100 tracking-tight">DistributedChess</span>
        <div className="flex items-center gap-4">
          <span className="text-zinc-400 text-sm">{user.username}</span>
          <button
            onClick={onLogout}
            className="text-zinc-500 hover:text-zinc-300 text-sm transition-colors"
          >
            Sign out
          </button>
        </div>
      </header>

      <main className="flex-1 max-w-lg mx-auto w-full px-6 py-10 space-y-10">

        {/* ── New game ───────────────────────────────────────────────── */}
        <section>
          <h2 className="text-xs font-medium uppercase tracking-widest text-zinc-500 mb-3">
            New Game
          </h2>

          <div className="relative">
            <input
              ref={inputRef}
              className="w-full bg-zinc-900 border border-zinc-800 text-zinc-100 rounded-xl px-4 py-3 text-sm placeholder-zinc-500 focus:outline-none focus:border-indigo-500 transition-colors"
              placeholder="Search for a player…"
              value={query}
              onChange={e => setQuery(e.target.value)}
              onFocus={() => results.length > 0 && setShowDropdown(true)}
              autoComplete="off"
            />

            {/* Dropdown */}
            {showDropdown && (
              <div
                ref={dropdownRef}
                className="absolute top-full mt-1.5 w-full bg-zinc-900 border border-zinc-800 rounded-xl shadow-2xl overflow-hidden z-20"
              >
                {searching && (
                  <div className="px-4 py-3 text-zinc-500 text-sm">Searching…</div>
                )}
                {!searching && results.length === 0 && (
                  <div className="px-4 py-3 text-zinc-500 text-sm">No players found</div>
                )}
                {results.map(u => (
                  <div
                    key={u.id}
                    className="flex items-center justify-between px-4 py-3 hover:bg-zinc-800 transition-colors group"
                  >
                    <span className="text-zinc-100 text-sm">{u.username}</span>
                    <button
                      onClick={() => challenge(u)}
                      disabled={challenging === u.id}
                      className="text-xs bg-indigo-600 hover:bg-indigo-500 active:bg-indigo-700 text-white px-3 py-1.5 rounded-lg transition-colors disabled:opacity-40 opacity-0 group-hover:opacity-100"
                    >
                      {challenging === u.id ? '…' : 'Challenge'}
                    </button>
                  </div>
                ))}
              </div>
            )}
          </div>
        </section>

        {/* ── Active games ──────────────────────────────────────────── */}
        {activeGames.length > 0 && (
          <section>
            <h2 className="text-xs font-medium uppercase tracking-widest text-zinc-500 mb-3">
              Active Games
            </h2>
            <div className="space-y-2">
              {activeGames.map(g => (
                <GameRow key={g.id} game={g} user={user} onResume={resume} />
              ))}
            </div>
          </section>
        )}

        {/* ── Finished games ────────────────────────────────────────── */}
        {finishedGames.length > 0 && (
          <section>
            <h2 className="text-xs font-medium uppercase tracking-widest text-zinc-500 mb-3">
              Finished
            </h2>
            <div className="space-y-2">
              {finishedGames.map(g => (
                <GameRow key={g.id} game={g} user={user} onResume={resume} />
              ))}
            </div>
          </section>
        )}

        {myGames.length === 0 && (
          <p className="text-zinc-600 text-sm text-center pt-4">
            Search for a player above to start your first game.
          </p>
        )}
      </main>
    </div>
  )
}
