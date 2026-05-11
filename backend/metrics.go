package main

import (
	"bufio"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"runtime"
	"slices"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const ringSize = 128

// globalMetrics is the in-process metrics singleton for this replica.
var globalMetrics = newMetricsStore()

type routeStats struct {
	count   atomic.Int64
	totalMs atomic.Int64
	errors  atomic.Int64
	ring    [ringSize]int64
	ringIdx atomic.Int64
}

func (rs *routeStats) record(ms int64, isErr bool) {
	rs.count.Add(1)
	rs.totalMs.Add(ms)
	if isErr {
		rs.errors.Add(1)
	}
	idx := rs.ringIdx.Add(1) % ringSize
	rs.ring[idx] = ms
}

func (rs *routeStats) percentiles() (p50, p95 int64) {
	samples := make([]int64, 0, ringSize)
	for i := range ringSize {
		if v := rs.ring[i]; v > 0 {
			samples = append(samples, v)
		}
	}
	if len(samples) == 0 {
		return 0, 0
	}
	slices.Sort(samples)
	n := len(samples)
	return samples[n*50/100], samples[n*95/100]
}

// EngineReplicaSnap holds the latest stats for one C++ engine replica.
type EngineReplicaSnap struct {
	Hostname         string  `json:"hostname"`
	CPUPercent       float64 `json:"cpu_percent"`
	SearchesInFlight int64   `json:"searches_in_flight"`
}

type metricsStore struct {
	routes         sync.Map // string → *routeStats
	engineCount    atomic.Int64
	engineErrors   atomic.Int64
	engineTotalMs  atomic.Int64
	engineInFlight atomic.Int64
	dbReads        atomic.Int64
	dbWrites       atomic.Int64
	activeConns    atomic.Int64
	startTime      time.Time
	hostname       string
	cpuPercent     atomic.Value // float64

	engineReplicas sync.Map // hostname → *EngineReplicaSnap
}

func newMetricsStore() *metricsStore {
	hostname, _ := os.Hostname()
	m := &metricsStore{
		startTime: time.Now(),
		hostname:  hostname,
	}
	m.cpuPercent.Store(float64(0))
	go m.trackCPU()
	go m.trackEngineCPU()
	return m
}

func (m *metricsStore) routeFor(path string) *routeStats {
	v, _ := m.routes.LoadOrStore(path, &routeStats{})
	return v.(*routeStats)
}

func (m *metricsStore) recordRequest(path string, ms int64, isErr bool) {
	m.routeFor(path).record(ms, isErr)
}

func (m *metricsStore) recordEngine(ms int64, isErr bool) {
	m.engineCount.Add(1)
	m.engineTotalMs.Add(ms)
	if isErr {
		m.engineErrors.Add(1)
	}
}

func (m *metricsStore) engineBegin() { m.engineInFlight.Add(1) }
func (m *metricsStore) engineEnd()   { m.engineInFlight.Add(-1) }

// trackEngineCPU probes the engine's /stats endpoint several times per tick
// (hitting different replicas via Docker DNS round-robin) and stores per-replica stats.
func (m *metricsStore) trackEngineCPU() {
	const probes = 4
	client := &http.Client{Timeout: 2 * time.Second}
	for range time.Tick(time.Second) {
		type result struct{ snap *EngineReplicaSnap }
		ch := make(chan result, probes)
		for range probes {
			go func() {
				resp, err := client.Get(engineURL() + "/stats")
				if err != nil {
					ch <- result{}
					return
				}
				body, err := io.ReadAll(resp.Body)
				resp.Body.Close()
				if err != nil {
					ch <- result{}
					return
				}
				var snap EngineReplicaSnap
				if err := json.Unmarshal(body, &snap); err != nil || snap.Hostname == "" {
					ch <- result{}
					return
				}
				ch <- result{&snap}
			}()
		}
		for range probes {
			r := <-ch
			if r.snap != nil {
				m.engineReplicas.Store(r.snap.Hostname, r.snap)
			}
		}
	}
}

func (m *metricsStore) recordDB(write bool) {
	if write {
		m.dbWrites.Add(1)
	} else {
		m.dbReads.Add(1)
	}
}

// trackCPU samples /proc/self/stat every second and updates cpuPercent.
func (m *metricsStore) trackCPU() {
	prev, prevTime, ok := cpuTicks()
	if !ok {
		return
	}
	for range time.Tick(time.Second) {
		cur, curTime, ok := cpuTicks()
		if !ok {
			continue
		}
		elapsed := curTime.Sub(prevTime).Seconds()
		if elapsed <= 0 {
			continue
		}
		// Linux scheduler runs at 100 ticks/sec per core.
		tps := float64(runtime.NumCPU()) * 100.0
		pct := float64(cur-prev) / (tps * elapsed) * 100
		if pct > 100 {
			pct = 100
		}
		m.cpuPercent.Store(pct)
		prev, prevTime = cur, curTime
	}
}

func cpuTicks() (ticks int64, t time.Time, ok bool) {
	f, err := os.Open("/proc/self/stat")
	if err != nil {
		return 0, time.Time{}, false
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	if !s.Scan() {
		return 0, time.Time{}, false
	}
	fields := strings.Fields(s.Text())
	if len(fields) < 15 {
		return 0, time.Time{}, false
	}
	utime, _ := strconv.ParseInt(fields[13], 10, 64)
	stime, _ := strconv.ParseInt(fields[14], 10, 64)
	return utime + stime, time.Now(), true
}

func memMB() float64 {
	f, err := os.Open("/proc/self/status")
	if err != nil {
		return 0
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	for s.Scan() {
		if strings.HasPrefix(s.Text(), "VmRSS:") {
			if fields := strings.Fields(s.Text()); len(fields) >= 2 {
				kb, _ := strconv.ParseFloat(fields[1], 64)
				return kb / 1024
			}
		}
	}
	return 0
}
