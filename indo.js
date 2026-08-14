// id.js - Proxy scraper untuk proxy Indonesia dari berbagai sumber
// Output disimpan ke file proxy.txt dengan format IP:PORT

const fs = require('fs');

(async () => {
  try {
    // Daftar URL sumber proxy Indonesia
    const urls = [
      'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies/countries/id/data.txt',
      'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies/countries/id/socks5/data.txt',
      'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies/countries/id/socks4/data.txt',
      'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies/countries/id/https/data.txt',
      'https://raw.githubusercontent.com/ProxyScrape/free-proxy-list/refs/heads/main/proxies/countries/id/http/data.txt',
      'https://raw.githubusercontent.com/databay-labs/free-proxy-list/refs/heads/master/by-country/id/socks5.txt',
      'https://raw.githubusercontent.com/databay-labs/free-proxy-list/refs/heads/master/by-country/id/socks4.txt',
      'https://raw.githubusercontent.com/databay-labs/free-proxy-list/refs/heads/master/by-country/id/http.txt'
    ];

    console.log('🔍 Memulai scraping proxy Indonesia...');

    // Mengambil data dari semua URL secara paralel
    const fetchPromises = urls.map(async (url) => {
      try {
        const response = await fetch(url);
        if (!response.ok) {
          console.warn(`⚠️ Gagal mengambil data dari ${url}: ${response.status}`);
          return [];
        }
        const text = await response.text();
        // Filter baris kosong dan komentar
        const proxies = text
          .split('\n')
          .map(line => line.trim())
          .filter(line => line && !line.startsWith('#'));
        return proxies;
      } catch (error) {
        console.warn(`⚠️ Error saat mengambil ${url}: ${error.message}`);
        return [];
      }
    });

    // Tunggu semua fetch selesai
    const results = await Promise.all(fetchPromises);
    
    // Gabungkan semua proxy dari semua sumber
    const allProxies = results.flat();

    if (allProxies.length === 0) {
      console.log('❌ Tidak ada proxy ditemukan.');
      return;
    }

    // Hapus duplikat dan ekstrak IP:PORT dari format protokol://IP:PORT
    const uniqueProxies = [...new Set(allProxies)];
    
    // Ekstrak IP:PORT dari berbagai format
    const ipPortList = uniqueProxies
      .map(proxy => {
        // Hilangkan protokol (socks4://, socks5://, http://, https://)
        const cleanProxy = proxy.replace(/^(socks4|socks5|http|https):\/\//, '');
        return cleanProxy;
      })
      .filter(ipPort => ipPort.includes(':')); // Pastikan ada port

    // Hapus duplikat lagi setelah pembersihan
    const finalProxyList = [...new Set(ipPortList)];

    console.log(`✅ Berhasil mengambil ${finalProxyList.length} proxy unik.`);

    // Simpan ke file proxy.txt
    const outputPath = 'proxy.txt';
    fs.writeFileSync(outputPath, finalProxyList.join('\n'), 'utf8');
    console.log(`💾 Proxy berhasil disimpan ke ${outputPath}`);

    // Tampilkan 5 contoh pertama
    console.log('\n📌 Contoh proxy (5 pertama):');
    finalProxyList.slice(0, 5).forEach((proxy, i) => {
      console.log(`  ${i+1}. ${proxy}`);
    });

    // Kembalikan hasil
    return finalProxyList;

  } catch (error) {
    console.error('❌ Error utama:', error.message);
    throw error;
  }
})();