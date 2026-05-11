import { useState, useEffect, useRef } from 'react'
import { api } from '../api'

const POLL_MS     = 1000
const PROBE_COUNT = 4   // concurrent probes per tick — hits different replicas via round-robin LB
const HISTORY_LEN = 30  // seconds of sparkline history

// ─── Primitives ────────────────────────────────────────────────────────────────

function Sparkline({ data = [], color = '#818cf8', height = 28 }) {
  if (data.length < 2) return <div style={{ height }} className="w-full" />
  const W = 100
  const min = Math.min(...data)
  const max = Math.max(...data)
  const range = max - min || 1
  const pts = data.map((v, i) => [
    (i / (data.length - 1)) * W,
    height - 4 - ((v - min) / range) * (height - 8),
  ])
  const line = pts.map(([x, y]) => `${x},${y}`).join(' ')
  const area = [
    `M ${pts[0][0]},${height}`,
    ...pts.map(([x, y]) => `L ${x},${y}`),
    `L ${pts.at(-1)[0]},${height}`,
    'Z',
  ].join(' ')
  const gid = `sg${color.replace(/[^a-z0-9]/gi, '')}`
  return (
    <svg
      viewBox={`0 0 ${W} ${height}`}
      className="w-full"
      style={{ height }}
      preserveAspectRatio="none"
    >
      <defs>
        <linearGradient id={gid} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor={color} stopOpacity="0.2" />
          <stop offset="100%" stopColor={color} stopOpacity="0" />
        </linearGradient>
      </defs>
      <path d={area} fill={`url(#${gid})`} />
      <polyline
        points={line}
        fill="none"
        stroke={color}
        strokeWidth="1.5"
        strokeLinejoin="round"
        strokeLinecap="round"
      />
      {(() => {
        const [lx, ly] = pts.at(-1)
        return <circle cx={lx} cy={ly} r="2.5" fill={color} />
      })()}
    </svg>
  )
}

function PulseDot({ active = true }) {
  return (
    <span className="relative flex h-2 w-2">
      {active && (
        <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75" />
      )}
      <span
        className={`relative inline-flex rounded-full h-2 w-2 ${
          active ? 'bg-emerald-500' : 'bg-slate-600'
        }`}
      />
    </span>
  )
}

function Bar({ pct, colorClass = 'bg-indigo-500' }) {
  const clamp = Math.min(100, Math.max(0, pct || 0))
  return (
    <div className="h-1 w-full bg-slate-800 rounded-full overflow-hidden">
      <div
        className={`h-full rounded-full transition-all duration-700 ${colorClass}`}
        style={{ width: `${clamp}%` }}
      />
    </div>
  )
}

function fmt(n, decimals = 0) {
  if (n == null || isNaN(n)) return '—'
  if (n >= 1_000_000) return (n / 1_000_000).toFixed(1) + 'M'
  if (n >= 10_000) return (n / 1_000).toFixed(1) + 'K'
  return Number(n).toFixed(decimals)
}

function fmtUptime(s) {
  if (!s) return '0s'
  if (s < 60) return `${s}s`
  if (s < 3600) return `${Math.floor(s / 60)}m ${s % 60}s`
  return `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m`
}

function cpuColorClass(pct) {
  if (pct >= 80) return 'bg-red-500'
  if (pct >= 50) return 'bg-amber-500'
  return 'bg-emerald-500'
}

function cpuTextClass(pct) {
  if (pct >= 80) return 'text-red-400'
  if (pct >= 50) return 'text-amber-400'
  return 'text-emerald-400'
}

// ─── Summary Stat Card ─────────────────────────────────────────────────────────

function StatCard({ label, value, unit, sub, spark, sparkColor = '#818cf8' }) {
  return (
    <div className="bg-slate-900 border border-slate-700/40 rounded-xl p-4 flex flex-col gap-2">
      <span className="text-[10px] font-semibold text-slate-500 uppercase tracking-widest">
        {label}
      </span>
      <div className="flex items-end gap-1.5 min-h-[2.75rem]">
        <span className="text-3xl font-mono font-semibold tabular-nums text-slate-50 leading-none">
          {value}
        </span>
        {unit && <span className="text-sm text-slate-500 mb-0.5">{unit}</span>}
      </div>
      {sub && <span className="text-xs text-slate-500">{sub}</span>}
      {spark && spark.length > 1 && (
        <div className="mt-1">
          <Sparkline data={spark} color={sparkColor} height={28} />
        </div>
      )}
    </div>
  )
}

// ─── Replica Card ──────────────────────────────────────────────────────────────

function ReplicaCard({ hostname, snap, reqHistory }) {
  const cpu = snap.cpu_percent ?? 0
  const mem = snap.mem_mb ?? 0
  const stale = Date.now() - (snap._fetchedAt ?? 0) > 5000
  const shortHost = hostname.length > 14 ? hostname.slice(0, 14) + '…' : hostname

  return (
    <div className="bg-slate-900 border border-slate-700/40 rounded-xl p-5 flex flex-col gap-4">
      {/* Header */}
      <div className="flex items-start justify-between gap-2">
        <div className="min-w-0">
          <div className="flex items-center gap-2">
            <PulseDot active={!stale} />
            <span className="text-sm font-mono font-medium text-slate-200 truncate" title={hostname}>
              {shortHost}
            </span>
          </div>
          <span className="text-[11px] text-slate-500 mt-0.5 block">
            up {fmtUptime(snap.uptime_s ?? 0)}
          </span>
        </div>
        <div className="text-right flex-shrink-0">
          <span className="text-xs font-mono text-slate-400">
            {fmt(snap.active_conns ?? 0)}
          </span>
          <span className="text-[10px] text-slate-600 block">active conn</span>
        </div>
      </div>

      {/* Req/s sparkline */}
      <div>
        <div className="flex items-center justify-between mb-1">
          <span className="text-[10px] text-slate-500 uppercase tracking-widest">Req / s</span>
          <span className="text-xs font-mono font-semibold text-slate-200">
            {reqHistory.at(-1) ?? 0}
          </span>
        </div>
        <Sparkline data={reqHistory.length >= 2 ? reqHistory : [0, 0]} color="#818cf8" height={32} />
      </div>

      {/* CPU */}
      <div>
        <div className="flex items-center justify-between mb-1.5">
          <span className="text-[10px] text-slate-500 uppercase tracking-widest">CPU</span>
          <span className={`text-xs font-mono font-semibold ${cpuTextClass(cpu)}`}>
            {cpu.toFixed(1)}%
          </span>
        </div>
        <Bar pct={cpu} colorClass={cpuColorClass(cpu)} />
      </div>

      {/* Memory */}
      <div>
        <div className="flex items-center justify-between mb-1.5">
          <span className="text-[10px] text-slate-500 uppercase tracking-widest">Memory</span>
          <span className="text-xs font-mono text-slate-300">{mem.toFixed(1)} MB</span>
        </div>
        <Bar pct={(mem / 256) * 100} colorClass="bg-indigo-500" />
      </div>

      {/* Engine avg */}
      {snap.engine_count > 0 && (
        <div className="pt-3 border-t border-slate-800 flex items-center justify-between">
          <span className="text-[10px] text-slate-500 uppercase tracking-widest">Engine avg</span>
          <span className="text-xs font-mono text-slate-300">
            {(snap.engine_avg_ms ?? 0).toFixed(0)}
            <span className="text-slate-600 ml-0.5">ms</span>
          </span>
        </div>
      )}
    </div>
  )
}

// ─── Engine Replica Card ───────────────────────────────────────────────────────

function EngineReplicaCard({ snap }) {
  const cpu = snap.cpu_percent ?? 0
  const inFlight = snap.searches_in_flight ?? 0
  const shortHost = snap.hostname.length > 14 ? snap.hostname.slice(0, 14) + '…' : snap.hostname

  return (
    <div className="bg-slate-900 border border-slate-700/40 rounded-xl p-5 flex flex-col gap-4">
      <div className="flex items-start justify-between gap-2">
        <div className="min-w-0">
          <div className="flex items-center gap-2">
            <PulseDot active={true} />
            <span className="text-sm font-mono font-medium text-slate-200 truncate" title={snap.hostname}>
              {shortHost}
            </span>
          </div>
          <span className="text-[11px] text-slate-500 mt-0.5 block">C++ engine</span>
        </div>
        {inFlight > 0 && (
          <span className="inline-flex items-center gap-1 px-1.5 py-0.5 rounded-full bg-amber-500/15 border border-amber-500/30 flex-shrink-0">
            <span className="w-1.5 h-1.5 rounded-full bg-amber-400 animate-pulse" />
            <span className="text-[9px] font-semibold text-amber-400 uppercase tracking-wide">
              searching
            </span>
          </span>
        )}
      </div>

      <div>
        <div className="flex items-center justify-between mb-1.5">
          <span className="text-[10px] text-slate-500 uppercase tracking-widest">CPU</span>
          <span className={`text-xs font-mono font-semibold ${cpuTextClass(cpu)}`}>
            {cpu.toFixed(1)}%
          </span>
        </div>
        <Bar pct={cpu} colorClass={cpuColorClass(cpu)} />
      </div>

      <div className="flex items-center justify-between">
        <span className="text-[10px] text-slate-500 uppercase tracking-widest">Searches in flight</span>
        <span className="text-xs font-mono text-slate-300">{inFlight}</span>
      </div>
    </div>
  )
}

// ─── Routes Table ──────────────────────────────────────────────────────────────

function RoutesTable({ routes }) {
  // Aggregate the same route path across replicas
  const agg = new Map()
  for (const r of routes) {
    if (!agg.has(r.path)) {
      agg.set(r.path, { path: r.path, count: 0, totalMs: 0, errors: 0, p95s: [] })
    }
    const a = agg.get(r.path)
    a.count    += r.count
    a.totalMs  += r.avg_ms * r.count
    a.errors   += r.errors
    a.p95s.push(r.p95_ms)
  }
  const rows = [...agg.values()]
    .map(a => ({
      path:    a.path,
      count:   a.count,
      avg_ms:  a.count > 0 ? a.totalMs / a.count : 0,
      p95_ms:  a.p95s.length ? Math.max(...a.p95s) : 0,
      errors:  a.errors,
      errPct:  a.count > 0 ? (a.errors / a.count) * 100 : 0,
    }))
    .sort((a, b) => b.count - a.count)

  return (
    <div className="bg-slate-900 border border-slate-700/40 rounded-xl overflow-hidden h-full">
      <div className="px-5 py-3.5 border-b border-slate-800">
        <span className="text-[10px] font-semibold text-slate-500 uppercase tracking-widest">
          Route Breakdown
        </span>
      </div>
      {rows.length === 0 ? (
        <div className="px-5 py-8 text-center text-slate-600 text-sm">
          No route data yet — make some requests.
        </div>
      ) : (
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="border-b border-slate-800">
                {['Route', 'Calls', 'Avg', 'P95', 'Errors'].map(h => (
                  <th
                    key={h}
                    className="text-left px-5 py-2.5 text-[10px] font-semibold text-slate-500 uppercase tracking-widest whitespace-nowrap"
                  >
                    {h}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-800/60">
              {rows.map(r => (
                <tr key={r.path} className="hover:bg-slate-800/30 transition-colors group">
                  <td className="px-5 py-3 font-mono text-xs text-slate-300 whitespace-nowrap">
                    {r.path}
                  </td>
                  <td className="px-5 py-3 font-mono tabular-nums text-slate-100 whitespace-nowrap">
                    {fmt(r.count)}
                  </td>
                  <td className="px-5 py-3 font-mono tabular-nums text-slate-400 whitespace-nowrap">
                    {r.avg_ms.toFixed(1)}
                    <span className="text-slate-600 text-[10px] ml-0.5">ms</span>
                  </td>
                  <td className="px-5 py-3 font-mono tabular-nums text-slate-400 whitespace-nowrap">
                    {r.p95_ms}
                    <span className="text-slate-600 text-[10px] ml-0.5">ms</span>
                  </td>
                  <td className="px-5 py-3 whitespace-nowrap">
                    {r.errors > 0 ? (
                      <span className="inline-flex items-center gap-1.5">
                        <span className="font-mono tabular-nums text-red-400">{fmt(r.errors)}</span>
                        <span className="text-[10px] text-red-600">
                          ({r.errPct.toFixed(1)}%)
                        </span>
                      </span>
                    ) : (
                      <span className="text-slate-700 font-mono">—</span>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  )
}

// ─── Engine Panel ─────────────────────────────────────────────────────────────

function EnginePanel({ totalCalls, avgMs, errors }) {
  const errRate = totalCalls > 0 ? (errors / totalCalls) * 100 : 0
  return (
    <div className="bg-slate-900 border border-slate-700/40 rounded-xl p-5 flex flex-col gap-4">
      <span className="text-[10px] font-semibold text-slate-500 uppercase tracking-widest">
        C++ Engine
      </span>
      <div className="grid grid-cols-2 gap-y-4 gap-x-3">
        <div>
          <p className="text-[10px] text-slate-500 uppercase tracking-widest mb-1">Total Calls</p>
          <p className="text-2xl font-mono font-semibold text-slate-100">{fmt(totalCalls)}</p>
        </div>
        <div>
          <p className="text-[10px] text-slate-500 uppercase tracking-widest mb-1">Avg Latency</p>
          <p className="text-2xl font-mono font-semibold text-slate-100">
            {avgMs.toFixed(0)}
            <span className="text-sm text-slate-500 ml-1">ms</span>
          </p>
        </div>
        <div>
          <p className="text-[10px] text-slate-500 uppercase tracking-widest mb-1">Errors</p>
          <p className={`text-2xl font-mono font-semibold ${errors > 0 ? 'text-red-400' : 'text-slate-100'}`}>
            {fmt(errors)}
          </p>
        </div>
        <div>
          <p className="text-[10px] text-slate-500 uppercase tracking-widest mb-1">Error Rate</p>
          <p className={`text-2xl font-mono font-semibold ${errRate > 0 ? 'text-red-400' : 'text-emerald-400'}`}>
            {errRate.toFixed(1)}
            <span className="text-sm text-slate-500 ml-0.5">%</span>
          </p>
        </div>
      </div>
    </div>
  )
}

// ─── DB Panel ────────────────────────────────────────────────────────────────

function DBPanel({ reads, writes }) {
  const total = reads + writes
  const readPct = total > 0 ? (reads / total) * 100 : 50
  return (
    <div className="bg-slate-900 border border-slate-700/40 rounded-xl p-5 flex flex-col gap-4">
      <span className="text-[10px] font-semibold text-slate-500 uppercase tracking-widest">
        Database
      </span>
      <div className="grid grid-cols-2 gap-y-4 gap-x-3">
        <div>
          <p className="text-[10px] text-slate-500 uppercase tracking-widest mb-1">Reads</p>
          <p className="text-2xl font-mono font-semibold text-sky-300">{fmt(reads)}</p>
        </div>
        <div>
          <p className="text-[10px] text-slate-500 uppercase tracking-widest mb-1">Writes</p>
          <p className="text-2xl font-mono font-semibold text-indigo-300">{fmt(writes)}</p>
        </div>
      </div>
      <div>
        <p className="text-[10px] text-slate-500 uppercase tracking-widest mb-2">Read / Write Split</p>
        {total > 0 ? (
          <>
            <div className="h-2 w-full rounded-full overflow-hidden flex">
              <div
                className="h-full bg-sky-500 transition-all duration-700"
                style={{ width: `${readPct}%` }}
              />
              <div className="h-full bg-indigo-500 flex-1 transition-all duration-700" />
            </div>
            <div className="flex justify-between mt-1.5">
              <span className="text-[10px] text-sky-500">{readPct.toFixed(0)}% reads</span>
              <span className="text-[10px] text-indigo-400">{(100 - readPct).toFixed(0)}% writes</span>
            </div>
          </>
        ) : (
          <div className="h-2 w-full bg-slate-800 rounded-full" />
        )}
      </div>
    </div>
  )
}

// ─── Main Component ───────────────────────────────────────────────────────────

export default function InfraDashboard({ onLeave }) {
  const [, forceUpdate] = useState(0)
  const [confirmReset, setConfirmReset] = useState(false)
  const [resetting, setResetting] = useState(false)
  const replicasRef    = useRef(new Map())  // hostname → { snap, reqHistory }
  const allReqHistRef  = useRef([])          // aggregate req/s sparkline
  const lastUpdateRef  = useRef(null)

  async function handleReset() {
    setResetting(true)
    try {
      await api.resetDB()
      replicasRef.current.clear()
      allReqHistRef.current = []
      forceUpdate(n => n + 1)
    } finally {
      setResetting(false)
      setConfirmReset(false)
    }
  }

  useEffect(() => {
    async function poll() {
      // Fire PROBE_COUNT parallel requests — Traefik round-robins them to different replicas
      const results = await Promise.allSettled(
        Array.from({ length: PROBE_COUNT }, () => api.stats())
      )

      let updated = false
      for (const r of results) {
        const snap = r.status === 'fulfilled' ? r.value : null
        if (!snap?.hostname) continue
        snap._fetchedAt = Date.now()

        const isNew    = !replicasRef.current.has(snap.hostname)
        const existing = replicasRef.current.get(snap.hostname)
        const prevCount = existing?.snap?.routes?.reduce((s, r) => s + r.count, 0) ?? 0
        const newCount  = snap.routes?.reduce((s, r) => s + r.count, 0) ?? 0
        // Suppress the initial spike on first observation (delta = historical total)
        const delta = isNew ? 0 : Math.max(0, newCount - prevCount)

        const prevHistory = existing?.reqHistory ?? []
        replicasRef.current.set(snap.hostname, {
          snap,
          reqHistory: [...prevHistory.slice(-(HISTORY_LEN - 1)), delta],
        })
        updated = true
      }

      if (updated) {
        // Aggregate req/s across all replicas for the summary sparkline
        const totalRps = [...replicasRef.current.values()]
          .reduce((s, r) => s + (r.reqHistory.at(-1) ?? 0), 0)
        allReqHistRef.current = [...allReqHistRef.current.slice(-(HISTORY_LEN - 1)), totalRps]
        lastUpdateRef.current = Date.now()
        forceUpdate(n => n + 1)
      }
    }

    poll()
    const timer = setInterval(poll, POLL_MS)
    return () => clearInterval(timer)
  }, [])

  const replicas  = [...replicasRef.current.values()]
  const allSnaps  = replicas.map(r => r.snap).filter(Boolean)

  // Aggregate totals across all replicas
  const totalReqs     = allSnaps.reduce((s, snap) => s + (snap.routes?.reduce((rs, r) => rs + r.count, 0) ?? 0), 0)
  const totalConns    = allSnaps.reduce((s, snap) => s + (snap.active_conns ?? 0), 0)
  const totalEngine   = allSnaps.reduce((s, snap) => s + (snap.engine_count ?? 0), 0)
  const totalEngErr   = allSnaps.reduce((s, snap) => s + (snap.engine_errors ?? 0), 0)
  const totalInFlight = allSnaps.reduce((s, snap) => s + (snap.engine_in_flight ?? 0), 0)

  // Deduplicate engine replicas by hostname — take the latest report from any backend replica
  const engineReplicaMap = new Map()
  for (const snap of allSnaps) {
    for (const er of (snap.engine_replicas ?? [])) {
      engineReplicaMap.set(er.hostname, er)
    }
  }
  const engineReplicas = [...engineReplicaMap.values()]
  const totalDBReads  = allSnaps.reduce((s, snap) => s + (snap.db_reads ?? 0), 0)
  const totalDBWrites = allSnaps.reduce((s, snap) => s + (snap.db_writes ?? 0), 0)
  const avgEngineMs   = totalEngine > 0
    ? allSnaps.reduce((s, snap) => s + (snap.engine_avg_ms ?? 0) * (snap.engine_count ?? 0), 0) / totalEngine
    : 0

  const allRoutes   = allSnaps.flatMap(snap => snap.routes ?? [])
  const secondsAgo  = lastUpdateRef.current ? Math.round((Date.now() - lastUpdateRef.current) / 1000) : null
  const isLive      = secondsAgo !== null && secondsAgo < 5

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100">

      {/* ── Header ───────────────────────────────────────────────────────── */}
      <header className="sticky top-0 z-10 bg-slate-950/90 backdrop-blur-sm border-b border-slate-800 px-6 py-3.5 flex items-center justify-between">
        <div className="flex items-center gap-3">
          <button
            onClick={onLeave}
            className="flex items-center gap-1.5 text-slate-400 hover:text-slate-200 transition-colors text-sm"
          >
            <svg className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth={2}>
              <path strokeLinecap="round" strokeLinejoin="round" d="M15 19l-7-7 7-7" />
            </svg>
            Dashboard
          </button>
          <span className="text-slate-700 select-none">·</span>
          <span className="text-sm font-semibold text-slate-100">Infrastructure</span>
        </div>
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-2.5">
            <PulseDot active={isLive} />
            <span className="text-xs text-slate-400 tabular-nums">
              {secondsAgo === null
                ? 'Connecting…'
                : isLive
                ? 'Live'
                : `Updated ${secondsAgo}s ago`}
            </span>
          </div>
          <button
            onClick={() => setConfirmReset(true)}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium bg-red-950/60 text-red-400 border border-red-800/50 hover:bg-red-900/60 hover:text-red-300 transition-colors"
          >
            <svg className="w-3.5 h-3.5" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth={2}>
              <path strokeLinecap="round" strokeLinejoin="round" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
            </svg>
            Delete All Data
          </button>
        </div>
      </header>

      {/* ── Confirm Reset Modal ───────────────────────────────────────────── */}
      {confirmReset && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 backdrop-blur-sm">
          <div className="bg-slate-900 border border-slate-700 rounded-2xl p-6 max-w-sm w-full mx-4 shadow-2xl">
            <h3 className="text-base font-semibold text-slate-100 mb-2">Delete all data?</h3>
            <p className="text-sm text-slate-400 mb-6">
              This will permanently delete all users, games, and moves from the database. The action cannot be undone.
            </p>
            <div className="flex gap-3 justify-end">
              <button
                onClick={() => setConfirmReset(false)}
                disabled={resetting}
                className="px-4 py-2 rounded-lg text-sm font-medium text-slate-300 bg-slate-800 hover:bg-slate-700 transition-colors disabled:opacity-50"
              >
                Cancel
              </button>
              <button
                onClick={handleReset}
                disabled={resetting}
                className="px-4 py-2 rounded-lg text-sm font-medium text-white bg-red-600 hover:bg-red-500 transition-colors disabled:opacity-50 flex items-center gap-2"
              >
                {resetting && (
                  <svg className="w-3.5 h-3.5 animate-spin" fill="none" viewBox="0 0 24 24">
                    <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" />
                    <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
                  </svg>
                )}
                {resetting ? 'Deleting…' : 'Delete Everything'}
              </button>
            </div>
          </div>
        </div>
      )}

      {/* ── Content ──────────────────────────────────────────────────────── */}
      <main className="max-w-7xl mx-auto px-6 py-8 space-y-8">

        {/* Summary strip */}
        <div className="grid grid-cols-2 lg:grid-cols-4 gap-4">
          <StatCard
            label="Total Requests"
            value={fmt(totalReqs)}
            sub={`${replicas.length} replica${replicas.length !== 1 ? 's' : ''} reporting`}
            spark={allReqHistRef.current}
            sparkColor="#818cf8"
          />
          <StatCard
            label="Active Connections"
            value={fmt(totalConns)}
            sub="across all replicas"
          />
          <StatCard
            label="Engine Calls"
            value={fmt(totalEngine)}
            sub={totalInFlight > 0
              ? `${totalInFlight} searching · ${engineReplicas.length} replica${engineReplicas.length !== 1 ? 's' : ''}`
              : totalEngine > 0
              ? `avg ${avgEngineMs.toFixed(0)}ms · ${totalEngErr} err`
              : 'no calls yet'}
            sparkColor="#f59e0b"
          />
          <StatCard
            label="DB Operations"
            value={fmt(totalDBReads + totalDBWrites)}
            sub={`${fmt(totalDBReads)} reads · ${fmt(totalDBWrites)} writes`}
            sparkColor="#38bdf8"
          />
        </div>

        {/* Replicas */}
        <section>
          <div className="flex items-center justify-between mb-4">
            <div>
              <h2 className="text-xs font-semibold text-slate-300 uppercase tracking-widest">
                Replicas
              </h2>
              <p className="text-xs text-slate-500 mt-0.5">
                {replicas.length === 0
                  ? 'Probing via load balancer…'
                  : `${replicas.length} replica${replicas.length !== 1 ? 's' : ''} discovered via round-robin`}
              </p>
            </div>
          </div>

          {replicas.length === 0 ? (
            <div className="bg-slate-900 border border-slate-700/40 rounded-xl p-10 flex items-center justify-center">
              <div className="flex items-center gap-2.5 text-slate-500 text-sm">
                <svg className="w-4 h-4 animate-spin flex-shrink-0" fill="none" viewBox="0 0 24 24">
                  <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" />
                  <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
                </svg>
                Waiting for replica responses…
              </div>
            </div>
          ) : (
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
              {replicas.map(({ snap, reqHistory }) => (
                <ReplicaCard
                  key={snap.hostname}
                  hostname={snap.hostname}
                  snap={snap}
                  reqHistory={reqHistory}
                />
              ))}
            </div>
          )}
        </section>

        {/* Engine replicas */}
        {engineReplicas.length > 0 && (
          <section>
            <div className="mb-4">
              <h2 className="text-xs font-semibold text-slate-300 uppercase tracking-widest">
                C++ Engine Replicas
              </h2>
              <p className="text-xs text-slate-500 mt-0.5">
                {engineReplicas.length} replica{engineReplicas.length !== 1 ? 's' : ''} discovered
              </p>
            </div>
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4">
              {engineReplicas.map(er => (
                <EngineReplicaCard key={er.hostname} snap={er} />
              ))}
            </div>
          </section>
        )}

        {/* Route table + Engine + DB */}
        <section className="grid grid-cols-1 lg:grid-cols-3 gap-4">
          <div className="lg:col-span-2">
            <RoutesTable routes={allRoutes} />
          </div>
          <div className="flex flex-col gap-4">
            <EnginePanel
              totalCalls={totalEngine}
              avgMs={avgEngineMs}
              errors={totalEngErr}
            />
            <DBPanel reads={totalDBReads} writes={totalDBWrites} />
          </div>
        </section>

      </main>
    </div>
  )
}
