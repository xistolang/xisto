/* In-memory TLS 1.2 handshakes, BearSSL client <-> BearSSL server, timing only the server side.
   Measures server CPU per full handshake without sockets or a load generator in the way.
   Usage (see bench/tls/crypto.sh): handshake [p256|x25519] [N] */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "bearssl.h"
#include "tls_test_keys.h" /* cert_der, key_der: tests/tls cert.pem/key.pem as DER (generated) */

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
#ifdef XS_T0_COUNT
extern unsigned long long xs_t0_count;
#endif

/* move every pending record from one engine to the other; returns bytes moved */
static size_t pump(br_ssl_engine_context *from, br_ssl_engine_context *to, double *srv_time, int from_is_server) {
    size_t moved = 0;
    for (;;) {
        unsigned fs = br_ssl_engine_current_state(from), ts = br_ssl_engine_current_state(to);
        if (!(fs & BR_SSL_SENDREC) || !(ts & BR_SSL_RECVREC)) return moved;
        size_t sl, rl;
        unsigned char *s = br_ssl_engine_sendrec_buf(from, &sl);
        unsigned char *r = br_ssl_engine_recvrec_buf(to, &rl);
        size_t n = sl < rl ? sl : rl;
        memcpy(r, s, n);
        /* only the server engine's calls are timed */
        if (from_is_server) {
            double t0 = now(); br_ssl_engine_sendrec_ack(from, n); *srv_time += now() - t0;
            br_ssl_engine_recvrec_ack(to, n);
        } else {
            br_ssl_engine_sendrec_ack(from, n);
            double t0 = now(); br_ssl_engine_recvrec_ack(to, n); *srv_time += now() - t0;
        }
        moved += n;
    }
}

int main(int argc, char **argv) {
    int force_p256 = argc < 2 || strcmp(argv[1], "x25519") != 0;
    int N = argc > 2 ? atoi(argv[2]) : 2000;
    br_skey_decoder_context kd;
    br_skey_decoder_init(&kd);
    br_skey_decoder_push(&kd, key_der, sizeof key_der);
    const br_ec_private_key *sk = br_skey_decoder_get_ec(&kd);
    br_x509_certificate chain = { (unsigned char *)cert_der, sizeof cert_der };
    /* the client pins the server key (no chain validation: we time the server) */
    br_x509_decoder_context xd; static unsigned char pub[80]; size_t publen = 0;
    br_x509_decoder_init(&xd, NULL, NULL);
    br_x509_decoder_push(&xd, cert_der, sizeof cert_der);
    br_x509_pkey *pk = br_x509_decoder_get_pkey(&xd);
    publen = pk->key.ec.qlen; memcpy(pub, pk->key.ec.q, publen);
    br_ec_public_key epk = { pk->key.ec.curve, pub, publen };

    static br_ssl_server_context sc; static br_ssl_client_context cc; static br_x509_knownkey_context kk; static br_x509_minimal_context xc;
    static unsigned char sbuf[BR_SSL_BUFSIZE_BIDI], cbuf[BR_SSL_BUFSIZE_BIDI];
    double srv = 0; int ok = 0;
    double wall0 = now();
#ifdef XS_T0_COUNT
    xs_t0_count = 0;
#endif
    for (int i = 0; i < N; i++) {
        double t0 = now();
        br_ssl_server_init_full_ec(&sc, &chain, 1, BR_KEYTYPE_EC, sk);
        br_ssl_engine_add_flags(&sc.eng, BR_OPT_ENFORCE_SERVER_PREFERENCES | BR_OPT_NO_RENEGOTIATION);
        br_ssl_engine_set_buffer(&sc.eng, sbuf, sizeof sbuf, 1);
        br_ssl_server_reset(&sc);
        srv += now() - t0;
        br_ssl_client_init_full(&cc, &xc, NULL, 0);
        br_x509_knownkey_init_ec(&kk, &epk, BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN);
        br_ssl_engine_set_x509(&cc.eng, &kk.vtable);
        if (force_p256) br_ssl_engine_set_ec(&cc.eng, &br_ec_p256_m31); /* client offers P-256 only, like oha's choice */
        br_ssl_engine_set_buffer(&cc.eng, cbuf, sizeof cbuf, 1);
        br_ssl_client_reset(&cc, "localhost", 0);
        for (int round = 0; round < 50; round++) {
            size_t a = pump(&cc.eng, &sc.eng, &srv, 0);
            size_t b = pump(&sc.eng, &cc.eng, &srv, 1);
            if (!a && !b) break;
        }
        if ((br_ssl_engine_current_state(&sc.eng) & BR_SSL_SENDAPP) && (br_ssl_engine_current_state(&cc.eng) & BR_SSL_SENDAPP)) ok++;
        else if (i == 0) fprintf(stderr, "handshake failed: server err %d client err %d\n", br_ssl_engine_last_error(&sc.eng), br_ssl_engine_last_error(&cc.eng));
    }
    double wall = now() - wall0;
    printf("%-7s %5d/%d ok  server %6.1f us/handshake  (both sides %6.1f us)", force_p256 ? "p256" : "x25519", ok, N, srv * 1e6 / N, wall * 1e6 / N);
#ifdef XS_T0_COUNT
    printf("  T0 instructions/handshake (server) %llu", xs_t0_count / N);
#endif
    printf("\n");
    return ok == N ? 0 : 1;
}
