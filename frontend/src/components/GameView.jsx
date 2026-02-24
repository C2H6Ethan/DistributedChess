import { useState, useEffect, useMemo } from 'react'
import { Chessboard } from 'react-chessboard'
import { Chess } from 'chess.js'
import { api } from '../api'

// ── Board palette ─────────────────────────────────────────────────────────────
const LIGHT_SQUARE = '#c7d2fe' // indigo-200
const DARK_SQUARE  = '#4338ca' // indigo-700

// Square overlay colours
const COL_LAST_MOVE = 'rgba(255, 255, 51, 0.45)' // yellow tint — last move from/to
const COL_SELECTED  = 'rgba(99, 102, 241, 0.55)'  // indigo tint — selected piece

// CSS radial gradients matching Chess.com hint style:
//   empty destination → small filled dot
//   capture destination → hollow ring (the piece "pokes through")
const hintDot  = 'radial-gradient(circle, rgba(0,0,0,0.18) 24%, transparent 24%)'
const hintRing = 'radial-gradient(circle, transparent 58%, rgba(0,0,0,0.22) 58%)'

// ── Status line ───────────────────────────────────────────────────────────────
function deriveStatus({ game, isMyTurn, myColor, opponent }) {
  if (game.status !== 'active') {
    const chess = new Chess(game.current_fen)
    // The side to move after the final move is the one in checkmate (the loser).
    const loserColor = game.current_fen.split(' ')[1]
    if (chess.isCheckmate()) {
      return loserColor !== myColor ? 'Checkmate — You win!' : 'Checkmate — You lose'
    }
    if (chess.isStalemate()) return 'Stalemate — Draw'
    if (chess.isDraw())      return 'Draw'
    return 'Game over'
  }
  return isMyTurn ? 'Your turn' : `Waiting for ${opponent}…`
}

// ── Component ─────────────────────────────────────────────────────────────────
export default function GameView({ user, game: initialGame, onGameChange, onLeave }) {
  const [game, setGame]         = useState(initialGame)
  const [fen, setFen]           = useState(initialGame.current_fen)
  const [moving, setMoving]     = useState(false)
  const [lastMove, setLastMove] = useState(null)  // { from, to }
  const [moveFrom, setMoveFrom] = useState(null)  // selected square, or null
  const [error, setError]       = useState(null)

  const isWhite  = game.white_id === user.id
  const myColor  = isWhite ? 'w' : 'b'
  const opponent = isWhite ? game.black_username : game.white_username

  // Always fresh — derived from the current display FEN, not game.current_fen.
  const activeColor = fen.split(' ')[1]
  const isMyTurn    = activeColor === myColor && game.status === 'active'

  // ── Sync display FEN when an external change arrives ────────────────────────
  // Covers both: authoritative server response after our own move, and opponent
  // moves delivered by polling.  Clear stale selection in both cases.
  useEffect(() => {
    setFen(game.current_fen)
    setMoveFrom(null)
  }, [game.current_fen])

  // ── Smart polling ───────────────────────────────────────────────────────────
  // Active only while waiting for the opponent. Automatically stops when
  // isMyTurn flips to true and restarts when we submit our move.
  useEffect(() => {
    if (game.status !== 'active' || isMyTurn) return

    const timer = setInterval(async () => {
      try {
        const updated = await api.getGame(game.id)
        setGame(updated)
        onGameChange(updated)
      } catch { /* transient network error — retry next tick */ }
    }, 2000)

    return () => clearInterval(timer)
  }, [game.id, game.status, isMyTurn]) // eslint-disable-line

  // ── Core move executor ──────────────────────────────────────────────────────
  // Both drag-and-drop and click-to-move funnel through here.
  // Returns true if the move was accepted locally (optimistic update applied).
  function submitMove(from, to) {
    if (!isMyTurn || moving) return false

    const chess = new Chess(fen)
    const move  = chess.move({ from, to, promotion: 'q' })
    if (!move) return false

    // Build UCI string — append promotion piece if chess.js assigned one.
    const uci = `${from}${to}${move.promotion ?? ''}`

    // Optimistic update: show the result immediately, revert on API failure.
    setFen(chess.fen())
    setLastMove({ from, to })
    setMoveFrom(null)
    setMoving(true)
    setError(null)

    api.move(game.id, uci)
      .then(result => {
        if (result.status === 'INVALID') {
          // Shouldn't reach here (chess.js already validated), but be safe.
          setFen(game.current_fen)
          setLastMove(null)
          return
        }
        const isTerminal = result.game_state !== 'ACTIVE'
        const updated = {
          ...game,
          current_fen: result.new_fen,
          status: isTerminal ? 'finished' : 'active',
        }
        setGame(updated)
        onGameChange(updated)
        setFen(result.new_fen)
      })
      .catch(err => {
        setFen(game.current_fen)
        setLastMove(null)
        setError(err.message)
      })
      .finally(() => setMoving(false))

    return true
  }

  // ── Drag-and-drop handler ───────────────────────────────────────────────────
  function onDrop(from, to) {
    // A drag implicitly cancels any pending click selection.
    setMoveFrom(null)
    return submitMove(from, to)
  }

  // ── Click-to-move handler ───────────────────────────────────────────────────
  // State machine:
  //   idle   → click own piece  → selected
  //   selected → click same sq  → idle (deselect)
  //   selected → click legal sq → execute move → idle
  //   selected → click own piece → re-select that piece
  //   selected → click other sq → idle (deselect)
  function onSquareClick(square) {
    if (!isMyTurn || moving) return

    const chess = new Chess(fen)

    // ── Phase 2: a piece is already selected ──────────────────────────────────
    if (moveFrom) {
      // Same square → deselect
      if (square === moveFrom) {
        setMoveFrom(null)
        return
      }

      // Legal destination → fire the move
      const isLegal = chess
        .moves({ square: moveFrom, verbose: true })
        .some(m => m.to === square)

      if (isLegal) {
        submitMove(moveFrom, square)
        return
      }

      // Another friendly piece → change selection
      const piece = chess.get(square)
      if (piece && piece.color === myColor) {
        setMoveFrom(square)
        return
      }

      // Anything else → deselect
      setMoveFrom(null)
      return
    }

    // ── Phase 1: nothing selected yet ─────────────────────────────────────────
    const piece = chess.get(square)
    if (piece && piece.color === myColor) {
      setMoveFrom(square)
    }
  }

  // ── Square highlight styles (memoised) ─────────────────────────────────────
  // Recomputes only when fen, moveFrom, or lastMove actually changes.
  // Layer order: last-move → selected piece → legal dots/rings.
  // Each layer intentionally overwrites the previous — selection always reads
  // cleanly on top of the yellow last-move tint.
  const squareStyles = useMemo(() => {
    const styles = {}

    // Layer 1 — last-move (yellow tint on both from and to squares)
    if (lastMove) {
      styles[lastMove.from] = { backgroundColor: COL_LAST_MOVE }
      styles[lastMove.to]   = { backgroundColor: COL_LAST_MOVE }
    }

    if (moveFrom) {
      // Layer 2 — selected piece (indigo tint, overwrites last-move if same sq)
      styles[moveFrom] = { backgroundColor: COL_SELECTED }

      // Layer 3 — legal destination hints
      // chess.move.captured handles en-passant correctly (the captured pawn
      // is not on the `to` square, but move.captured is still set).
      const chess = new Chess(fen)
      for (const move of chess.moves({ square: moveFrom, verbose: true })) {
        styles[move.to] = { background: move.captured ? hintRing : hintDot }
      }
    }

    return styles
  }, [fen, moveFrom, lastMove])

  // ── Render ──────────────────────────────────────────────────────────────────
  const status     = deriveStatus({ game, isMyTurn, myColor, opponent })
  const isFinished = game.status !== 'active'

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
      <div className="flex-1 flex items-center justify-center gap-6 p-6">

        {/* Board */}
        <div style={{ width: 'min(580px, calc(100vh - 160px), 80vw)' }}>
          <Chessboard
            position={fen}
            onPieceDrop={onDrop}
            onSquareClick={onSquareClick}
            boardOrientation={isWhite ? 'white' : 'black'}
            customDarkSquareStyle={{ backgroundColor: DARK_SQUARE }}
            customLightSquareStyle={{ backgroundColor: LIGHT_SQUARE }}
            customSquareStyles={squareStyles}
            arePiecesDraggable={isMyTurn && !moving}
            animationDuration={120}
          />
        </div>

        {/* Sidebar */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl shadow-lg p-5 w-52 shrink-0 self-center space-y-5">

          {/* Players */}
          <div className="space-y-2.5">
            {[
              { color: 'b', username: isWhite ? opponent : user.username, dot: 'bg-zinc-700 border border-zinc-500' },
              { color: 'w', username: isWhite ? user.username : opponent, dot: 'bg-zinc-200 border border-zinc-400' },
            ].map(({ color, username, dot }) => {
              const active = activeColor === color && game.status === 'active'
              return (
                <div
                  key={color}
                  className={`flex items-center gap-2.5 transition-colors ${active ? 'text-zinc-100' : 'text-zinc-500'}`}
                >
                  <span className={`w-3 h-3 rounded-full shrink-0 ${dot}`} />
                  <span className="text-sm truncate">{username}</span>
                  {active && (
                    <span className="ml-auto w-1.5 h-1.5 rounded-full bg-indigo-400 animate-pulse shrink-0" />
                  )}
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

          {error && <p className="text-red-400 text-xs">{error}</p>}

          {isFinished && (
            <button
              onClick={onLeave}
              className="w-full bg-indigo-600 hover:bg-indigo-500 active:bg-indigo-700 text-white rounded-lg py-2 text-sm font-medium transition-colors"
            >
              Back to Dashboard
            </button>
          )}

          {!isMyTurn && !isFinished && (
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
