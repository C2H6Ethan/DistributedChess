import { useState } from 'react'
import { api, setToken } from '../api'

export default function Auth({ onAuth }) {
  const [mode, setMode] = useState('login')
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState(null)
  const [loading, setLoading] = useState(false)

  async function handleSubmit(e) {
    e.preventDefault()
    setError(null)
    setLoading(true)
    try {
      const { token } = await (mode === 'login'
        ? api.login(username, password)
        : api.register(username, password))
      setToken(token)
      const payload = JSON.parse(atob(token.split('.')[1]))
      onAuth({ id: payload.user_id, username: payload.username })
    } catch (err) {
      setError(err.message)
    } finally {
      setLoading(false)
    }
  }

  function toggleMode() {
    setMode(m => (m === 'login' ? 'register' : 'login'))
    setError(null)
  }

  return (
    <div className="min-h-screen flex items-center justify-center px-4">
      <div className="w-full max-w-sm">
        {/* Wordmark */}
        <div className="mb-8 text-center">
          <h1 className="text-2xl font-semibold tracking-tight text-zinc-100">
            DistributedChess
          </h1>
          <p className="mt-1 text-sm text-zinc-500">
            {mode === 'login' ? 'Sign in to continue' : 'Create your account'}
          </p>
        </div>

        {/* Card */}
        <div className="bg-zinc-900 border border-zinc-800 rounded-xl shadow-lg p-6">
          <form onSubmit={handleSubmit} className="space-y-3">
            <input
              className="w-full bg-zinc-800 border border-zinc-700 text-zinc-100 rounded-lg px-4 py-2.5 text-sm placeholder-zinc-500 focus:outline-none focus:border-indigo-500 transition-colors"
              placeholder="Username"
              value={username}
              onChange={e => setUsername(e.target.value)}
              autoFocus
              required
            />
            <input
              type="password"
              className="w-full bg-zinc-800 border border-zinc-700 text-zinc-100 rounded-lg px-4 py-2.5 text-sm placeholder-zinc-500 focus:outline-none focus:border-indigo-500 transition-colors"
              placeholder="Password"
              value={password}
              onChange={e => setPassword(e.target.value)}
              required
            />

            {error && (
              <p className="text-red-400 text-xs pt-1">{error}</p>
            )}

            <button
              type="submit"
              disabled={loading}
              className="w-full bg-indigo-600 hover:bg-indigo-500 active:bg-indigo-700 text-white rounded-lg py-2.5 text-sm font-medium transition-colors disabled:opacity-40 mt-1"
            >
              {loading ? '—' : mode === 'login' ? 'Sign in' : 'Create account'}
            </button>
          </form>
        </div>

        <button
          onClick={toggleMode}
          className="mt-4 w-full text-center text-xs text-zinc-500 hover:text-zinc-300 transition-colors"
        >
          {mode === 'login'
            ? "Don't have an account? Register"
            : 'Already have an account? Sign in'}
        </button>
      </div>
    </div>
  )
}
