/*
 * receiver.c - Server ("Receiver") program
 *
 * High level view:
 *   1. Create a socket
 *   2. Bind to an address/port
 *   3. Listen / accept a connection
 *   4. Read/write data  (receive frames -> recompute checksum/CRC -> accept/reject)
 *   5. Shutdown connection
 *
 * Detection logic uses ONLY the recomputed checksum/CRC over the received
 * (possibly corrupted) payload vs. the redundancy field carried in the
 * frame header - exactly what a real receiver would do. The frame's
 * error_type is ground truth from the sender, logged purely so you can
 * build the evaluation tables the assignment asks for.
 *
 * Usage:
 *   ./receiver <port> [logfile.csv]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [logfile.csv]\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    const char *logfile = (argc >= 3) ? argv[2] : "results.csv";

    FILE *log = fopen(logfile, "a");
    if (!log) { perror("fopen logfile"); return 1; }
    /* write header if file is empty */
    fseek(log, 0, SEEK_END);
    if (ftell(log) == 0) {
        fprintf(log, "frame_id,scheme,crc_type,error_type,payload_len,"
                     "sent_redundancy,recomputed_redundancy,detected,decode_time_ms\n");
    }

    /* Step 1: create a socket */
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Step 2: bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    /* Step 3: listen / accept */
    if (listen(listen_sock, 1) < 0) { perror("listen"); return 1; }
    printf("[receiver] listening on port %d ...\n", port);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
    if (sock < 0) { perror("accept"); return 1; }
    printf("[receiver] client connected: %s\n", inet_ntoa(client_addr.sin_addr));

    long total = 0, accepted = 0, rejected = 0;

    /* Step 4: read/write data */
    while (1) {
        uint8_t hdr_buf[HEADER_SIZE];
        int r = recv_all(sock, hdr_buf, HEADER_SIZE);
        if (r == 0) { printf("[receiver] connection closed by sender\n"); break; }
        if (r < 0) { perror("recv header"); break; }

        FrameHeader hdr;
        header_deserialize(&hdr, hdr_buf);

        if (hdr.payload_len > MAX_PAYLOAD) {
            fprintf(stderr, "[receiver] bad payload_len %u, aborting\n", hdr.payload_len);
            break;
        }

        uint8_t payload[MAX_PAYLOAD];
        r = recv_all(sock, payload, hdr.payload_len);
        if (r <= 0) { fprintf(stderr, "[receiver] failed to read payload\n"); break; }

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        uint32_t recomputed;
        if (hdr.scheme == SCHEME_CHECKSUM) {
            recomputed = checksum16_compute(payload, hdr.payload_len);
        } else {
            recomputed = crc_compute(payload, hdr.payload_len,
                                      crc_poly_for_type(hdr.crc_type), hdr.crc_type);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double decode_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

        int mismatch = (recomputed != hdr.redundancy);
        int detected = mismatch; /* error detected if redundancy fields differ */
        const char *verdict = mismatch ? "REJECT (error detected)" : "ACCEPT (no error detected)";

        total++;
        if (mismatch) rejected++; else accepted++;

        printf("[receiver] frame %u | scheme=%s crc=%d true_error=%s len=%u "
               "sent=0x%08X recomputed=0x%08X -> %s\n",
               hdr.frame_id, scheme_name(hdr.scheme), hdr.crc_type,
               error_type_name(hdr.error_type), hdr.payload_len,
               hdr.redundancy, recomputed, verdict);

        fprintf(log, "%u,%s,%d,%s,%u,0x%08X,0x%08X,%d,%.6f\n",
                hdr.frame_id, scheme_name(hdr.scheme), hdr.crc_type,
                error_type_name(hdr.error_type), hdr.payload_len,
                hdr.redundancy, recomputed, detected, decode_ms);
        fflush(log);
    }

    printf("[receiver] summary: total=%ld accepted=%ld rejected=%ld\n", total, accepted, rejected);

    /* Step 5: shutdown connection */
    shutdown(sock, SHUT_RDWR);
    close(sock);
    close(listen_sock);
    fclose(log);
    return 0;
}
