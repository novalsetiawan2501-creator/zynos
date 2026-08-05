// Package main - Load Testing Tool
//
// Tool ini mensimulasikan traffic tinggi ke sebuah endpoint web menggunakan
// protokol HTTP/2 dengan method GET. Dirancang dengan connection pooling,
// worker pool, rate limiting, dan graceful shutdown berbasis context.
//
// Arsitektur singkat:
//
//	[Rate Limiter] --token--> [N Producer Goroutine] --job--> [Jobs Channel] --> [M Worker Pool] --> HTTP/2 Client
//
// - "goroutine"  : jumlah goroutine producer yang menghasilkan job request (dibatasi rate limiter)
// - "worker"     : jumlah goroutine worker yang benar-benar mengeksekusi request HTTP
// - "connection" : jumlah koneksi TCP paralel maksimum ke host target (connection pooling)
// - "rate"       : target request per detik dasar
// - "multiplier" : pengkali rate dasar -> target akhir = rate * multiplier
// - "time"       : durasi total pengujian dalam detik
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"golang.org/x/net/http2"
)

// ========================= KONFIGURASI =========================

// Config menampung semua parameter yang bisa diatur lewat flag CLI.
type Config struct {
	URL        string // target endpoint
	Duration   int    // durasi pengujian (detik)
	Rate       int    // request per detik (dasar)
	Goroutine  int    // jumlah goroutine producer/concurrent
	Connection int    // jumlah koneksi paralel ke host
	Multiplier int    // pengkali rate dasar
	Worker     int    // jumlah worker pool
}

// TargetRPS menghitung target request/detik akhir setelah dikali multiplier.
func (c Config) TargetRPS() int {
	rps := c.Rate * c.Multiplier
	if rps < 1 {
		rps = 1
	}
	return rps
}

// ========================= STATISTIK =========================

// Stats menyimpan seluruh counter statistik secara atomic agar aman
// diakses oleh banyak goroutine sekaligus tanpa lock eksplisit.
type Stats struct {
	TotalSent      atomic.Int64 // total request yang dikirim
	TotalSuccess   atomic.Int64 // total response 2xx
	TotalFailed    atomic.Int64 // total error / non-2xx
	TotalLatencyNs atomic.Int64 // akumulasi latency (nanosecond) untuk hitung rata-rata
	BytesRead      atomic.Int64 // total byte body yang dibaca (opsional, indikasi non-blocking read)
}

// snapshot mengambil nilai statistik saat ini dalam bentuk yang mudah dicetak.
func (s *Stats) snapshot() (sent, success, failed int64, avgLatencyMs float64) {
	sent = s.TotalSent.Load()
	success = s.TotalSuccess.Load()
	failed = s.TotalFailed.Load()
	totalLat := s.TotalLatencyNs.Load()
	if sent > 0 {
		avgLatencyMs = float64(totalLat) / float64(sent) / float64(time.Millisecond)
	}
	return
}

// ========================= HTTP/2 CLIENT =========================

// newHTTP2Client membangun *http.Client dengan transport yang dikonfigurasi
// untuk HTTP/2 serta connection pooling sesuai jumlah koneksi paralel yang diminta.
func newHTTP2Client(maxConnPerHost int) (*http.Client, error) {
	transport := &http.Transport{
		// Connection pooling: batasi & jaga koneksi paralel ke host target
		MaxConnsPerHost:     maxConnPerHost,
		MaxIdleConnsPerHost: maxConnPerHost,
		MaxIdleConns:        maxConnPerHost * 2,
		IdleConnTimeout:     90 * time.Second,
		// Timeout untuk fase TLS handshake / dial supaya goroutine tidak menggantung
		TLSHandshakeTimeout: 10 * time.Second,
	}

	// Mengaktifkan HTTP/2 di atas transport HTTP standar (upgrade otomatis
	// via ALPN saat target mendukung h2 lewat TLS). Jika server tidak
	// mendukung HTTP/2, koneksi akan otomatis fallback ke HTTP/1.1.
	if err := http2.ConfigureTransport(transport); err != nil {
		return nil, fmt.Errorf("gagal konfigurasi HTTP/2 transport: %w", err)
	}

	client := &http.Client{
		Transport: transport,
		Timeout:   15 * time.Second, // timeout per-request, mencegah goroutine macet selamanya
	}
	return client, nil
}

// ========================= WORKER =========================

// doRequest mengeksekusi satu request GET dan mencatat hasilnya ke Stats.
// Body response dibaca & dibuang secara non-blocking memakai io.Copy ke Discard
// supaya koneksi bisa di-reuse oleh transport (keep-alive) dan tidak membocorkan resource.
func doRequest(ctx context.Context, client *http.Client, url string, stats *Stats) {
	start := time.Now()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		stats.TotalFailed.Add(1)
		stats.TotalSent.Add(1)
		return
	}
	// Header HTTP sederhana
	req.Header.Set("User-Agent", "go-loadtest-tool/1.0")
	req.Header.Set("Accept", "*/*")

	resp, err := client.Do(req)
	latency := time.Since(start)

	stats.TotalSent.Add(1)
	stats.TotalLatencyNs.Add(int64(latency))

	if err != nil {
		// Error handling basic: koneksi gagal, timeout, dsb.
		stats.TotalFailed.Add(1)
		return
	}
	defer resp.Body.Close()

	// Baca & buang body agar koneksi bisa direuse (drainBody), sekaligus hitung ukurannya.
	n, _ := drainBody(resp.Body)
	stats.BytesRead.Add(n)

	if resp.StatusCode >= 200 && resp.StatusCode < 300 {
		stats.TotalSuccess.Add(1)
	} else {
		stats.TotalFailed.Add(1)
	}
}

// drainBody membaca seluruh body dan membuang isinya, mengembalikan jumlah byte.
// Memakai io.Copy ke io.Discard agar efisien dan mencegah memory leak pada body besar,
// sekaligus memungkinkan koneksi TCP di-reuse oleh transport (keep-alive).
func drainBody(body io.Reader) (int64, error) {
	return io.Copy(io.Discard, body)
}

// worker adalah unit worker pool yang mengonsumsi job dari channel `jobs`
// dan mengeksekusi request HTTP hingga channel ditutup atau context selesai.
func worker(ctx context.Context, id int, client *http.Client, url string, jobs <-chan struct{}, stats *Stats, wg *sync.WaitGroup) {
	defer wg.Done()
	for {
		select {
		case <-ctx.Done():
			return
		case _, ok := <-jobs:
			if !ok {
				return // channel ditutup, tidak ada job baru
			}
			doRequest(ctx, client, url, stats)
		}
	}
}

// ========================= PRODUCER / RATE LIMITER =========================

// producer menghasilkan job secara rate-limited dan mengirimkannya ke channel jobs.
// Setiap producer berbagi ticker yang sama (dikirim lewat parameter) sehingga total
// throughput seluruh producer tetap mendekati target RPS keseluruhan.
func producer(ctx context.Context, id int, ticker <-chan time.Time, jobs chan<- struct{}, wg *sync.WaitGroup) {
	defer wg.Done()
	for {
		select {
		case <-ctx.Done():
			return
		case _, ok := <-ticker:
			if !ok {
				return
			}
			// Non-blocking send: kalau worker pool penuh/lambat, job di-drop
			// daripada producer ikut ngeblock (menjaga stabilitas & non-blocking I/O).
			select {
			case jobs <- struct{}{}:
			case <-ctx.Done():
				return
			default:
				// jobs channel penuh -> lewati tick ini (backpressure sederhana)
			}
		}
	}
}

// ========================= PROGRESS REPORTER =========================

// progressReporter mencetak progress bar & statistik real-time setiap 5 detik
// sampai context selesai.
func progressReporter(ctx context.Context, stats *Stats, totalDuration time.Duration, startTime time.Time) {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			elapsed := time.Since(startTime)
			printProgress(stats, elapsed, totalDuration)
		}
	}
}

// printProgress mencetak satu baris progress bar + ringkasan statistik saat ini.
func printProgress(stats *Stats, elapsed, total time.Duration) {
	sent, success, failed, avgLat := stats.snapshot()

	pct := float64(elapsed) / float64(total)
	if pct > 1 {
		pct = 1
	}
	const barWidth = 30
	filled := int(pct * barWidth)
	if filled > barWidth {
		filled = barWidth
	}
	bar := ""
	for i := 0; i < barWidth; i++ {
		if i < filled {
			bar += "="
		} else {
			bar += " "
		}
	}

	currentRPS := float64(sent) / elapsed.Seconds()

	fmt.Printf("\r[%s] %5.1f%% | %6ds/%ds | sent=%d ok=%d fail=%d | rps~%.1f | avg_latency=%.2fms",
		bar, pct*100, int(elapsed.Seconds()), int(total.Seconds()),
		sent, success, failed, currentRPS, avgLat)
	fmt.Println() // baris baru tiap 5 detik supaya histori terlihat
}

// ========================= FINAL REPORT =========================

func printFinalReport(stats *Stats, elapsed time.Duration) {
	sent, success, failed, avgLat := stats.snapshot()
	rps := float64(sent) / elapsed.Seconds()
	successRate := 0.0
	if sent > 0 {
		successRate = float64(success) / float64(sent) * 100
	}

	fmt.Println("\n=========== HASIL AKHIR LOAD TEST ===========")
	fmt.Printf("Durasi berjalan     : %.2f detik\n", elapsed.Seconds())
	fmt.Printf("Total request       : %d\n", sent)
	fmt.Printf("Berhasil (2xx)      : %d (%.2f%%)\n", success, successRate)
	fmt.Printf("Gagal/Error         : %d\n", failed)
	fmt.Printf("Rata-rata RPS       : %.2f req/s\n", rps)
	fmt.Printf("Rata-rata latency   : %.2f ms\n", avgLat)
	fmt.Printf("Total bytes dibaca  : %d bytes\n", stats.BytesRead.Load())
	fmt.Println("==============================================")
}

// ========================= FLAG PARSING =========================

func parseFlags() Config {
	url := flag.String("url", "", "Target endpoint URL (wajib), contoh: https://example.com")
	dur := flag.Int("time", 10, "Durasi pengujian dalam detik")
	rate := flag.Int("rate", 50, "Request per detik (dasar)")
	goroutine := flag.Int("goroutine", 10, "Jumlah goroutine producer/concurrent")
	connection := flag.Int("connection", 10, "Jumlah koneksi paralel ke host target")
	multiplier := flag.Int("multiplier", 1, "Pengkali dari rate dasar")
	worker := flag.Int("worker", 20, "Jumlah worker pool yang mengeksekusi request")

	flag.Parse()

	if *url == "" {
		fmt.Println("Error: parameter -url wajib diisi.")
		flag.Usage()
		os.Exit(1)
	}

	return Config{
		URL:        *url,
		Duration:   *dur,
		Rate:       *rate,
		Goroutine:  *goroutine,
		Connection: *connection,
		Multiplier: *multiplier,
		Worker:     *worker,
	}
}

// ========================= MAIN =========================

func main() {
	cfg := parseFlags()

	client, err := newHTTP2Client(cfg.Connection)
	if err != nil {
		log.Fatalf("Gagal membuat HTTP/2 client: %v", err)
	}

	targetRPS := cfg.TargetRPS()
	fmt.Printf("Mulai load test -> url=%s durasi=%ds target_rps=%d (rate=%d x multiplier=%d) goroutine=%d worker=%d connection=%d\n",
		cfg.URL, cfg.Duration, targetRPS, cfg.Rate, cfg.Multiplier, cfg.Goroutine, cfg.Worker, cfg.Connection)

	// Context utama: otomatis selesai setelah `time` detik, atau saat
	// menerima sinyal interrupt (Ctrl+C) / SIGTERM untuk graceful shutdown.
	baseCtx, stopSignal := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stopSignal()

	ctx, cancel := context.WithTimeout(baseCtx, time.Duration(cfg.Duration)*time.Second)
	defer cancel()

	stats := &Stats{}
	startTime := time.Now()

	// Jobs channel jadi jembatan antara producer (rate limiter) dan worker pool.
	// Buffer dibuat proporsional agar burst kecil tetap tertampung tanpa
	// membebani memori (mencegah memory leak akibat channel tak terbatas).
	jobsBuffer := cfg.Worker * 4
	jobs := make(chan struct{}, jobsBuffer)

	// Ticker global yang menentukan target RPS keseluruhan.
	interval := time.Second / time.Duration(targetRPS)
	if interval <= 0 {
		interval = time.Nanosecond
	}
	rateTicker := time.NewTicker(interval)
	defer rateTicker.Stop()

	var wgWorkers sync.WaitGroup
	var wgProducers sync.WaitGroup

	// Jalankan worker pool
	for i := 0; i < cfg.Worker; i++ {
		wgWorkers.Add(1)
		go worker(ctx, i, client, cfg.URL, jobs, stats, &wgWorkers)
	}

	// Jalankan producer goroutine, semua berbagi ticker yang sama
	for i := 0; i < cfg.Goroutine; i++ {
		wgProducers.Add(1)
		go producer(ctx, i, rateTicker.C, jobs, &wgProducers)
	}

	// Jalankan progress reporter (cetak tiap 5 detik)
	go progressReporter(ctx, stats, time.Duration(cfg.Duration)*time.Second, startTime)

	// Tunggu context selesai (timeout habis ATAU sinyal interrupt diterima)
	<-ctx.Done()
	fmt.Println("\nMenghentikan pengujian (graceful shutdown)...")

	// Tutup channel jobs setelah producer berhenti supaya worker berhenti bersih
	wgProducers.Wait()
	close(jobs)
	wgWorkers.Wait()

	elapsed := time.Since(startTime)
	printFinalReport(stats, elapsed)
}
