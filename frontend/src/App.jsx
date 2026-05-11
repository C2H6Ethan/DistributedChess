import { useState } from 'react'
import { setToken, setRefreshToken } from './api'
import Auth from './components/Auth'
import Dashboard from './components/Dashboard'
import GameView from './components/GameView'
import InfraDashboard from './components/InfraDashboard'

// Rehydrate session from localStorage on first render.
// Access token may be expired — that's fine, the refresh token will renew it on the next request.
function loadStoredUser() {
  const token = localStorage.getItem('chess_token')
  const refresh = localStorage.getItem('chess_refresh_token')
  if (!token) return null
  try {
    const payload = JSON.parse(atob(token.split('.')[1]))
    // If both tokens are missing/expired, force re-login.
    if (payload.exp * 1000 < Date.now() && !refresh) {
      localStorage.removeItem('chess_token')
      return null
    }
    setToken(token)
    if (refresh) setRefreshToken(refresh)
    return { id: payload.user_id, username: payload.username }
  } catch {
    return null
  }
}

export default function App() {
  const [user,  setUser]  = useState(loadStoredUser)
  const [game,  setGame]  = useState(null)
  const [infra, setInfra] = useState(false)

  function handleAuth(userData) {
    setUser(userData)
    setGame(null)
  }

  function handleLogout() {
    setToken(null)
    setRefreshToken(null)
    setUser(null)
    setGame(null)
    setInfra(false)
  }

  if (!user) return <Auth onAuth={handleAuth} />

  if (infra) return <InfraDashboard onLeave={() => setInfra(false)} />

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
      onInfra={() => setInfra(true)}
    />
  )
}
