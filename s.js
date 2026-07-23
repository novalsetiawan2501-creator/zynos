const fs = require('fs');
const https = require('https');
const { URL } = require('url');

// ==================== KONFIGURASI ====================
const PROXYSCRAPE_BASE = 'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies';
const DATABAY_BASE = 'https://raw.githubusercontent.com/databay-labs/free-proxy-list/master/by-country';
const PROXIFLY_BASE = 'https://raw.githubusercontent.com/proxifly/free-proxy-list/refs/heads/main/proxies';
const MONOSANS_ALL = 'https://raw.githubusercontent.com/monosans/proxy-list/refs/heads/main/proxies/all.txt';
const MONOSANS_HTTP = 'https://raw.githubusercontent.com/monosans/proxy-list/refs/heads/main/proxies/http.txt';
const PROXYSCRAPE_API = 'https://api.proxyscrape.com/v4/free-proxy-list/get?request=display_proxies&protocol=http&format=text';
const DATABAY_API = 'https://databay.com/api/v1/proxy-list?format=txt&protocol=http&limit=1000';
const VMHEAVEN_HTTPS = 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/https.txt';
const VMHEAVEN_HTTP = 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/http.txt';

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

function isValidProxy(line) {
  return /^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d+$/.test(line.trim());
}

function extractProxy(line) {
  let proxy = line.trim();
  // Hapus protokol kalo ada (http://, https://, socks4://, socks5://)
  if (proxy.includes('://')) {
    proxy = proxy.split('://')[1];
  }
  return proxy;
}

// ==================== FITUR HAPUS KOMENTAR ====================
function removeComments(line) {
  // Hapus komentar yang dimulai dengan # atau //
  let cleaned = line.trim();
  
  // Cek komentar #
  if (cleaned.includes('#')) {
    cleaned = cleaned.split('#')[0].trim();
  }
  
  // Cek komentar //
  if (cleaned.includes('//')) {
    cleaned = cleaned.split('//')[0].trim();
  }
  
  // Cek komentar /* */
  if (cleaned.includes('/*')) {
    cleaned = cleaned.split('/*')[0].trim();
  }
  
  return cleaned;
}

function isCommentLine(line) {
  const trimmed = line.trim();
  // Cek apakah line kosong atau hanya komentar
  if (trimmed === '') return true;
  if (trimmed.startsWith('#')) return true;
  if (trimmed.startsWith('//')) return true;
  if (trimmed.startsWith('/*')) return true;
  return false;
}

// ==================== SCRAPE PROXYSCRAPE ====================
async function scrapeProxyScrape() {
  console.log('[*] Scraping ProxyScrape...');
  const proxies = new Set();
  const protocols = ['http', 'https'];
  
  try {
    console.log('   [+] Ambil all/data.txt...');
    const allData = await fetchUrl(`${PROXYSCRAPE_BASE}/all/data.txt`);
    const allLines = allData.split('\n')
      .map(l => l.trim())
      .filter(l => l && l.includes('://'));
    
    for (const line of allLines) {
      proxies.add(extractProxy(line));
    }
    console.log(`   [+] all/data.txt: ${allLines.length} proxy`);
    
    console.log('   [+] Ambil countries/{country}/{protocol}/data.txt...');
    let total = 0;
    for (const country of COUNTRIES) {
      for (const protocol of protocols) {
        const url = `${PROXYSCRAPE_BASE}/countries/${country}/${protocol}/data.txt`;
        try {
          const data = await fetchUrl(url);
          const lines = data.split('\n')
            .map(l => l.trim())
            .filter(l => l && l.includes('://'));
          
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
          .filter(l => l && isValidProxy(l));
        
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
  
  // Ambil ALL dulu
  try {
    console.log('   [+] Ambil all/data.txt...');
    const allUrl = `${PROXIFLY_BASE}/all/data.txt`;
    const data = await fetchUrl(allUrl);
    const lines = data.split('\n')
      .map(l => l.trim())
      .filter(l => l && l.includes(':'));
    
    for (const line of lines) {
      proxies.add(extractProxy(line));
    }
    console.log(`      [+] ALL: ${lines.length} proxy`);
  } catch (err) {
    console.log('      [!] ALL gagal:', err.message);
  }
  
  // Ambil per country pake huruf besar
  console.log('   [+] Ambil countries/{country}/data.txt...');
  let total = 0;
  for (const country of COUNTRIES_UPPER) {
    const url = `${PROXIFLY_BASE}/countries/${country}/data.txt`;
    try {
      const data = await fetchUrl(url);
      const lines = data.split('\n')
        .map(l => l.trim())
        .filter(l => l && l.includes(':'));
      
      for (const line of lines) {
        proxies.add(extractProxy(line));
      }
      
      if (lines.length > 0) {
        total += lines.length;
        console.log(`      [+] ${country}: ${lines.length} proxy`);
      }
    } catch (err) {
      // Skip 404
    }
  }
  console.log(`   [+] countries/{country}/data.txt total: ${total} proxy (unique)`);
  
  console.log(`[+] Proxifly total: ${proxies.size} proxy`);
  return proxies;
}

// ==================== SCRAPE MONOSANS ====================
async function scrapeMonosans() {
  console.log('[*] Scraping Monosans...');
  const proxies = new Set();
  
  // Ambil all.txt
  try {
    console.log('   [+] Ambil all.txt...');
    const allData = await fetchUrl(MONOSANS_ALL);
    const allLines = allData.split('\n')
      .map(l => l.trim())
      .filter(l => l && l.length > 0);
    
    let httpCount = 0;
    let socksFiltered = 0;
    
    for (const line of allLines) {
      // Cek apakah ini proxy HTTP/HTTPS (bukan socks)
      if (line.toLowerCase().includes('socks4') || line.toLowerCase().includes('socks5')) {
        socksFiltered++;
        continue; // Skip socks4/socks5
      }
      
      // Extract IP:PORT dari line
      const proxy = extractProxy(line);
      if (isValidProxy(proxy)) {
        proxies.add(proxy);
        httpCount++;
      }
    }
    
    console.log(`      [+] HTTP/HTTPS proxy: ${httpCount}`);
    console.log(`      [+] Socks4/Socks5 di-skip: ${socksFiltered}`);
    
  } catch (err) {
    console.log('   [!] all.txt gagal:', err.message);
  }
  
  // Ambil http.txt
  try {
    console.log('   [+] Ambil http.txt...');
    const httpData = await fetchUrl(MONOSANS_HTTP);
    const httpLines = httpData.split('\n')
      .map(l => l.trim())
      .filter(l => l && isValidProxy(l));
    
    for (const line of httpLines) {
      proxies.add(extractProxy(line));
    }
    
    console.log(`      [+] http.txt: ${httpLines.length} proxy`);
    
  } catch (err) {
    console.log('   [!] http.txt gagal:', err.message);
  }
  
  console.log(`[+] Monosans total: ${proxies.size} proxy (HTTP/HTTPS only)`);
  return proxies;
}

// ==================== SCRAPE PROXYSCRAPE API ====================
async function scrapeProxyScrapeAPI() {
  console.log('[*] Scraping ProxyScrape API...');
  const proxies = new Set();
  
  try {
    const data = await fetchUrl(PROXYSCRAPE_API);
    const lines = data.split('\n')
      .map(l => removeComments(l))
      .filter(l => l && isValidProxy(l));
    
    for (const line of lines) {
      proxies.add(extractProxy(line));
    }
    
    console.log(`[+] ProxyScrape API total: ${proxies.size} proxy`);
  } catch (err) {
    console.log('[!] ProxyScrape API gagal:', err.message);
  }
  
  return proxies;
}

// ==================== SCRAPE DATABAY API ====================
async function scrapeDatabayAPI() {
  console.log('[*] Scraping Databay API...');
  const proxies = new Set();
  
  try {
    const data = await fetchUrl(DATABAY_API);
    const lines = data.split('\n')
      .map(l => removeComments(l))
      .filter(l => l && !isCommentLine(l) && isValidProxy(l));
    
    for (const line of lines) {
      proxies.add(extractProxy(line));
    }
    
    console.log(`[+] Databay API total: ${proxies.size} proxy`);
  } catch (err) {
    console.log('[!] Databay API gagal:', err.message);
  }
  
  return proxies;
}

// ==================== SCRAPE VMHEAVEN ====================
async function scrapeVMHeaven() {
  console.log('[*] Scraping VMHeaven...');
  const proxies = new Set();
  
  // Ambil https.txt
  try {
    console.log('   [+] Ambil https.txt...');
    const httpsData = await fetchUrl(VMHEAVEN_HTTPS);
    const httpsLines = httpsData.split('\n')
      .map(l => l.trim())
      .filter(l => l && isValidProxy(l));
    
    for (const line of httpsLines) {
      proxies.add(extractProxy(line));
    }
    
    console.log(`      [+] https.txt: ${httpsLines.length} proxy`);
    
  } catch (err) {
    console.log('   [!] https.txt gagal:', err.message);
  }
  
  // Ambil http.txt
  try {
    console.log('   [+] Ambil http.txt...');
    const httpData = await fetchUrl(VMHEAVEN_HTTP);
    const httpLines = httpData.split('\n')
      .map(l => l.trim())
      .filter(l => l && isValidProxy(l));
    
    for (const line of httpLines) {
      proxies.add(extractProxy(line));
    }
    
    console.log(`      [+] http.txt: ${httpLines.length} proxy`);
    
  } catch (err) {
    console.log('   [!] http.txt gagal:', err.message);
  }
  
  console.log(`[+] VMHeaven total: ${proxies.size} proxy`);
  return proxies;
}

// ==================== FITUR AUTO HAPUS DUPLIKAT & VALIDASI ====================
function filterAndSortProxies(proxySet) {
  console.log('\n[*] Sedang menghapus duplikat & memfilter proxy valid...');
  
  // Konversi Set ke Array
  const proxyArray = Array.from(proxySet);
  const initialCount = proxyArray.length;
  
  // Filter proxy yang valid (IP:PORT)
  const validProxies = proxyArray.filter(proxy => {
    // Cek format IP:PORT
    const parts = proxy.split(':');
    if (parts.length !== 2) return false;
    
    // Cek IP
    const ip = parts[0].split('.');
    if (ip.length !== 4) return false;
    
    for (let seg of ip) {
      if (seg === '' || isNaN(seg) || seg < 0 || seg > 255) return false;
    }
    
    // Cek PORT
    const port = parseInt(parts[1]);
    if (isNaN(port) || port < 1 || port > 65535) return false;
    
    return true;
  });
  
  // Hapus duplikat pake Set lagi (jaga-jaga)
  const uniqueSet = new Set(validProxies);
  const uniqueArray = Array.from(uniqueSet);
  
  // Sorting biar rapi (urutin berdasarkan IP & port)
  uniqueArray.sort((a, b) => {
    const ipA = a.split(':')[0].split('.').map(Number);
    const ipB = b.split(':')[0].split('.').map(Number);
    
    for (let i = 0; i < 4; i++) {
      if (ipA[i] !== ipB[i]) return ipA[i] - ipB[i];
    }
    return parseInt(a.split(':')[1]) - parseInt(b.split(':')[1]);
  });
  
  const duplicateCount = initialCount - validProxies.length;
  const invalidCount = validProxies.length - uniqueArray.length;
  
  console.log(`   [+] Total proxy sebelum filter: ${initialCount}`);
  console.log(`   [+] Proxy valid: ${validProxies.length}`);
  console.log(`   [+] Proxy unik setelah filter: ${uniqueArray.length}`);
  console.log(`   [+] Duplikat terhapus: ${duplicateCount + invalidCount}`);
  console.log(`   [+] Proxy invalid terhapus: ${validProxies.length - uniqueArray.length}`);
  
  return uniqueArray;
}

// ==================== MAIN ====================
async function main() {
  console.log('🚀 Mulai scraping proxy...\n');
  console.log(`[+] Total negara: ${COUNTRIES.length}\n`);
  
  // Jalankan sequential biar keliatan jelas
  const proxyScrapeProxies = await scrapeProxyScrape();
  const databayProxies = await scrapeDatabay();
  const proxiflyProxies = await scrapeProxifly();
  const monosansProxies = await scrapeMonosans();
  const proxyScrapeAPIProxies = await scrapeProxyScrapeAPI();
  const databayAPIProxies = await scrapeDatabayAPI();
  const vmHeavenProxies = await scrapeVMHeaven();
  
  // Gabung semua proxy
  const allProxies = new Set([
    ...proxyScrapeProxies,
    ...databayProxies,
    ...proxiflyProxies,
    ...monosansProxies,
    ...proxyScrapeAPIProxies,
    ...databayAPIProxies,
    ...vmHeavenProxies
  ]);
  
  // ========== FITUR AUTO HAPUS DUPLIKAT & VALIDASI ==========
  const finalProxies = filterAndSortProxies(allProxies);
  
  // Simpan ke file
  fs.writeFileSync('proxy.txt', finalProxies.join('\n'));
  
  console.log(`\n${'='.repeat(50)}`);
  console.log(`🔥 TOTAL PROXY UNIK & VALID: ${finalProxies.length}`);
  console.log(`   ProxyScrape:     ${proxyScrapeProxies.size}`);
  console.log(`   Databay:         ${databayProxies.size}`);
  console.log(`   Proxifly:        ${proxiflyProxies.size}`);
  console.log(`   Monosans:        ${monosansProxies.size}`);
  console.log(`   ProxyScrape API: ${proxyScrapeAPIProxies.size}`);
  console.log(`   Databay API:     ${databayAPIProxies.size}`);
  console.log(`   VMHeaven:        ${vmHeavenProxies.size}`);
  console.log(`   Gabungan:        ${allProxies.size}`);
  console.log(`   Hasil akhir:     ${finalProxies.length}`);
  console.log(`\n📁 Disimpan ke: proxy.txt`);
  console.log(`${'='.repeat(50)}`);
}

main().catch(console.error);