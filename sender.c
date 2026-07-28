/*
 * sender.c - Client ("Sender") program
 *
 * High level view (matches the client architecture diagram):
 *   1. Create a socket
 *   2. Setup the server address
 *   3. Connect to the server
 *   4. Read/write data   (read file -> build frames -> inject error -> send)
 *   5. Shutdown connection
 *
 * Usage:
 *   ./sender <server_ip> <port> <file> <scheme> <crc_type> <error_type> [burst_len]
 *
 *   scheme     : checksum | crc
 *   crc_type   : 8 | 10 | 16 | 32   (ignored if scheme=checksum, still required as a placeholder, use 0)
 *   error_type : none | single | two | odd | burst
 *   burst_len  : optional, only used when error_type=burst (default 8)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

static int parse_scheme(const char *s) {
    if (strcmp(s, "checksum") == 0) return SCHEME_CHECKSUM;
    if (strcmp(s, "crc") == 0) return SCHEME_CRC;
    fprintf(stderr, "Unknown scheme '%s' (use checksum|crc)\n", s);
    exit(1);
}

static int parse_error_type(const char *s) {
    if (strcmp(s, "none") == 0) return ERR_NONE;
    if (strcmp(s, "single") == 0) return ERR_SINGLE_BIT;
    if (strcmp(s, "two") == 0) return ERR_TWO_ISOLATED;
    if (strcmp(s, "odd") == 0) return ERR_ODD;
    if (strcmp(s, "burst") == 0) return ERR_BURST;
    fprintf(stderr, "Unknown error_type '%s' (use none|single|two|odd|burst)\n", s);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 7) {
        fprintf(stderr,
            "Usage: %s <server_ip> <port> <file> <scheme:checksum|crc> "
            "<crc_type:8|10|16|32|0> <error_type:none|single|two|odd|burst> [burst_len]\n",
            argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *filename = argv[3];
    int scheme = parse_scheme(argv[4]);
    int crc_type = atoi(argv[5]);
    int error_type = parse_error_type(argv[6]);
    int burst_len = (argc >= 8) ? atoi(argv[7]) : 8;

    if (scheme == SCHEME_CRC && crc_type != 8 && crc_type != 10 && crc_type != 16 && crc_type != 32) {
        fprintf(stderr, "crc_type must be one of 8, 10, 16, 32 when scheme=crc\n");
        return 1;
    }

    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("fopen"); return 1; }

    /* Step 1: create a socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    /* Step 2: setup the server address */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP: %s\n", server_ip);
        return 1;
    }

    /* Step 3: connect to the server */
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }
    printf("[sender] connected to %s:%d\n", server_ip, port);

    uint8_t raw[MAX_PAYLOAD];
    uint8_t tx[MAX_PAYLOAD];
    uint32_t frame_id = 0;
    double total_encode_time = 0.0;
    long frames_sent = 0;

    /* Step 4: read/write data */
    while (1) {
        size_t nread = fread(raw, 1, MAX_PAYLOAD, fp);
        if (nread == 0) break;

        size_t payload_len = nread;
        if (payload_len < MIN_PAYLOAD) {
            memset(raw + payload_len, 0, MIN_PAYLOAD - payload_len); /* pad, per 802.3 min payload */
            payload_len = MIN_PAYLOAD;
        }

        memcpy(tx, raw, payload_len);

        /* Compute redundancy over the CLEAN payload (before simulated transmission error) */
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        uint32_t redundancy;
        if (scheme == SCHEME_CHECKSUM) {
            redundancy = checksum16_compute(tx, payload_len);
        } else {
            redundancy = crc_compute(tx, payload_len, crc_poly_for_type(crc_type), crc_type);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double encode_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        total_encode_time += encode_ms;

        /* Simulate the transmission error on the copy that actually goes on the wire */
        if (error_type == ERR_BURST) {
            inject_burst_error(tx, payload_len, burst_len);
        } else {
            apply_error(tx, payload_len, error_type);
        }

        FrameHeader hdr = {0};
        hdr.frame_id = frame_id++;
        hdr.scheme = (uint8_t)scheme;
        hdr.crc_type = (uint8_t)crc_type;
        hdr.error_type = (uint8_t)error_type;
        hdr.payload_len = (uint16_t)payload_len;
        hdr.redundancy = redundancy;

        uint8_t hdr_buf[HEADER_SIZE];
        header_serialize(&hdr, hdr_buf);

        if (send_all(sock, hdr_buf, HEADER_SIZE) < 0) { perror("send header"); break; }
        if (send_all(sock, tx, payload_len) < 0) { perror("send payload"); break; }

        frames_sent++;
        printf("[sender] frame %u sent | scheme=%s crc=%d error=%s len=%zu redundancy=0x%08X\n",
               hdr.frame_id, scheme_name(scheme), crc_type, error_type_name(error_type),
               payload_len, redundancy);
    }

    printf("[sender] done. frames_sent=%ld total_encode_time_ms=%.4f avg_encode_time_ms=%.6f\n",
           frames_sent, total_encode_time, frames_sent ? total_encode_time / frames_sent : 0.0);

    fclose(fp);

    /* Step 5: shutdown connection */
    shutdown(sock, SHUT_WR);
    close(sock);
    return 0;
}
