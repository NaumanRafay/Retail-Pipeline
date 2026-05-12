# Parallel CSV Data Processing Pipeline

 Nauman Rafay  

---

## What This Project Does

This project is a parallel data processing pipeline built entirely using Linux OS primitives. It reads retail transaction CSV files, processes them through multiple parallel worker threads, calculates total revenue per category and top 3 products per category, and generates human-readable and machine-readable reports.

The entire pipeline is split across 4 separate processes that communicate using named pipes (FIFO), POSIX shared memory, and semaphores — no high level frameworks, no external libraries, pure OS system calls.

---

## OS Concepts Demonstrated

| Concept | Where Used |
|---|---|
| `fork()` + `exec()` | dispatcher creates ingester, processor, reporter as child processes |
| `waitpid()` | dispatcher reaps all children, no zombies |
| Named Pipe (FIFO) | ingester → processor data transfer |
| POSIX Shared Memory | processor → reporter result handoff |
| Named Semaphore | processor signals reporter when data is ready |
| Unnamed Semaphores | bounded buffer inside processor (producer-consumer) |
| Mutex | protects aggregation table and queue from race conditions |
| POSIX Threads (`pthread`) | thread pool inside processor with reader + N worker threads |
| `pthread_attr_t` | explicit stack size (1MB) and joinable state set |
| `dup()` + `dup2()` | log file redirection per child, stdout save/restore in reporter |
| Signals | SIGINT, SIGTERM graceful shutdown, SIGUSR1 reporter→dispatcher notification |
| `atexit()` | guaranteed cleanup on any exit path |
| Shell Scripting | `run.sh` with getopts, functions, trap, arithmetic, case |

---

## Project Architecture

```
run.sh
  └── dispatcher (master process)
        ├── fork+exec ──► ingester
        │                    │
        │              Named Pipe (FIFO)
        │                    │
        ├── fork+exec ──► processor
        │               (reader thread + N worker threads)
        │                    │
        │              Shared Memory + Named Semaphore
        │                    │
        └── fork+exec ──► reporter
                             │
                        SIGUSR1 signal
                             │
                         dispatcher
```

Data flow: CSV files → ingester chunks → FIFO → processor threads → shared memory → reporter → report.txt + report.csv

---

## File Structure

```
OS_FinalProject_DS-A/
├── src/
│   ├── common.h          # shared structs, constants, all includes
│   ├── dispatcher.cpp    # master process: setup, fork/exec, signals, cleanup
│   ├── ingester.cpp      # reads CSV files, sends chunks over FIFO
│   ├── processor.cpp     # thread pool, parses chunks, aggregates data
│   └── reporter.cpp      # reads shared memory, writes report files
├── data/
│   ├── input.txt         # list of CSV filenames to process
│   ├── sales1.csv        # sample retail data
│   └── sales2.csv        # sample retail data
├── report/
│   └── report.pdf        # written project report
├── logs/                 # auto-created at runtime
│   ├── ingester.log
│   ├── processor.log
│   └── reporter.log
├── Makefile
├── run.sh
├── README.md
└── DECLARATION.txt
```

---

## CSV Input Format

Each CSV file must follow this format — no header row, comma separated:

```
category,product_name,price,quantity
```

Example:

```
Grocery,Rice,250,2
Electronics,Mouse,1200,1
Clothes,Shirt,900,3
```

An `input.txt` file must exist inside the input directory listing the CSV filenames one per line:

```
sales1.csv
sales2.csv
```

The pipeline reads only the files listed in `input.txt`. The number of files and columns is not hardcoded — it reads whatever is given.

---

## How to Build

Make sure you are on Ubuntu 22.04 or later with gcc and make installed.

```bash
sudo apt install build-essential
```

Then build:

```bash
make
```

This compiles all four executables — `dispatcher`, `ingester`, `processor`, `reporter` — in the current directory.

To clean build artifacts:

```bash
make clean
```

---

## How to Run

Use the provided `run.sh` script:

```bash
./run.sh -i <input_dir> -o <output_dir> -n <threads>
```

Example:

```bash
./run.sh -i data -o output -n 4
```

### Options

| Flag | Description | Required |
|---|---|---|
| `-i` | Input directory containing CSV files and input.txt | Yes |
| `-o` | Output directory where report.txt and report.csv will be written | Yes |
| `-n` | Number of worker threads (default: 4) | No |
| `-c` | Clean build artifacts before running | No |
| `-h` | Show help message | No |

---

## Output

After a successful run you get:

**output/report.txt** — human readable:
```
Retail Report
Total Records: 10

Category: Electronics
Revenue: 5800
Top Products:
  1) Mouse -> 3600
  2) Keyboard -> 2200

Category: Grocery
Revenue: 1890
Top Products:
  1) Rice -> 750
  2) Tea -> 600
  3) Sugar -> 540
```

**output/report.csv** — machine readable:
```
category,total_revenue,top1,top2,top3
Electronics,5800,Mouse,Keyboard,
Grocery,1890,Rice,Tea,Sugar
Clothes,4500,Shirt,Jeans,
```

**Terminal summary:**
```
runtime_seconds=1
records_processed=3
exit_status=0
```

**logs/** directory contains per-process logs with PID and PPID on every line:
```
[ingester PID=46 PPID=45] started reading csv files
[ingester PID=46 PPID=45] processing file: sales1.csv
[processor PID=47 PPID=45] done
[reporter PID=48 PPID=45] waiting for processor
[reporter PID=48 PPID=45] report files written
```

---

## How Each Component Works

### dispatcher
Parses arguments, creates the FIFO with `mkfifo()`, creates shared memory with `shm_open()` + `ftruncate()`, creates named semaphore with `sem_open()` initialised to 0. Installs signal handlers for SIGINT, SIGTERM, SIGCHLD, SIGUSR1. Forks three children — each child redirects its stdout and stderr to its own log file using `dup2()` before calling `execvp()`. Then blocks in `waitpid()` for all three children. On exit cleans up FIFO, shared memory, and semaphore.

### ingester
Reads `input.txt` from the input directory to get CSV filenames. Opens the FIFO for writing — this call blocks until processor opens the read end, which is the intended synchronization. Reads each CSV file in chunks of 20 lines, packs each chunk into a `Chunk` struct with a header containing chunk_id, byte_count, source_file_id, is_eof, and writes it to the FIFO. When all files are done sends a final chunk with `is_eof = 1`.

### processor
Opens the FIFO for reading. Initialises two unnamed semaphores `sem_empty` (queue capacity) and `sem_full` (items available) for the bounded queue. Creates N worker threads and one reader thread using `pthread_create()` with explicit `pthread_attr_t` setting 1MB stack and joinable state. Reader thread reads chunks from FIFO and pushes to bounded queue. Worker threads pop chunks, parse CSV lines, calculate revenue (price × quantity), and update the aggregation table protected by `table_mutex`. When EOF chunk arrives reader sends N poison pills to stop workers. After all workers join, sorts products by revenue, writes aggregated data to shared memory, and posts the named semaphore.

### reporter
Opens the named semaphore and calls `sem_wait()` — blocks here until processor posts it. Then opens shared memory with `shm_open()` + `mmap()`. Demonstrates `dup()` and `dup2()` by saving stdout with `dup()`, redirecting stdout to report.txt with `dup2()`, writing the report using `cout`, then restoring stdout with `dup2()`. Writes report.csv using ofstream. Sends SIGUSR1 to dispatcher via `kill(getppid(), SIGUSR1)`. Cleans up and exits.

---

## Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 10 | Bad command line arguments |
| 20 | Failed to create FIFO, shared memory, or semaphore |
| 30 | Child process fork failed |
| 40 | I/O error — missing files, unreadable CSV |
| 130 | Interrupted by SIGINT (Ctrl+C), clean shutdown |
| 143 | Interrupted by SIGTERM, clean shutdown |

---

## Cleanup Guarantee

Pressing Ctrl+C during a run triggers graceful shutdown:

1. SIGINT reaches dispatcher
2. Dispatcher forwards SIGTERM to all three children
3. Each child exits cleanly
4. Dispatcher `waitpid()` reaps all children — no zombies
5. `atexit(cleanup)` runs — FIFO unlinked, shared memory unlinked, semaphore unlinked

After the program exits `ipcs -m` will show no leftover shared memory and `ls /tmp/retail_fifo_*` will show nothing belonging to this project.

---

## Concurrency Design

Inside processor the bounded queue connects the reader thread and worker threads:

```
FIFO
  |
  | read()
  ▼
reader thread
  |
  | sem_wait(sem_empty) → lock q_mutex → push → unlock → sem_post(sem_full)
  ▼
bounded queue [size Q]
  ▼
  | sem_wait(sem_full) → lock q_mutex → pop → unlock → sem_post(sem_empty)
  |
worker thread 1  ──► parse chunk ──► lock table_mutex ──► update aggregation table ──► unlock
worker thread 2  ──► parse chunk ──► lock table_mutex ──► update aggregation table ──► unlock
worker thread N  ──► parse chunk ──► lock table_mutex ──► update aggregation table ──► unlock
```

Two mutexes, two unnamed semaphores, one named semaphore — each serving a specific purpose with no overlap.

---

## Dependencies

No external libraries. Only standard Linux/POSIX:

- `gcc` / `g++` with `-pthread` flag
- `librt` for POSIX shared memory and named semaphores (`-lrt`)
- Ubuntu 22.04 or later recommended

---

## Dataset Variant

This implementation handles the **Retail Transactions** variant:

- Revenue per record = price × quantity
- Per-category total revenue aggregated across all records
- Top 3 products per category ranked by revenue
