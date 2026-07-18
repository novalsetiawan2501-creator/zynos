const fs = require('fs');
const https = require('https');
const { URL } = require('url');

// ==================== KONFIGURASI ====================
const PROXYSCRAPE_BASE = 'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies';
const DATABAY_BASE = 'https://raw.githubusercontent.com/databay-labs/free-proxy-list/master/by-country';
const PROXIFLY_BASE = 'https://raw.githubusercontent.com/proxifly/free-proxy-list/refs/heads/main/proxies';
const THORDATA_HTTP = 'https://raw.githubusercontent.com/Thordata/awesome-free-proxy-list/refs/heads/main/proxies/http.txt';
const THORDATA_HTTPS = 'https://raw.githubusercontent.com/Thordata/awesome-free-proxy-list/refs/heads/main/proxies/https.txt';
const VPSLAB_HTTP = 'https://raw.githubusercontent.com/VPSLabCloud/VPSLab-Free-Proxy-List/main/http_all.txt';

const HEADERS = {
  'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
};

// ==================== COUNTRY LIST ====================
const COUNTRIES = [
  'ae', 'al', 'am', 'ao', 'ar', 'at', 'au', 'az', 'ba', 'bd', 'be', 'bf', 'bg', 'bi', 'bo', 'br', 'bw',
  'by', 'ca', 'cd', 'ch', 'cl', 'cm', 'cn', 'co', 'cr', 'cz', 'de', 'dk', 'do', 'ec', 'ee', 'eg', 'es',
  'fi', 'fr', 'gb', 'ge', 'gh', 'gr', 'gt', 'hk', 'hn', 'hr', 'hu', 'id', 'ie', 'il', 'in', 'iq', 'ir',
  'it', 'jp', 'ke', 'kg', 'kh', 'kr', 'kz', 'lb', 'lt', 'lv', 'ly', 'mx', 'my', 'ng', 'nl', 'no', 'np',
  'om', 'pe', 'ph', 'pk', 'pl', 'pr', 'ps', 'pt', 'py', 'qa', 'ro', 'ru', 'se', 'sg', 'sk', 'sn', 'sy',
  'th', 'tr', 'tw', 'tz', 'ua', 'us', 'uy', 'uz', 've', 'vn', 'za'
];

// ==================== COUNTRY LIST HURUF BESAR UNTUK PROXIFLY ====================
const COUNTRIES_UPPER = [
  'AE', 'AF', 'AL', 'AM', 'AR', 'AT', 'AU', 'AZ', 'BA', 'BB', 'BD', 'BE', 'BF', 'BG', 'BI', 'BO', 'BR', 'BW',
  'BY', 'CA', 'CD', 'CH', 'CL', 'CM', 'CN', 'CO', 'CR', 'CY', 'CZ', 'DE', 'DK', 'DM', 'DO', 'EC', 'EE', 'EG',
  'ES', 'FI', 'FR', 'GB', 'GE', 'GH', 'GR', 'GT', 'HK', 'HN', 'HR', 'HU', 'ID', 'IE', 'IL', 'IN', 'IQ', 'IR',
  'IT', 'JM', 'JP', 'KE', 'KG', 'KH', 'KR', 'KZ', 'LA', 'LB', 'LC', 'LS', 'LT', 'LV', 'ME', 'MU', 'MX', 'MY',
  'NA', 'NG', 'NL', 'NO', 'NP', 'NZ', 'OM', 'PE', 'PG', 'PH', 'PK', 'PL', 'PR', 'PS', 'PT', 'PY', 'QA', 'RO',
  'RS', 'RU', 'RW', 'SA', 'SC', 'SE', 'SG', 'SI', 'SK', 'SN', 'SY', 'TC', 'TG', 'TH', 'TR', 'TT', 'TW', 'TZ',
  'UA', 'US', 'UY', 'UZ', 'VE', 'VG', 'VN', 'ZA', 'ZM', 'ZW', 'ZZ'
];

// ==================== FUNGSI FETCH ====================
function fetchUrl(url) {
  return new Promise((resolve, reject) => {
    const parsedUrl = new URL(url);
    const options = {
      hostname: parsedUrl.hostname,
      path: parsedUrl.pathname + parsedUrl.search,
      headers: HEADERS,
      timeout: 15000,
    };

    const req = https.get(options, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        if (res.statusCode === 200) {
          resolve(data);
        } else {
          reject(new Error(`HTTP ${res.statusCode}`));
        }
      });
    });

    req.on('error', reject);
    req.on('timeout', () => {
      req.destroy();
      reject(new Error('Timeout'));
    });
    req.end();
  });
}

// ==================== FILTER BARIS VALID ====================
function isValidLine(line) {
  const trimmed = line.trim();
  if (!trimmed) return false;
  if (trimmed.startsWith('#')) return false;
  if (trimmed.startsWith('//')) return false;
  if (trimmed.startsWith('/*') || trimmed.endsWith('*/')) return false;
  if (trimmed.startsWith('http://') || trimmed.startsWith('https://')) return true;
  if (/^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d+$/.test(trimmed)) return true;
  return false;
}

// ==================== VALIDASI & EXTRACT ====================
function isValidProxy(line) {
  let cleaned = line.trim();
  if (cleaned.startsWith('http://')) {
    cleaned = cleaned.substring(7);
  } else if (cleaned.startsWith('https://')) {
    cleaned = cleaned.substring(8);
  }
  return /^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d+$/.test(cleaned);
}

function extractProxy(line) {
  let proxy = line.trim();
  if (proxy.startsWith('http://')) {
    proxy = proxy.substring(7);
  } else if (proxy.startsWith('https://')) {
    proxy = proxy.substring(8);
  }
  if (proxy.endsWith('/')) {
    proxy = proxy.slice(0, -1);
  }
  return proxy;
}

// ==================== FILTER HANYA HTTP & HTTPS ====================
function isHttpOrHttps(line) {
  const trimmed = line.trim();
  return trimmed.startsWith('http://') || trimmed.startsWith('https://');
}

// ==================== SCRAPE PROXYSCRAPE ====================
async function scrapeProxyScrape() {
  console.log('[*] Scraping ProxyScrape...');
  const proxies = new Set();
  const protocols = ['http', 'https'];
  
  try {
    console.log('   [+] Ambil countries/{country}/{protocol}/data.txt...');
    let total = 0;
    for (const country of COUNTRIES) {
      for (const protocol of protocols) {
        const url = `${PROXYSCRAPE_BASE}/countries/${country}/${protocol}/data.txt`;
        try {
          const data = await fetchUrl(url);
          const lines = data.split('\n')
            .map(l => l.trim())
            .filter(l => isValidLine(l) && isHttpOrHttps(l));
          
          for (const line of lines) {
            proxies.add(extractProxy(line));
          }
          
          if (lines.length > 0) {
            total += lines.length;
            console.log(`      [+] ${country}/${protocol}: ${lines.length} proxy`);
          }
        } catch (err) {
          // Skip 404
        }
      }
    }
    console.log(`   [+] countries/{country}/{protocol}/data.txt total: ${total} proxy (unique)`);
    
  } catch (err) {
    console.error('[!] ProxyScrape error:', err.message);
  }
  
  console.log(`[+] ProxyScrape total: ${proxies.size} proxy`);
  return proxies;
}

// ==================== SCRAPE DATABAY ====================
async function scrapeDatabay() {
  console.log('[*] Scraping Databay...');
  const proxies = new Set();
  const protocols = ['http'];
  let total = 0;
  
  for (const country of COUNTRIES) {
    for (const protocol of protocols) {
      const url = `${DATABAY_BASE}/${country}/${protocol}.txt`;
      try {
        const data = await fetchUrl(url);
        const lines = data.split('\n')
          .map(l => l.trim())
          .filter(l => isValidLine(l) && isHttpOrHttps(l));
        
        for (const line of lines) {
          proxies.add(extractProxy(line));
        }
        
        if (lines.length > 0) {
          total += lines.length;
          console.log(`   [+] ${country}/${protocol}: ${lines.length} proxy`);
        }
      } catch (err) {
        // Skip 404
      }
    }
  }
  
  console.log(`[+] Databay total: ${proxies.size} proxy`);
  return proxies;
}

// ==================== SCRAPE PROXIFLY ====================
async function scrapeProxifly() {
  console.log('[*] Scraping Proxifly...');
  const proxies = new Set();
  
  try {
    console.log('   [+] Ambil all/data.txt...');
    const allUrl = `${PROXIFLY_BASE}/all/data.txt`;
    const data = await fetchUrl(allUrl);
    const lines = data.split('\n')
      .map(l => l.trim())
      .filter(l => isValidLine(l) && isHttpOrHttps(l));
    
    for (const line of lines) {
      proxies.add(extractProxy(line));
    }
    console.log(`      [+] ALL: ${lines.length} proxy (http/https only)`);
  } catch (err) {
    console.log('      [!] ALL gagal:', err.message);
  }
  
  console.log('   [+] Ambil countries/{country}/data.txt...');
  let total = 0;
  for (const country of COUNTRIES_UPPER) {
    const url = `${PROXIFLY_BASE}/countries/${country}/data.txt`;
    try {
      const data = await fetchUrl(url);
      const lines = data.split('\n')
        .map(l => l.trim())
        .filter(l => isValidLine(l) && isHttpOrHttps(l));
      
      for (const line of lines) {
        proxies.add(extractProxy(line));
      }
      
      if (lines.length > 0) {
        total += lines.length;
        console.log(`      [+] ${country}: ${lines.length} proxy (http/https only)`);
      }
    } catch (err) {
      // Skip 404
    }
  }
  console.log(`   [+] countries/{country}/data.txt total: ${total} proxy (unique)`);
  
  console.log(`[+] Proxifly total: ${proxies.size} proxy (http/https only)`);
  return proxies;
}

// ==================== SCRAPE THORDATA ====================
async function scrapeThordata() {
  console.log('[*] Scraping Thordata...');
  const proxies = new Set();
  
  try {
    console.log('   [+] Ambil http.txt...');
    const data = await fetchUrl(THORDATA_HTTP);
    const lines = data.split('\n')
      .map(l => l.trim())
      .filter(l => isValidLine(l) && isHttpOrHttps(l));
    
    for (const line of lines) {
      proxies.add(extractProxy(line));
    }
    console.log(`      [+] http.txt: ${lines.length} proxy`);
  } catch (err) {
    console.log('      [!] http.txt gagal:', err.message);
  }
  
  try {
    console.log('   [+] Ambil https.txt...');
    const data = await fetchUrl(THORDATA_HTTPS);
    const lines = data.split('\n')
      .map(l => l.trim())
      .filter(l => isValidLine(l) && isHttpOrHttps(l));
    
    for (const line of lines) {
      proxies.add(extractProxy(line));
    }
    console.log(`      [+] https.txt: ${lines.length} proxy`);
  } catch (err) {
    console.log('      [!] https.txt gagal:', err.message);
  }
  
  console.log(`[+] Thordata total: ${proxies.size} proxy`);
  return proxies;
}

// ==================== SCRAPE VPSLAB ====================
async function scrapeVPSLab() {
  console.log('[*] Scraping VPSLab...');
  const proxies = new Set();
  
  try {
    console.log('   [+] Ambil http_all.txt...');
    const data = await fetchUrl(VPSLAB_HTTP);
    const lines = data.split('\n')
      .map(l => l.trim())
      .filter(l => isValidLine(l) && isHttpOrHttps(l));
    
    for (const line of lines) {
      proxies.add(extractProxy(line));
    }
    console.log(`      [+] http_all.txt: ${lines.length} proxy (skip semua jenis komentar)`);
  } catch (err) {
    console.log('      [!] http_all.txt gagal:', err.message);
  }
  
  console.log(`[+] VPSLab total: ${proxies.size} proxy`);
  return proxies;
}

// ==================== TAMBAHAN SOURCE ARRAY (LINK BARU) ====================
const EXTRA_SOURCES = [
  // KangProxy
  { url: 'https://raw.githubusercontent.com/officialputuid/KangProxy/KangProxy/http/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/officialputuid/KangProxy/KangProxy/https/https.txt', protocol: 'https' },
  
  // HProxy
  { url: 'https://raw.githubusercontent.com/hproxy-com/free-proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/hproxy-com/free-proxy-list/main/https.txt', protocol: 'https' },
  
  // Riyoway
  { url: 'https://raw.githubusercontent.com/Riyoway/proxies/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/Riyoway/proxies/master/https.txt', protocol: 'https' },
  
  // Monosans
  { url: 'https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/https.txt', protocol: 'https' },
  
  // TheSpeedX
  { url: 'https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/https.txt', protocol: 'https' },
  
  // VMHeaven
  { url: 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/https.txt', protocol: 'https' },
  
  // Iplocate
  { url: 'https://raw.githubusercontent.com/iplocate/free-proxy-list/main/protocols/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/iplocate/free-proxy-list/main/protocols/https.txt', protocol: 'https' },
  
  // XYZS996
  { url: 'https://raw.githubusercontent.com/xyzs996/free-proxy-health-list/main/proxies/protocols/http/data.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/xyzs996/free-proxy-health-list/main/proxies/protocols/https/data.txt', protocol: 'https' },
  
  // Vakhov
  { url: 'https://raw.githubusercontent.com/vakhov/fresh-proxy-list/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/vakhov/fresh-proxy-list/master/https.txt', protocol: 'https' },
  
  // Roosterkid
  { url: 'https://raw.githubusercontent.com/roosterkid/openproxylist/master/HTTP_RAW.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/roosterkid/openproxylist/master/HTTPS_RAW.txt', protocol: 'https' },
  
  // Argh94
  { url: 'https://raw.githubusercontent.com/Argh94/Proxy-List/main/HTTP.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/Argh94/Proxy-List/main/HTTPS.txt', protocol: 'https' },
  
  // Cyberh4ck3r
  { url: 'https://raw.githubusercontent.com/cyberh4ck3r/free-proxy-list/main/http-proxies.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/cyberh4ck3r/free-proxy-list/main/https-proxies.txt', protocol: 'https' },
  
  // Gproxynet
  { url: 'https://raw.githubusercontent.com/gproxynet/free-proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gproxynet/free-proxy-list/main/https.txt', protocol: 'https' },
  
  // ProxyForEveryone
  { url: 'https://raw.githubusercontent.com/officialputuid/ProxyForEveryone/main/http/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/officialputuid/ProxyForEveryone/main/https/https.txt', protocol: 'https' },
  
  // Dinoz0rg
  { url: 'https://raw.githubusercontent.com/dinoz0rg/proxy-list/main/checked_proxies/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/dinoz0rg/proxy-list/main/checked_proxies/https.txt', protocol: 'https' },
  
  // GFPCOM
  { url: 'https://raw.githubusercontent.com/gfpcom/free-proxy-list/main/sources/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gfpcom/free-proxy-list/main/sources/https.txt', protocol: 'https' },
  
  // gnxD3RfTT2WE
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/http-proxy-list-2026/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/http-proxy-list-2026/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/free-proxy-pool-2026/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/free-proxy-pool-2026/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/Proxy-List-2026/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/Proxy-List-2026/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/free-proxy-2026/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/free-proxy-2026/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/proxy-pool-2026/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/proxy-pool-2026/main/https.txt', protocol: 'https' },
  
  // Lainnya
  { url: 'https://raw.githubusercontent.com/clarketm/proxy-list/master/proxy-list-raw.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/mmpx12/proxy-list/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/mmpx12/proxy-list/master/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/proxy4pars/proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/proxy4pars/proxy-list/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/zloi-user/hideip.me/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/zloi-user/hideip.me/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/Anonym0usWork1221/Free-Proxies/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/Anonym0usWork1221/Free-Proxies/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/prxchk/proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/prxchk/proxy-list/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/spysone/proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/spysone/proxy-list/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/zDayOld/Proxy-List/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/zDayOld/Proxy-List/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/alex13822/proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/alex13822/proxy-list/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/kimyew/proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/kimyew/proxy-list/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/sunrise-sea/proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/sunrise-sea/proxy-list/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/proxylist/proxylist.github.io/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/proxylist/proxylist.github.io/master/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/marcelo-diaz/proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/marcelo-diaz/proxy-list/main/https.txt', protocol: 'https' },
];

// ==================== SCRAPE EXTRA SOURCES ====================
async function scrapeExtraSources() {
  console.log('[*] Scraping Extra Sources...');
  const proxies = new Set();
  
  for (const source of EXTRA_SOURCES) {
    try {
      const data = await fetchUrl(source.url);
      const lines = data.split('\n')
        .map(l => l.trim())
        .filter(l => isValidLine(l) && isHttpOrHttps(l));
      
      for (const line of lines) {
        proxies.add(extractProxy(line));
      }
      
      if (lines.length > 0) {
        console.log(`   [+] ${source.protocol.toUpperCase()}: ${lines.length} proxy`);
      }
    } catch (err) {
      // Skip error
    }
  }
  
  console.log(`[+] Extra Sources total: ${proxies.size} proxy`);
  return proxies;
}

// ==================== MAIN ====================
async function main() {
  console.log('🚀 Mulai scraping proxy...\n');
  console.log(`[+] Total negara: ${COUNTRIES.length}\n`);
  console.log('[!] Hanya mengambil proxy HTTP/HTTPS dan di-convert ke ip:port');
  console.log('[!] Socks4/Socks5, komentar #, //, /* */, dan baris kosong otomatis di-skip\n');
  
  const proxyScrapeProxies = await scrapeProxyScrape();
  const databayProxies = await scrapeDatabay();
  const proxiflyProxies = await scrapeProxifly();
  const thordataProxies = await scrapeThordata();
  const vpslabProxies = await scrapeVPSLab();
  const extraProxies = await scrapeExtraSources();
  
  const allProxies = new Set([
    ...proxyScrapeProxies, 
    ...databayProxies, 
    ...proxiflyProxies, 
    ...thordataProxies, 
    ...vpslabProxies,
    ...extraProxies
  ]);
  
  fs.writeFileSync('proxy.txt', Array.from(allProxies).join('\n'));
  
  console.log(`\n${'='.repeat(50)}`);
  console.log(`🔥 TOTAL PROXY (HTTP/HTTPS ONLY): ${allProxies.size}`);
  console.log(`   ProxyScrape: ${proxyScrapeProxies.size}`);
  console.log(`   Databay:     ${databayProxies.size}`);
  console.log(`   Proxifly:    ${proxiflyProxies.size}`);
  console.log(`   Thordata:    ${thordataProxies.size}`);
  console.log(`   VPSLab:      ${vpslabProxies.size}`);
  console.log(`   Extra:       ${extraProxies.size}`);
  console.log(`   Gabungan:    ${allProxies.size}`);
  console.log(`\n📁 Disimpan ke: proxy.txt`);
  console.log(`${'='.repeat(50)}`);
}

main().catch(console.error);