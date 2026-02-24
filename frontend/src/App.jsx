import { useState } from 'react'
import { setToken } from './api'
import Auth from './components/Auth'
import Dashboard from './components/Dashboard'
import GameView from './components/GameView'

// Rehydrate session from localStorage on first render.
function loadStoredUser() {
  const token = localStorage.getItem('chess_token')
  if (!token) return null
  try {
    const payload = JSON.parse(atob(token.split('.')[1]))
    if (payload.exp * 1000 < Date.now()) {
      localStorage.removeItem('chess_token')
      return null
    }
    setToken(token)
    return { id: payload.user_id, username: payload.username }
  } catch {
    return null
  }
}

export default function App() {
  const [user, setUser] = useState(loadStoredUser)
  const [game, setGame] = useState(null)

  function handleAuth(userData) {
    setUser(userData)
    setGame(null)
  }

  function handleLogout() {
    setToken(null)
    setUser(null)
    setGame(null)
  }

  if (!user) return <Auth onAuth={handleAuth} />

  if (game) {
    return (
      <GameView
        user={user}
        game={game}
        onGameChange={setGame}
        onLeave={() => setGame(null)}
      />
    )
  }

  return (
    <Dashboard
      user={user}
      onStartGame={setGame}
      onLogout={handleLogout}
    />
  )
}
