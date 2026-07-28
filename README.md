# Network-Lab
CN lab
# CSE/PC/B/S/314 — Assignment 1: Checksum & CRC Error Detection

Client-server implementation matching the "Client — high level view" diagram:
create socket → setup server address → connect → read/write data → shutdown.

## Files
- `common.h` / `common.c` — frame format, 16-bit checksum, generic bit-by-bit CRC
  (CRC-8/10/16/32 with the exact polynomials from the assignment), the four
  error-injection routines, and reliable send/recv helpers.
- `sender.c` — the **Sender** (client). Reads a file, splits it into
  Ethernet-sized frames (46–1500 byte payload, padded if short), computes the
  checksum/CRC over the clean payload, then simulates a transmission error on
  the copy that is actually sent, and transmits frame header + payload.
- `receiver.c` — the **Receiver** (server). Accepts one connection, recomputes
  the checksum/CRC over the *received* payload, and compares it to the
  redundancy field to ACCEPT or REJECT the frame — exactly what a real
  receiver does. Every frame is logged to a CSV for your report.
- `run_evaluation.sh` — runs all scheme × CRC-type × error-type combinations
  automatically and appends everything to `results.csv`.

## Build
```
make
```

## Run manually
```
# terminal 1
./receiver <port> [results.csv]

# terminal 2
./sender <server_ip> <port> <file> <scheme> <crc_type> <error_type> [burst_len]
```
- `scheme`: `checksum` | `crc`
- `crc_type`: `8` | `10` | `16` | `32` (pass `0` when scheme is `checksum`)
- `error_type`: `none` | `single` | `two` | `odd` | `burst`
- `burst_len`: optional, only used when `error_type=burst` (default 8 bits)

## Run the full evaluation automatically
```
./run_evaluation.sh <file> [host] [base_port]
```
This produces `results.csv` with every combination of:
- scheme: checksum, CRC-8, CRC-10, CRC-16, CRC-32
- error type: none, single-bit, two isolated single-bit, odd number of errors, burst

Columns: `frame_id, scheme, crc_type, error_type, payload_len, sent_redundancy,
recomputed_redundancy, detected, decode_time_ms`

Load `results.csv` in Excel / pandas to build the required tables and graphs
(detection rate per error type per scheme, and timing comparisons using the
`decode_time_ms` column plus the `total_encode_time_ms` the sender prints).

## Notes for your report
- **Checksum vs CRC**: with real data you should see checksum occasionally
  miss two-isolated-bit or odd-error cases that CRC always catches (CRC's
  strength is being able to detect *all* burst errors up to the polynomial's
  degree and all odd numbers of errors, by construction). Run the evaluation
  a few times with different random seeds (re-run `run_evaluation.sh`, it
  reseeds using time+PID) to build up statistics for the "detected by X but
  not Y" cases the evaluation section asks for.
- **Execution time / resource utilization**: the sender prints total/average
  encode time; the receiver logs per-frame decode time in `results.csv`. You
  can additionally wrap runs with `/usr/bin/time -v` to capture memory/CPU
  utilization for the report.
