# ProcX - Advanced Process Management System

**ProcX** is a high-performance, POSIX-compliant distributed process management system designed for UNIX-like environments. It enables users to spawn, control, and monitor processes across multiple terminal instances synchronously, acting as a custom kernel-level process manager.

This project demonstrates advanced system programming concepts including **Shared Memory**, **Named Semaphores**, **Message Queues**, and **Daemon Processes**, operating with a decentralized architecture where every instance is a peer.

---

## 🚀 Key Features

* **Multi-Instance Synchronization:** Run ProcX in multiple terminals simultaneously. Any action (spawn/kill) in one terminal is instantly reflected across all active instances via Shared Memory.
* **Dual Execution Modes:**
  * **Attached Mode:** Processes are child processes of the current terminal. If the terminal closes, the process terminates.
  * **Detached Mode (Daemon-like):** Processes are disassociated from the controlling terminal using `setsid()`. They persist in the background even if the parent ProcX instance exits.


* **Real-Time IPC Messaging:** Uses System V Message Queues with a **"Targeted Delivery"** protocol to broadcast events (Start/Stop) efficiently between specific terminals.
* **Automated Zombie Cleanup:** A dedicated **Monitor Thread** runs in the background, polling for terminated processes using non-blocking calls (`WNOHANG`) to prevent zombie process accumulation.
* **Graceful Shutdown & Resource Management:** Implements a reference-counted resource cleanup strategy. System resources (SHM, Semaphores) are only destroyed when the *last* active terminal exits.
* **Deadlock Prevention:** Uses a "Poison Pill" strategy to safely unblock threads during shutdown sequences.

---

## 🛠 Architecture & Technical Decisions

ProcX is built upon four main pillars of Operating System design:

### 1. Centralized State (POSIX Shared Memory)

* **Mechanism:** `shm_open`, `mmap`
* **Usage:** A monolithic state structure containing the Process Table and Terminal Registry is mapped into the address space of every active instance.
* **Benefit:** Provides O(1) access to the global system state without the overhead of socket-based communication.

### 2. Concurrency Control (Named Semaphores)

* **Mechanism:** `sem_open`, `sem_wait`, `sem_post`
* **Usage:** Acts as a global mutex protecting the Critical Section (Shared Memory).
* **Benefit:** Guarantees data consistency and prevents Race Conditions when multiple terminals attempt to modify the process list simultaneously.

### 3. Inter-Process Communication (Targeted Message Queue)

* **Mechanism:** `msgget`, `msgsnd`, `msgrcv`
* **Innovation:** Instead of a chaotic broadcast, ProcX uses **Targeted Routing**. Messages are tagged with the specific PID (`mtype = target_pid`) of the destination terminal.
* **Benefit:** Prevents "message stealing" by other threads and reduces CPU context switching overhead.

### 4. Background Monitoring (Threading)

* **Mechanism:** `pthread_create`, `waitpid`
* **Usage:** A secondary thread acts as a Garbage Collector. It handles `SIGCHLD` logic asynchronously by periodically checking process states.

---

## 📦 Installation & Build

### Prerequisites

* GCC Compiler (Support for C99 or later)
* Make
* Linux Environment (or WSL/macOS with POSIX support)

### Compilation

Use the provided `Makefile` to compile the source code:

```bash
make

```

To clean up build artifacts (object files and executables):

```bash
make clean

```

---

## 💻 Usage Guide

Start the application. You can open multiple terminal windows and run the same command to test synchronization.

```bash
./procx

```

### Interactive Menu

1. **Start New Process:**
* Enter command (e.g., `sleep 100`, `firefox`).
* **Mode 0 (Attached):** Process lives as long as the terminal is open.
* **Mode 1 (Detached):** Process becomes independent (daemonized).


2. **List Running Processes:**
* Displays a real-time table of all processes managed by the ProcX ecosystem, showing PID, Owner, Mode, and Uptime.


3. **Terminate Process:**
* Send a `SIGTERM` signal to any managed process by entering its PID.


4. **Exit:**
* Safely shuts down the local instance. If it is the last running instance, it performs a full system cleanup (unlinking SHM/Semaphores).



---

## 🔧 Deep Dive: The "Poison Pill" Strategy

One of the complex challenges in multi-threaded IPC is safely shutting down a thread blocked on a system call (like `msgrcv`). ProcX solves this with the **Poison Pill** pattern:

```c
// When shutdown is requested:
Message poison_pill;
poison_pill.msg_type = getpid(); // Target our own thread
poison_pill.command = 99;        // Special KILL command

// Send a non-blocking message to self
msgsnd(msg_queue_id, &poison_pill, ..., IPC_NOWAIT);

```

The listener thread receives this specific message, recognizes the kill command, breaks its loop, and allows `pthread_join` to complete successfully, ensuring zero resource leaks.

---

## 📂 Project Structure

```
procx/
├── procx.c         # Core implementation (Single-file architecture)
├── Makefile        # Build automation script
└── README.md       # Project documentation

```

---

## 📄 License

This project is licensed under the MIT License. 

**Author:** Mehmet Enes Erden
**Date:** December 2025
