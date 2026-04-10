# QuillOS

**QuillOS** is a from-scratch x86-64 operating system with a built-in distributed computing runtime. It boots on bare metal via Limine, runs a preemptive multitasking kernel, and can spawn processes or dispatch computational jobs across multiple nodes over a real network stack.

Created by Jasper Dragoo on March 8th, 2026. Developed by Jasper Dragoo and Miles Magyar.

---

## What it is

QuillOS is a hobby kernel written in C++ and x86-64 assembly. Unlike most toy kernels that stop at "it prints hello world," QuillOS has a full stack:

- A real interrupt system (32 ISRs, 16 IRQs, proper PIC remapping)
- A preemptive round-robin scheduler with sleep/wake support
- A bitmap physical memory manager + free-list kernel heap
- An ATA PIO disk driver
- A PCI bus enumerator
- An Intel E1000 network driver with DMA descriptor rings
- A network stack: Ethernet, ARP, IPv4, UDP
- A cluster protocol for peer discovery and distributed work
- A distributed job scheduler that load-balances across nodes
- A distributed process system with remote spawn and kill

Two QuillOS nodes running in separate QEMU VMs can discover each other, exchange load metrics, and dispatch work — the less-loaded node runs the job.

---

## Kernel subsystems

| # | Component | Status | Notes |
|---|-----------|--------|-------|
| 1 | IDT / Interrupts | Done | Full exception handler with register dump, IRQ registration API |
| 2 | Timer (PIT) | Done | 1 kHz tick, drives preemptive scheduling |
| 3 | Preemptive scheduler | Done | Round-robin, 10 ms slices, sleep/wake, idle task |
| 4 | Memory manager | Done | PMM bitmap + kernel heap with split/coalesce |
| 5 | In-memory filesystem | Done | Path-based API, shell commands (`ls`, `cat`, `mkdir`, etc.) |
| 6 | ATA PIO disk driver | Done | IDENTIFY, LBA28 read/write, hexdump |
| 7 | PCI bus enumeration | Done | Config space read/write, bus mastering |
| 8 | E1000 NIC driver | Done | DMA rings, poll receive, real Ethernet |
| 9 | Network stack | Done | Ethernet + ARP + IPv4 + UDP |
| 10 | Cluster protocol | Done | Peer discovery, load reports, job messages |
| 11 | Distributed jobs | Done | `run sum 10 20 30` auto-scheduled |
| 12 | Distributed processes | Done | `spawn counter` / `kill <pid>` — local or remote |

---

## Shell commands

### System
`help` `cls` `ver` `uptime` `halt` `reboot` `color <name>`

### Filesystem
`ls [path]` `mkdir <name>` `touch <name>` `cat <name>` `write <name> <text>` `rm <name>` `pwd` `cd <path>`

### Diagnostics
`meminfo` `heapinfo` `heaptest` `intinfo` `lspci` `diskinfo`

### Tasks and processes
`ps` — list scheduler tasks
`procs` — list distributed processes (local + remote)
`spawn <type> [node]` — start a process (types: `counter`, `stress`, `monitor`, `worker`)
`kill <pid>` — terminate a process (local or remote)

### Disk
`readsec <lba>` — read a sector as ASCII
`hexdump <lba>` — hex dump a sector

### Network and cluster
`netinfo` — IP, MAC, packet counts
`discover` — broadcast peer discovery
`peers` — list discovered cluster nodes
`cluster` — cluster status overview
`run <type> <args..>` — auto-scheduled distributed job
  - `run sum 10 20 30`
  - `run product 2 3 7`
  - `run max 5 99 12`
  - `run prime 5000`
`jobs` — job history with status and where each ran
`rjob <ip> <n1 n2..>` — manual remote job submit

---

## Building and running

### Windows 11 (via WSL)

```powershell
# In PowerShell as Administrator (one time)
wsl --install -d Ubuntu
```

After restart, open the Ubuntu terminal:

```bash
sudo apt update && sudo apt install -y build-essential nasm xorriso qemu-system-x86 make
git clone https://github.com/Miles-Magyar/quillos.git
cd quillos/quillos
sed -i 's/\r$//' Makefile linker.ld run.sh run-cluster.sh
bash run.sh
```

### macOS (Intel)

```bash
brew install x86_64-elf-gcc x86_64-elf-binutils nasm xorriso qemu
git clone https://github.com/Miles-Magyar/quillos.git
cd quillos
bash setup.sh
```

### Linux

```bash
sudo apt install build-essential nasm xorriso qemu-system-x86
git clone https://github.com/Miles-Magyar/quillos.git
cd quillos
bash setup.sh
```

---

## Running a 2-node cluster

From inside `quillos/quillos/`:

```bash
bash run-cluster.sh --build
```

Two QEMU windows open with MAC addresses ending in `01` and `02`. They get IPs `10.0.0.1` and `10.0.0.2` automatically. Within a few seconds they discover each other via the heartbeat protocol.

Try the distributed features in either shell:

```
peers                      # list the other node
run sum 100 200 300        # auto-scheduled — goes to whichever node has less load
run prime 10000            # count primes up to 10000
jobs                       # see where each ran
spawn counter              # start a long-running process
spawn stress 2             # start a CPU stress test on Node 2
procs                      # see all processes across the cluster
kill 1                     # stop a process (local or remote)
```

---

## Architecture

```
┌─────────────────────────────────────────────┐
│  Shell — commands, filesystem, diagnostics  │
├─────────────────────────────────────────────┤
│  Distributed Process Manager                │
│  spawn, kill, process table (local+remote)  │
├─────────────────────────────────────────────┤
│  Distributed Job Scheduler (DJob)           │
│  load balancing, job history                │
├─────────────────────────────────────────────┤
│  Cluster Protocol                           │
│  discovery, load reports, job dispatch      │
├─────────────────────────────────────────────┤
│  Network Stack                              │
│  UDP → IPv4 → ARP → Ethernet                │
├─────────────────────────────────────────────┤
│  E1000 NIC Driver  │  ATA PIO Disk Driver   │
├─────────────────────────────────────────────┤
│  PCI Bus           │  Filesystem            │
├─────────────────────────────────────────────┤
│  Preemptive Scheduler  │  Memory Manager    │
├─────────────────────────────────────────────┤
│  IDT / IRQ / Timer     │  Framebuffer       │
├─────────────────────────────────────────────┤
│  Limine Bootloader (x86-64)                 │
└─────────────────────────────────────────────┘
```

---

## Source layout

```
quillos/
├── Makefile              # Auto-detects macOS cross-compiler
├── linker.ld             # Kernel at 0xffffffff80000000
├── run.sh                # Build + single-node QEMU launch
├── run-cluster.sh        # Build + 2-node QEMU launch
├── kernel/
│   ├── kernel.cpp        # _start entry point
│   ├── idt.cpp           # IDT + PIC + dispatchers
│   ├── interrupts.asm    # ISR/IRQ stubs + context switch
│   ├── timer.cpp         # PIT + IRQ 0 handler
│   ├── keyboard.cpp      # IRQ 1 handler
│   ├── scheduler.cpp     # Preemptive multitasking
│   ├── memory.cpp        # PMM bitmap + kernel heap
│   ├── filesystem.cpp    # In-memory FS
│   ├── pci.cpp           # PCI enumeration
│   ├── disk.cpp          # ATA PIO driver
│   ├── e1000.cpp         # Intel NIC driver
│   ├── network.cpp       # Ethernet/ARP/IP/UDP
│   ├── cluster.cpp       # Discovery + protocol
│   ├── djob.cpp          # Distributed job scheduler
│   ├── jobs.h            # Job abstraction
│   ├── process.cpp       # Distributed process manager
│   ├── shell.cpp         # Command parser
│   └── console.cpp       # Framebuffer text output
├── include/
│   └── limine.h          # Limine boot protocol
└── limine/               # Limine bootloader binaries
```

---

## License

MIT
