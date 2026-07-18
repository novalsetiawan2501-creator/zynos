const fs = require('fs');

const output_file = 'proxy.txt';
const FETCH_TIMEOUT = 10000;
const CONCURRENCY = 15;

const proxySet = new Set();
const stats = { http: 0, https: 0, socks4: 0, socks5: 0 };

const sources = [
  // proxyscrape/free-proxy-list — updated every 5 min
  { url: 'https://raw.githubusercontent.com/proxyscrape/free-proxy-list/main/proxies/protocols/http/data.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/proxyscrape/free-proxy-list/main/proxies/protocols/https/data.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/proxyscrape/free-proxy-list/main/proxies/protocols/socks4/data.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/proxyscrape/free-proxy-list/main/proxies/protocols/socks5/data.txt', protocol: 'socks5' },

  // proxifly/free-proxy-list — updated every 5 min, 2568 proxies
  { url: 'https://raw.githubusercontent.com/proxifly/free-proxy-list/main/proxies/protocols/http/data.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/proxifly/free-proxy-list/main/proxies/protocols/https/data.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/proxifly/free-proxy-list/main/proxies/protocols/socks4/data.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/proxifly/free-proxy-list/main/proxies/protocols/socks5/data.txt', protocol: 'socks5' },

  // Thordata/awesome-free-proxy-list — daily updated, auto-verified
  { url: 'https://raw.githubusercontent.com/Thordata/awesome-free-proxy-list/main/proxies/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/Thordata/awesome-free-proxy-list/main/proxies/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/Thordata/awesome-free-proxy-list/main/proxies/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/Thordata/awesome-free-proxy-list/main/proxies/socks5.txt', protocol: 'socks5' },

  // officialputuid/KangProxy — daily updated, 230 stars
  { url: 'https://raw.githubusercontent.com/officialputuid/KangProxy/KangProxy/http/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/officialputuid/KangProxy/KangProxy/https/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/officialputuid/KangProxy/KangProxy/socks4/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/officialputuid/KangProxy/KangProxy/socks5/socks5.txt', protocol: 'socks5' },

  // databay-labs/free-proxy-list — updated every 5 min, strict SSL
  { url: 'https://raw.githubusercontent.com/databay-labs/free-proxy-list/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/databay-labs/free-proxy-list/master/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/databay-labs/free-proxy-list/master/socks5.txt', protocol: 'socks5' },

  // VPSLabCloud/VPSLab-Free-Proxy-List — updated every 15 min
  { url: 'https://raw.githubusercontent.com/VPSLabCloud/VPSLab-Free-Proxy-List/main/http_all.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/VPSLabCloud/VPSLab-Free-Proxy-List/main/socks4_all.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/VPSLabCloud/VPSLab-Free-Proxy-List/main/socks5_all.txt', protocol: 'socks5' },

  // hproxy-com/free-proxy-list — 19k+ live proxies
  { url: 'https://raw.githubusercontent.com/hproxy-com/free-proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/hproxy-com/free-proxy-list/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/hproxy-com/free-proxy-list/main/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/hproxy-com/free-proxy-list/main/socks5.txt', protocol: 'socks5' },

  // Riyoway/proxies — daily updated
  { url: 'https://raw.githubusercontent.com/Riyoway/proxies/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/Riyoway/proxies/master/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/Riyoway/proxies/master/socks5.txt', protocol: 'socks5' },

  // monosans/proxy-list — hourly, still active
  { url: 'https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/socks5.txt', protocol: 'socks5' },

  // TheSpeedX/PROXY-List — large lists, hourly
  { url: 'https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks5.txt', protocol: 'socks5' },

  // ProxyScrape API v4 — updated every minute
  { url: 'https://api.proxyscrape.com/v4/free-proxy-list/get?request=display_proxies&protocol=http&format=text', protocol: 'http' },
  { url: 'https://api.proxyscrape.com/v4/free-proxy-list/get?request=display_proxies&protocol=socks4&format=text', protocol: 'socks4' },
  { url: 'https://api.proxyscrape.com/v4/free-proxy-list/get?request=display_proxies&protocol=socks5&format=text', protocol: 'socks5' },

  // Databay API — updated every 5 min, strict SSL
  { url: 'https://databay.com/api/v1/proxy-list?format=txt&protocol=http&limit=1000', protocol: 'http' },
  { url: 'https://databay.com/api/v1/proxy-list?format=txt&protocol=socks5&limit=1000', protocol: 'socks5' },

  // vmheaven/VMHeaven.io-Free-Proxy-List — updated every 15 min, 43 stars
  { url: 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/vmheaven/VMHeaven.io-Free-Proxy-List/main/socks5.txt', protocol: 'socks5' },

  // iplocate/free-proxy-list — updated every 30 min, 169 stars
  { url: 'https://raw.githubusercontent.com/iplocate/free-proxy-list/main/protocols/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/iplocate/free-proxy-list/main/protocols/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/iplocate/free-proxy-list/main/protocols/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/iplocate/free-proxy-list/main/protocols/socks5.txt', protocol: 'socks5' },

  // xyzs996/free-proxy-health-list — verified health list, auto-updated, 15 stars
  { url: 'https://raw.githubusercontent.com/xyzs996/free-proxy-health-list/main/proxies/protocols/http/data.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/xyzs996/free-proxy-health-list/main/proxies/protocols/socks4/data.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/xyzs996/free-proxy-health-list/main/proxies/protocols/socks5/data.txt', protocol: 'socks5' },

  // vakhov/fresh-proxy-list — 362 stars, fresh working proxies
  { url: 'https://raw.githubusercontent.com/vakhov/fresh-proxy-list/master/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/vakhov/fresh-proxy-list/master/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/vakhov/fresh-proxy-list/master/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/vakhov/fresh-proxy-list/master/socks5.txt', protocol: 'socks5' },

  // roosterkid/openproxylist — 880 stars, hourly updates
  { url: 'https://raw.githubusercontent.com/roosterkid/openproxylist/master/HTTPS_RAW.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/roosterkid/openproxylist/master/SOCKS4_RAW.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/roosterkid/openproxylist/master/SOCKS5_RAW.txt', protocol: 'socks5' },

  // Argh94/Proxy-List — 225 stars, refreshed every hour
  { url: 'https://raw.githubusercontent.com/Argh94/Proxy-List/main/HTTP.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/Argh94/Proxy-List/main/HTTPS.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/Argh94/Proxy-List/main/SOCKS4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/Argh94/Proxy-List/main/SOCKS5.txt', protocol: 'socks5' },

  // cyberh4ck3r/free-proxy-list — auto-updated from multiple sources
  { url: 'https://raw.githubusercontent.com/cyberh4ck3r/free-proxy-list/main/http-proxies.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/cyberh4ck3r/free-proxy-list/main/https-proxies.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/cyberh4ck3r/free-proxy-list/main/socks4-proxies.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/cyberh4ck3r/free-proxy-list/main/socks5-proxies.txt', protocol: 'socks5' },

  // gproxynet/free-proxy-list — checked hourly
  { url: 'https://raw.githubusercontent.com/gproxynet/free-proxy-list/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gproxynet/free-proxy-list/main/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/gproxynet/free-proxy-list/main/socks5.txt', protocol: 'socks5' },

  // officialputuid/ProxyForEveryone — regularly updated, 7 stars
  { url: 'https://raw.githubusercontent.com/officialputuid/ProxyForEveryone/main/http/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/officialputuid/ProxyForEveryone/main/https/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/officialputuid/ProxyForEveryone/main/socks4/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/officialputuid/ProxyForEveryone/main/socks5/socks5.txt', protocol: 'socks5' },

  // dinoz0rg/proxy-list — scraped and verified, 22 stars
  { url: 'https://raw.githubusercontent.com/dinoz0rg/proxy-list/main/checked_proxies/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/dinoz0rg/proxy-list/main/checked_proxies/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/dinoz0rg/proxy-list/main/checked_proxies/socks5.txt', protocol: 'socks5' },

  // elliottophellia/proxylist — 207 stars, checked proxy list
  { url: 'https://raw.githubusercontent.com/elliottophellia/proxylist/main/results/http/global/http_checked.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/elliottophellia/proxylist/main/results/http/global/phttp_checked.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/elliottophellia/proxylist/main/results/socks4/global/socks4_checked.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/elliottophellia/proxylist/main/results/socks4/global/psocks4_checked.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/elliottophellia/proxylist/main/results/socks5/global/socks5_checked.txt', protocol: 'socks5' },
  { url: 'https://raw.githubusercontent.com/elliottophellia/proxylist/main/results/socks5/global/psocks5_checked.txt', protocol: 'socks5' },

  // gfpcom/free-proxy-list — 362 stars, updated every 30 min
  { url: 'https://raw.githubusercontent.com/gfpcom/free-proxy-list/main/sources/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gfpcom/free-proxy-list/main/sources/https.txt', protocol: 'https' },
  { url: 'https://raw.githubusercontent.com/gfpcom/free-proxy-list/main/sources/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/gfpcom/free-proxy-list/main/sources/socks5.txt', protocol: 'socks5' },

  // gnxD3RfTT2WE/http-proxy-list-2026 — updated daily, July 2026
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/http-proxy-list-2026/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/http-proxy-list-2026/main/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/http-proxy-list-2026/main/socks5.txt', protocol: 'socks5' },

  // gnxD3RfTT2WE/free-proxy-pool-2026 — updated daily, July 2026
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/free-proxy-pool-2026/main/http.txt', protocol: 'http' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/free-proxy-pool-2026/main/socks4.txt', protocol: 'socks4' },
  { url: 'https://raw.githubusercontent.com/gnxD3RfTT2WE/free-proxy-pool-2026/main/socks5.txt', protocol: 'socks5' },
];

const ipPortRegex = /^(\d{1,3}\.){3}\d{1,3}:\d{2,5}$/;

function isValidProxy(line) {
  const trimmed = line.trim();
  if (!trimmed || !ipPortRegex.test(trimmed)) return null;
  const [ip, port] = trimmed.split(':');
  const portNum = parseInt(port, 10);
  if (portNum < 1 || portNum > 65535) return null;
  const octets = ip.split('.');
  if (octets.some(o => parseInt(o, 10) > 255)) return null;
  return trimmed;
}

async function fetchWithTimeout(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT);
  try {
    const res = await fetch(url, { signal: controller.signal });
    if (!res.ok) return null;
    return await res.text();
  } catch {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

async function fetchSource(source) {
  const data = await fetchWithTimeout(source.url);
  if (!data) return 0;
  let count = 0;
  for (const line of data.split('\n')) {
    const proxy = isValidProxy(line);
    if (proxy) {
      const key = `${source.protocol}://${proxy}`;
      if (!proxySet.has(key)) {
        proxySet.add(key);
        stats[source.protocol]++;
        count++;
      }
    }
  }
  return count;
}

async function fetchProxies() {
  console.log(`Scraping dari ${sources.length} source...`);
  let done = 0;
  for (let i = 0; i < sources.length; i += CONCURRENCY) {
    const batch = sources.slice(i, i + CONCURRENCY);
    const results = await Promise.all(batch.map(s => fetchSource(s)));
    done += batch.length;
    const found = results.reduce((a, b) => a + b, 0);
    console.log(`[${done}/${sources.length}] +${found} proxy baru`);
  }

  const all = [...proxySet];
  fs.writeFileSync(output_file, all.join('\n'));

  console.log('\n=== HASIL ===');
  console.log(`Total: ${all.length} proxy`);
  console.log(`HTTP:   ${stats.http}`);
  console.log(`HTTPS:  ${stats.https}`);
  console.log(`SOCKS4: ${stats.socks4}`);
  console.log(`SOCKS5: ${stats.socks5}`);
  console.log(`\nDisimpan ke: ${output_file}`);
  console.log('Format: protocol://ip:port');
}

fetchProxies();
