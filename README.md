# OS Kernel Project: Producer-Consumer with Kernel Threads

A Linux kernel module implementing the classic Producer-Consumer problem
using kernel threads, semaphores, and a circular shared buffer —
written as part of CSE330 (Operating Systems, Spring 2026).

## What It Does

When loaded via `insmod`, the module:
1. Spawns **1 producer kernel thread** (`kProducer-1`) that scans the Linux
   task list and enqueues every process owned by a given UID into a shared buffer
2. Spawns **N consumer kernel threads** (`kConsumer-1` ... `kConsumer-N`) that
   dequeue items and compute how long each process has been running
3. On `rmmod`, reports total items produced/consumed and total elapsed
   process time in `HH:MM:SS`

## Synchronization

| Primitive | Role |
|-----------|------|
| `empty` semaphore | Blocks producer when buffer is full |
| `full` semaphore | Blocks consumers when buffer is empty |
| `mutex` semaphore | Mutual exclusion on shared buffer access |

## Files

| File | Description |
|------|-------------|
| `producer_consumer.c` | Full kernel module — producer/consumer threads, semaphores, circular buffer |
| `Makefile` | Builds the `.ko` kernel object |

## Usage

```bash
# Build
make

# Load module (example: UID=1000, buffer=50, 1 producer, 5 consumers)
sudo insmod producer_consumer.ko uuid=1000 buffSize=50 prod=1 cons=5

# View kernel log output
sudo dmesg | tail -30

# Unload module (prints final stats)
sudo rmmod producer_consumer
```

## Sample Output
```
###[thread_init_module]### Kernel module received inputs: UID:1000, Buffer-Size:50, prod:1, cons:1
###[producer_thread_function]### kProducer-1 Producer Thread stopped.
###[thread_exit_module]### Total number of items produced: 500
###[thread_exit_module]### Total number of items consumed: 500
###[thread_exit_module]### Total elapsed time for UID 1000 is   1:14:42
```

## Test Screenshots

| Test | Description |
|------|-------------|
| `test1.png` | Buffer=5, prod=0, cons=1 — no producer edge case |
| `test2.png` | Buffer=5, prod=1, cons=1 — basic run |
| `test3.png` | Buffer=50, prod=1, cons=1 — 10 items produced/consumed |
| `test4.png` | Buffer=50, prod=1, cons=2 — 2 consumers, 100 items |
| `test5.png` | Buffer=50, prod=1, cons=1 — 500 items, elapsed 1:14:42 |
| `bonus.png` | Bonus test case |

## Tools & Environment
- Linux Kernel Module (C)
- Tested on: `qemu-system-x86_64` (pwn.college environment)
- Kernel synchronization: semaphores (`linux/semaphore.h`)
- Threading: `kthread_run`, `kthread_stop`
