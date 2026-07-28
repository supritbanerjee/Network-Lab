#!/bin/bash
# run_evaluation.sh
#
# Automates running the sender against the receiver across every
# scheme / CRC-type / error-type combination the assignment asks you
# to evaluate, and appends every result to results.csv so you can
# build tables/graphs in Excel, Python (pandas/matplotlib), etc.
#
# Usage: ./run_evaluation.sh <file_to_send> [host] [base_port]

set -e
FILE=${1:-testfile.bin}
HOST=${2:-127.0.0.1}
BASE_PORT=${3:-20000}

if [ ! -f "$FILE" ]; then
    echo "Input file '$FILE' not found."
    exit 1
fi

rm -f results.csv
PORT=$BASE_PORT

run_case () {
    local scheme=$1
    local crc_type=$2
    local error_type=$3
    PORT=$((PORT+1))

    ./receiver "$PORT" results.csv > "receiver_${scheme}_${crc_type}_${error_type}.log" 2>&1 &
    local rpid=$!
    sleep 0.4
    ./sender "$HOST" "$PORT" "$FILE" "$scheme" "$crc_type" "$error_type" \
        > "sender_${scheme}_${crc_type}_${error_type}.log" 2>&1
    wait "$rpid" 2>/dev/null
    echo "done: scheme=$scheme crc_type=$crc_type error_type=$error_type"
}

echo "=== Checksum, all error types ==="
for err in none single two odd burst; do
    run_case checksum 0 "$err"
done

echo "=== CRC, all polynomials x all error types ==="
for crc in 8 10 16 32; do
    for err in none single two odd burst; do
        run_case crc "$crc" "$err"
    done
done

echo
echo "All runs complete. See results.csv for the combined table."
echo "Columns: frame_id,scheme,crc_type,error_type,payload_len,sent_redundancy,recomputed_redundancy,detected,decode_time_ms"
