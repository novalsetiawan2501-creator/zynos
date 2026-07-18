const fs = require('fs');
const https = require('https');
const { URL } = require('url');

// ==================== KONFIGURASI ====================
const PROXYSCRAPE_BASE = 'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies';
const DATABAY_BASE = 'https://raw.githubusercontent.com/databay-labs/free-proxy-list/master/by-country';
const PROXIFLY_BASE = 'https://raw.githubusercontent.com/proxifly/free-proxy-list/refs/heads/main/proxies';

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
  if (proxy.includes('://')) {
    proxy = proxy.split('://')[1];
  }
  return proxy;
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

// ==================== MAIN ====================
async function main() {
  console.log('🚀 Mulai scraping proxy...\n');
  console.log(`[+] Total negara: ${COUNTRIES.length}\n`);
  
  // Jalankan sequential biar keliatan jelas
  const proxyScrapeProxies = await scrapeProxyScrape();
  const databayProxies = await scrapeDatabay();
  const proxiflyProxies = await scrapeProxifly();
  
  const allProxies = new Set([...proxyScrapeProxies, ...databayProxies, ...proxiflyProxies]);
  
  fs.writeFileSync('proxy.txt', Array.from(allProxies).join('\n'));
  
  console.log(`\n${'='.repeat(50)}`);
  console.log(`🔥 TOTAL PROXY: ${allProxies.size}`);
  console.log(`   ProxyScrape: ${proxyScrapeProxies.size}`);
  console.log(`   Databay:     ${databayProxies.size}`);
  console.log(`   Proxifly:    ${proxiflyProxies.size}`);
  console.log(`   Gabungan:    ${allProxies.size}`);
  console.log(`\n📁 Disimpan ke: proxy.txt`);
  console.log(`${'='.repeat(50)}`);
}

main().catch(console.error);