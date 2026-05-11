package main

import (
	"net/http"
	"time"
)

type routeSnapshot struct {
	Path   string  `json:"path"`
	Count  int64   `json:"count"`
	AvgMs  float64 `json:"avg_ms"`
	P50Ms  int64   `json:"p50_ms"`
	P95Ms  int64   `json:"p95_ms"`
	Errors int64   `json:"errors"`
}

type statsSnapshot struct {
	Hostname        string               `json:"hostname"`
	UptimeS         int64                `json:"uptime_s"`
	CPUPercent      float64              `json:"cpu_percent"`
	MemMB           float64              `json:"mem_mb"`
	Routes          []routeSnapshot      `json:"routes"`
	EngineCount     int64                `json:"engine_count"`
	EngineErrors    int64                `json:"engine_errors"`
	EngineAvgMs     float64              `json:"engine_avg_ms"`
	EngineInFlight  int64                `json:"engine_in_flight"`
	EngineReplicas  []*EngineReplicaSnap `json:"engine_replicas"`
	DBReads         int64                `json:"db_reads"`
	DBWrites        int64                `json:"db_writes"`
	ActiveConns     int64                `json:"active_conns"`
}

func statsHandler(m *metricsStore) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-cache")
		snap := statsSnapshot{
			Hostname:       m.hostname,
			UptimeS:        int64(time.Since(m.startTime).Seconds()),
			CPUPercent:     m.cpuPercent.Load().(float64),
			MemMB:          memMB(),
			EngineCount:    m.engineCount.Load(),
			EngineErrors:   m.engineErrors.Load(),
			EngineInFlight: m.engineInFlight.Load(),
			DBReads:        m.dbReads.Load(),
			DBWrites:       m.dbWrites.Load(),
			ActiveConns:    m.activeConns.Load(),
		}
		m.engineReplicas.Range(func(_, v any) bool {
			snap.EngineReplicas = append(snap.EngineReplicas, v.(*EngineReplicaSnap))
			return true
		})
		if snap.EngineCount > 0 {
			snap.EngineAvgMs = float64(m.engineTotalMs.Load()) / float64(snap.EngineCount)
		}
		m.routes.Range(func(k, v any) bool {
			rs := v.(*routeStats)
			count := rs.count.Load()
			p50, p95 := rs.percentiles()
			s := routeSnapshot{
				Path:   k.(string),
				Count:  count,
				Errors: rs.errors.Load(),
				P50Ms:  p50,
				P95Ms:  p95,
			}
			if count > 0 {
				s.AvgMs = float64(rs.totalMs.Load()) / float64(count)
			}
			snap.Routes = append(snap.Routes, s)
			return true
		})
		writeJSON(w, http.StatusOK, snap)
	}
}

// tracked wraps h with request timing and active-connection tracking.
func tracked(route string, h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		globalMetrics.activeConns.Add(1)
		defer globalMetrics.activeConns.Add(-1)
		start := time.Now()
		sw := &statusWriter{ResponseWriter: w}
		h.ServeHTTP(sw, r)
		ms := time.Since(start).Milliseconds()
		globalMetrics.recordRequest(route, ms, sw.code >= 500)
	})
}

type statusWriter struct {
	http.ResponseWriter
	code int
}

func (s *statusWriter) WriteHeader(code int) {
	s.code = code
	s.ResponseWriter.WriteHeader(code)
}

// Flush proxies to the underlying ResponseWriter so SSE handlers get a real Flusher.
func (s *statusWriter) Flush() {
	if f, ok := s.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	}
}
