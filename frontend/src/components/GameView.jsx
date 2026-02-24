import { useState, useEffect, useRef, useMemo } from 'react'
import { Chessboard } from 'react-chessboard'
import { Chess } from 'chess.js'
import { api } from '../api'

// ── Constants ─────────────────────────────────────────────────────────────────
const INITIAL_FEN  = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
const LIGHT_SQUARE = '#c7d2fe'
const DARK_SQUARE  = '#4338ca'
const COL_LAST_MOVE = 'rgba(255, 255, 51, 0.45)'
const COL_SELECTED  = 'rgba(99, 102, 241, 0.55)'
const hintDot  = 'radial-gradient(circle, rgba(0,0,0,0.18) 24%, transparent 24%)'
const hintRing = 'radial-gradient(circle, transparent 58%, rgba(0,0,0,0.22) 58%)'

// ── Status text ───────────────────────────────────────────────────────────────
function deriveStatus({ game, isMyTurn, myColor, opponent }) {
  if (game.status !== 'active') {
    const chess = new Chess(game.current_fen)
    const loserColor = game.current_fen.split(' ')[1]
    if (chess.isCheckmate()) return loserColor !== myColor ? 'Checkmate — You win!' : 'Checkmate — You lose'
    if (chess.isStalemate()) return 'Stalemate — Draw'
    if (chess.isDraw())      return 'Draw'
    return 'Game over'
  }
  return isMyTurn ? 'Your turn' : `Waiting for ${opponent}…`
}

// ── Move history panel ────────────────────────────────────────────────────────
function MoveHistory({ fullMoves, highlightedPly, onNavigate, boardSize }) {
  const activeRef = useRef(null)

  useEffect(() => {
    activeRef.current?.scrollIntoView({ block: 'nearest', behavior: 'smooth' })
  }, [highlightedPly])

  return (
    <div
      style={{ height: boardSize }}
      className="bg-zinc-900 border border-zinc-800 rounded-xl shadow-lg flex flex-col w-40 shrink-0 overflow-hidden"
    >
      {/* Header */}
      <div className="px-3 py-2 border-b border-zinc-800 shrink-0">
        <p className="text-xs font-medium uppercase tracking-widest text-zinc-500">Moves</p>
      </div>

      {/* Scrollable list */}
      <div className="flex-1 overflow-y-auto py-1">
        {fullMoves.length === 0 ? (
          <p className="text-zinc-600 text-xs px-3 py-2">No moves yet</p>
        ) : (
          fullMoves.map(({ num, white, black }) => (
            <div key={num} className="flex items-stretch text-xs">
              {/* Move number */}
              <span className="w-7 shrink-0 flex items-center justify-end pr-1.5 text-zinc-600 select-none">
                {num}.
              </span>

              {/* White's move */}
              <MoveCell
                entry={white}
                highlighted={white.ply === highlightedPly}
                ref={white.ply === highlightedPly ? activeRef : null}
                onClick={() => onNavigate(white.ply)}
              />

              {/* Black's move (may be absent if game ended after white's move) */}
              {black ? (
                <MoveCell
                  entry={black}
                  highlighted={black.ply === highlightedPly}
                  ref={black.ply === highlightedPly ? activeRef : null}
                  onClick={() => onNavigate(black.ply)}
                />
              ) : (
                <span className="flex-1" />
              )}
            </div>
          ))
        )}
      </div>

      {/* Arrow key hint */}
      <div className="px-3 py-2 border-t border-zinc-800 shrink-0">
        <p className="text-zinc-600 text-xs">← → to navigate</p>
      </div>
    </div>
  )
}

// Individual move cell — needs forwardRef for the scroll target
import { forwardRef } from 'react'
const MoveCell = forwardRef(function MoveCell({ entry, highlighted, onClick }, ref) {
  return (
    <button
      ref={ref}
      onClick={onClick}
      className={`flex-1 text-left px-1.5 py-1 rounded transition-colors font-mono ${
        highlighted
          ? 'bg-indigo-600 text-white'
          : 'text-zinc-300 hover:bg-zinc-800'
      }`}
    >
      {entry.san}
    </button>
  )
})

// ── Main component ────────────────────────────────────────────────────────────
export default function GameView({ user, game: initialGame, onGameChange, onLeave }) {
  const [game, setGame]               = useState(initialGame)
  const [moves, setMoves]             = useState([])   // [{ ply, uci, fen_after }]
  const [viewIndex, setViewIndex]     = useState(null) // null = live; N = pinned to position N
  const [optimisticFen, setOptimistic] = useState(null)
  const [moving, setMoving]           = useState(false)
  const [moveFrom, setMoveFrom]       = useState(null)
  const [error, setError]             = useState(null)

  const prevMovesLen = useRef(0)

  const isWhite  = game.white_id === user.id
  const myColor  = isWhite ? 'w' : 'b'
  const opponent = isWhite ? game.black_username : game.white_username

  // displayIdx: how many moves deep we are viewing (0 = before any moves)
  const displayIdx  = viewIndex ?? moves.length
  // In history when pinned to a position that isn't the latest
  const isInHistory = displayIdx < moves.length
  // Interactivity based on the live game state, not the viewed position
  const liveActiveColor = game.current_fen.split(' ')[1]
  const isMyTurn        = liveActiveColor === myColor && game.status === 'active'
  const canMove         = isMyTurn && !moving && !isInHistory
  const isFinished      = game.status !== 'active'

  // ── Fetch moves on mount ──────────────────────────────────────────────────
  useEffect(() => {
    api.getMoves(game.id).then(setMoves).catch(() => {})
  }, [game.id])

  // ── Jump to live when opponent plays while in history ─────────────────────
  useEffect(() => {
    if (moves.length > prevMovesLen.current && viewIndex !== null) {
      setViewIndex(null)
    }
    prevMovesLen.current = moves.length
  }, [moves.length]) // eslint-disable-line

  // ── Smart polling ─────────────────────────────────────────────────────────
  useEffect(() => {
    if (isFinished || isMyTurn) return
    const timer = setInterval(async () => {
      try {
        const [updated, freshMoves] = await Promise.all([
          api.getGame(game.id),
          api.getMoves(game.id),
        ])
        setGame(updated)
        onGameChange(updated)
        setMoves(freshMoves)
      } catch { /* retry next tick */ }
    }, 2000)
    return () => clearInterval(timer)
  }, [game.id, isFinished, isMyTurn]) // eslint-disable-line

  // ── Clear stale selection when the board changes ──────────────────────────
  useEffect(() => { setMoveFrom(null) }, [displayIdx])

  // ── Keyboard navigation ───────────────────────────────────────────────────
  useEffect(() => {
    function onKey(e) {
      if (e.key === 'ArrowLeft') {
        const cur = viewIndex ?? moves.length
        if (cur > 0) setViewIndex(cur - 1)
      } else if (e.key === 'ArrowRight') {
        const cur = viewIndex ?? moves.length
        if (cur < moves.length) {
          const next = cur + 1
          setViewIndex(next >= moves.length ? null : next)
        }
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [viewIndex, moves.length])

  // ── Derived: displayed FEN ────────────────────────────────────────────────
  const displayFen = useMemo(() => {
    if (displayIdx === 0)             return INITIAL_FEN
    if (displayIdx >= moves.length)   return optimisticFen ?? game.current_fen
    return moves[displayIdx - 1].fen_after
  }, [displayIdx, moves, optimisticFen, game.current_fen])

  // ── Derived: last-move highlight (from DB history, works across sessions) ─
  const lastMoveHighlight = useMemo(() => {
    if (displayIdx === 0 || moves.length === 0) return null
    const m = moves[Math.min(displayIdx, moves.length) - 1]
    if (!m) return null
    return { from: m.uci.slice(0, 2), to: m.uci.slice(2, 4) }
  }, [displayIdx, moves])

  // ── Derived: SAN notation (chess.js replay from start) ───────────────────
  const sans = useMemo(() => {
    if (moves.length === 0) return []
    const chess = new Chess()
    return moves.map(m => {
      try {
        const mv = chess.move({
          from: m.uci.slice(0, 2),
          to:   m.uci.slice(2, 4),
          promotion: m.uci[4] || undefined,
        })
        return mv?.san ?? m.uci
      } catch { return m.uci }
    })
  }, [moves])

  // ── Derived: grouped full moves for the history panel ────────────────────
  const fullMoves = useMemo(() => {
    const rows = []
    for (let i = 0; i < sans.length; i += 2) {
      rows.push({
        num:   Math.floor(i / 2) + 1,
        white: { san: sans[i],     ply: i + 1 },
        black: i + 1 < sans.length ? { san: sans[i + 1], ply: i + 2 } : null,
      })
    }
    return rows
  }, [sans])

  // highlightedPly = the ply of the move that produced the currently shown position
  const highlightedPly = displayIdx > 0 ? displayIdx : null

  // ── Square styles (memoised) ──────────────────────────────────────────────
  const squareStyles = useMemo(() => {
    const s = {}
    if (lastMoveHighlight) {
      s[lastMoveHighlight.from] = { backgroundColor: COL_LAST_MOVE }
      s[lastMoveHighlight.to]   = { backgroundColor: COL_LAST_MOVE }
    }
    if (moveFrom && canMove) {
      s[moveFrom] = { backgroundColor: COL_SELECTED }
      const chess = new Chess(displayFen)
      for (const mv of chess.moves({ square: moveFrom, verbose: true })) {
        s[mv.to] = { background: mv.captured ? hintRing : hintDot }
      }
    }
    return s
  }, [lastMoveHighlight, moveFrom, displayFen, canMove])

  // ── Core move executor ────────────────────────────────────────────────────
  function submitMove(from, to) {
    if (!canMove) return false
    const chess = new Chess(displayFen)
    const move  = chess.move({ from, to, promotion: 'q' })
    if (!move) return false

    const uci = `${from}${to}${move.promotion ?? ''}`
    setOptimistic(chess.fen())
    setMoveFrom(null)
    setMoving(true)
    setError(null)

    api.move(game.id, uci)
      .then(result => {
        if (result.status === 'INVALID') { setOptimistic(null); return }
        const updated = {
          ...game,
          current_fen: result.new_fen,
          status: result.game_state !== 'ACTIVE' ? 'finished' : 'active',
        }
        setGame(updated)
        onGameChange(updated)
        // Refresh move history so last-move highlight is driven by DB
        api.getMoves(game.id).then(setMoves).catch(() => {})
        setOptimistic(null)
      })
      .catch(err => { setOptimistic(null); setError(err.message) })
      .finally(() => setMoving(false))

    return true
  }

  function onDrop(from, to) {
    setMoveFrom(null)
    return submitMove(from, to)
  }

  function onSquareClick(square) {
    if (!canMove) return
    const chess = new Chess(displayFen)
    if (moveFrom) {
      if (square === moveFrom) { setMoveFrom(null); return }
      const isLegal = chess.moves({ square: moveFrom, verbose: true }).some(m => m.to === square)
      if (isLegal)  { submitMove(moveFrom, square); return }
      const piece = chess.get(square)
      if (piece && piece.color === myColor) { setMoveFrom(square); return }
      setMoveFrom(null)
      return
    }
    const piece = chess.get(square)
    if (piece && piece.color === myColor) setMoveFrom(square)
  }

  function navigateTo(ply) {
    setViewIndex(ply >= moves.length ? null : ply)
  }

  // Board size expression (reused for both board and panels)
  const boardSize = 'min(580px, calc(100vh - 160px), 80vw)'
  const status    = deriveStatus({ game, isMyTurn, myColor, opponent })

  // ── Render ────────────────────────────────────────────────────────────────
  return (
    <div className="min-h-screen flex flex-col">

      {/* Header */}
      <header className="border-b border-zinc-800 px-6 py-4 flex items-center justify-between shrink-0">
        <button
          onClick={onLeave}
          className="flex items-center gap-1.5 text-zinc-500 hover:text-zinc-300 text-sm transition-colors"
        >
          <svg className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth={2}>
            <path strokeLinecap="round" strokeLinejoin="round" d="M15 19l-7-7 7-7" />
          </svg>
          Dashboard
        </button>
        <span className="text-zinc-500 text-sm">{user.username}</span>
      </header>

      {/* Main */}
      <div className="flex-1 flex items-center justify-center gap-6 p-6 overflow-x-auto">

        {/* ── Move history panel (left) ─────────────────────────────── */}
        <MoveHistory
          fullMoves={fullMoves}
          highlightedPly={highlightedPly}
          onNavigate={navigateTo}
          boardSize={boardSize}
        />

        {/* ── Board ────────────────────────────────────────────────── */}
        <div style={{ width: boardSize }}>
          <Chessboard
            position={displayFen}
            onPieceDrop={onDrop}
            onSquareClick={onSquareClick}
            boardOrientation={isWhite ? 'white' : 'black'}
            customDarkSquareStyle={{ backgroundColor: DARK_SQUARE }}
            customLightSquareStyle={{ backgroundColor: LIGHT_SQUARE }}
            customSquareStyles={squareStyles}
            arePiecesDraggable={canMove}
            animationDuration={120}
          />
        </div>

        {/* ── Sidebar ──────────────────────────────────────────────── */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl shadow-lg p-5 w-52 shrink-0 self-center space-y-5">

          {/* Players */}
          <div className="space-y-2.5">
            {[
              { color: 'b', username: isWhite ? opponent : user.username, dot: 'bg-zinc-700 border border-zinc-500' },
              { color: 'w', username: isWhite ? user.username : opponent, dot: 'bg-zinc-200 border border-zinc-400' },
            ].map(({ color, username, dot }) => {
              const active = liveActiveColor === color && !isFinished
              return (
                <div key={color} className={`flex items-center gap-2.5 transition-colors ${active ? 'text-zinc-100' : 'text-zinc-500'}`}>
                  <span className={`w-3 h-3 rounded-full shrink-0 ${dot}`} />
                  <span className="text-sm truncate">{username}</span>
                  {active && <span className="ml-auto w-1.5 h-1.5 rounded-full bg-indigo-400 animate-pulse shrink-0" />}
                </div>
              )
            })}
          </div>

          <div className="border-t border-zinc-800" />

          {/* Status */}
          <div>
            <p className="text-zinc-500 text-xs uppercase tracking-widest mb-1.5">Status</p>
            <p className={`text-sm font-medium leading-snug ${isFinished ? 'text-indigo-400' : 'text-zinc-100'}`}>
              {status}
            </p>
          </div>

          {/* History mode indicator */}
          {isInHistory && (
            <div>
              <p className="text-zinc-500 text-xs mb-2">
                Move {displayIdx} of {moves.length}
              </p>
              <button
                onClick={() => setViewIndex(null)}
                className="w-full text-xs bg-zinc-800 hover:bg-zinc-700 text-zinc-300 rounded-lg py-1.5 transition-colors"
              >
                ↩ Back to live
              </button>
            </div>
          )}

          {error && <p className="text-red-400 text-xs">{error}</p>}

          {isFinished && (
            <button
              onClick={onLeave}
              className="w-full bg-indigo-600 hover:bg-indigo-500 active:bg-indigo-700 text-white rounded-lg py-2 text-sm font-medium transition-colors"
            >
              Back to Dashboard
            </button>
          )}

          {!isMyTurn && !isFinished && !isInHistory && (
            <div className="flex items-center gap-2 text-zinc-600 text-xs">
              <span className="w-1.5 h-1.5 rounded-full bg-zinc-600 animate-pulse" />
              Polling…
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
