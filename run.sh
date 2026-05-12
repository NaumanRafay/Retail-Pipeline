#!/usr/bin/env bash

INPUT_DIR=""
OUTPUT_DIR=""
THREADS=4
CLEAN=0
PID_FILE=".dispatcher.pid"

FIFO_PATH="/tmp/retail_fifo_$$"
SHM_NAME="/retail_shm_$$"

help_msg() {
  echo "Usage: ./run.sh -i <input_dir> -o <output_dir> [-n threads] [-c] [-h]"
}

cleanup() {
  local pid=""
  if [ -f "$PID_FILE" ]; then
    pid=$(cat "$PID_FILE")
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill -TERM "$pid" >/dev/null 2>&1
    fi
    rm -f "$PID_FILE"
  fi
}

count_csv() {
  local c=0
  for f in "$INPUT_DIR"/*.csv; do
    [ -f "$f" ] && c=$((c+1))
  done
  echo "$c"
}

while getopts ":i:o:n:ch" opt; do
  case "$opt" in
    i) INPUT_DIR="$OPTARG" ;;
    o) OUTPUT_DIR="$OPTARG" ;;
    n) THREADS="$OPTARG" ;;
    c) CLEAN=1 ;;
    h) help_msg; exit 0 ;;
    *) help_msg; exit 1 ;;
  esac
done

[ -z "$INPUT_DIR" ] && help_msg && exit 1
[ -z "$OUTPUT_DIR" ] && help_msg && exit 1
[ ! -d "$INPUT_DIR" ] && echo "input directory missing" && exit 1

if [ "$CLEAN" -eq 1 ]; then
  make clean
fi

mkdir -p "$OUTPUT_DIR"

csvs=$(count_csv)
[ "$csvs" -lt 1 ] && echo "no csv files" && exit 1
[ ! -f "$INPUT_DIR/input.txt" ] && echo "input.txt missing" && exit 1

make || { echo "build failed"; exit 1; }

trap cleanup EXIT INT TERM

start=$(date +%s)
./dispatcher "$INPUT_DIR" "$OUTPUT_DIR" "$THREADS" "$FIFO_PATH" "$SHM_NAME" &
dpid=$!
echo "$dpid" > "$PID_FILE"

wait "$dpid"
status=$?

end=$(date +%s)
runtime=$((end-start))

rows=0
total_records=0
if [ -f "$OUTPUT_DIR/report.csv" ]; then
  first=1
  while IFS= read -r line; do
    [ "$first" -eq 1 ] && first=0 && continue
    rows=$((rows+1))
  done < "$OUTPUT_DIR/report.csv"
fi

if [ -f "$OUTPUT_DIR/report.txt" ]; then
  while IFS= read -r tline; do
    case "$tline" in
      "Total Records:"*)
        total_records=${tline#Total Records: }
        break
        ;;
    esac
  done < "$OUTPUT_DIR/report.txt"
fi

echo "runtime_seconds=$runtime"
echo "categories_processed=$rows"
echo "total_records=$total_records"
echo "exit_status=$status"

exit "$status"
