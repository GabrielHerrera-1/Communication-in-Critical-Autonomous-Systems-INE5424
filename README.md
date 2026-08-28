# Autonomous Systems Communication Stack

A C++17 communication stack for distributed autonomous systems, combining
raw Ethernet networking, System V IPC, QEMU-based multi-vehicle simulation,
clock synchronization, spatial communication filtering, and time-triggered
publish-subscribe messaging.

Built for POSIX/Linux with no runtime dependencies beyond libc and the
C++ Standard Library.

> Developed for **INE5424 — Operating Systems II** at UFSC (2026/1).
> This repository implements stages 1–5 of the 7-stage project specification.

**Tech stack:** `C++17` · `POSIX` · `Linux` · `QEMU` ·
`Raw Ethernet (AF_PACKET)` · `System V IPC` · `Shared Memory` ·
`Semaphores` · `Linux Kernel Modules` · `PTP` · `Publish-Subscribe` ·
`SCHED_FIFO`

## Highlights

- Multi-vehicle simulation using QEMU, scaling up to **24 concurrent VMs**
- Raw Ethernet communication over `AF_PACKET`/`SOCK_RAW`, broadcast-only
  and without IP, across isolated virtual network domains
- Intra-vehicle IPC via System V shared memory and semaphores, with a
  producer-consumer ring buffer for efficient broadcast fan-out
- Simplified IEEE 1588/PTP clock synchronization with **1.53–3.32 ms**
  residual offset in validation tests
- Custom Linux kernel module for simulated GPS-based spatial positioning
- Time-triggered publish-subscribe messaging with RSU-assisted recovery
- Fully asynchronous, signal/semaphore-driven reception
- Real-time message-handling threads using `SCHED_FIFO`

## Architecture

The stack follows the Observer × Observed pattern end to end:

```
Component (POSIX process)
   │
   ▼
SmartData<Type>            (stage 5 — Interest/Response, PRODUCER or CONSUMER role)
   │  Concurrent_Observer<Message, Port>
   ▼
Communicator<Vehicle_Protocol>   (blocking send/receive endpoint for app code)
   │  Concurrent_Observer<Buffer, Port>  — data handed off via a semaphore
   ▼
Protocol<SharedMemoryNIC, RawSocketNIC>  (multiplexes by Port, stamps timestamp/quadrant,
   │                                      intercepts SPTP control traffic)
   │  Conditional_Data_Observer<Buffer, EtherType>
   ▼
NIC<Engine>                 (owns the buffer pool, notifies Protocol on receive)
   │
   ├── RawSocketEngine     — AF_PACKET/SOCK_RAW, inter-VM, one per vehicle's gateway
   └── SharedMemoryEngine  — System V shm/sem, intra-VM, one per process
```

A few design decisions worth calling out:

- `NIC<Engine>` is a template. `RawSocketEngine` and `SharedMemoryEngine` expose the same surface (`engine_init`, `engine_send`, `engine_receive`, `engine_close`), so `Protocol` and everything above it never has to know which one it's talking to. `Protocol<SharedMemoryNIC, RawSocketNIC = void>` only instantiates the raw-socket NIC in the gateway process; regular components only see the SHM NIC and reach the outside world through it.
- Reception is signal/semaphore-driven on both engines, never polled. `RawSocketEngine` relies on `SIGIO`/`O_ASYNC`; `SharedMemoryEngine` blocks a dedicated thread on a per-reader System V semaphore. Both funnel into the same `NIC` receive callback, which notifies `Protocol` right away.
- Timestamp and quadrant are stamped in the header, not the payload, via function pointers injected into `NIC` from outside. That sidesteps a circular `NIC` ↔ `Protocol` template dependency, and every layer above can read the values without knowing how they were produced.
- Main and receive-service threads run `SCHED_FIFO` (priorities 98 and 99, set through `RT_Priority`), so a busy application thread doesn't starve message delivery.
- Each stage builds on the previous one's abstractions instead of replacing them. New concerns land in whichever layer owns them, mostly `Engine` or `Protocol::Header`, occasionally a new layer stacked on `Communicator`.

## Performance & validation

Numbers below come from the QEMU test logs referenced in each stage's report under `doc/`.

| Stage | Scenario | Result |
|---|---|---|
| 1 | Inter-VM RTT, 5 VMs, raw socket, 200 samples | avg **6.36 ms** (min 2.58 ms) → ~3.18 ms one-way |
| 2 | Intra-VM vs. inter-VM RTT, 20 samples each | SHM avg **1.92 ms** vs. raw socket avg **19.79 ms** (~10× faster) |
| 2 | Stress test, 5 VMs × 200 msgs | 0% loss in most runs, up to 2.3% loss under peak load in one run |
| 3 | SPTP residual offset, 6 VMs, 22 samples/pair | **1.53–3.32 ms** across sender/receiver pairs |
| 3 | Intra-VM RTT (SHM), 5000 samples | avg **0.46 ms**, ~9× faster than inter-VM |
| 4 | Spatial filtering, 9 VMs, 90 s | **0%** cross-quadrant delivery; anchored RSUs recorded 0 drift, mobile vehicles 30 transitions each |
| 5 | Period adherence, 22 VMs, 20 producers | worst-case average ≈500.994 ms for a 500 ms requested period |
| 5 | Full-scale test, 24 VMs (4 RSUs + 20 vehicles) | all 10 Interest/Response scenarios passing |

## Quick start

Requirements: a C++17 toolchain (`g++`), `qemu-system-x86_64`, Python 3. Building the GPS kernel module from source also requires a matching kernel build tree. A prebuilt `gps.ko` is committed, so this only matters if you touch `kernel/gps_module/gps.c` directly.

```sh
make                      # default: builds and runs the full stage-5 suite (10 scenarios) in QEMU
make build                # compile libso2.a and the core test binaries, no QEMU runs
make test-interest-all    # explicit: the same 10-scenario stage-5 suite
make test-sptp-simple     # stage 3 in isolation
make test-quadrant        # stage 4 in isolation
make measure-rtt          # inter-VM latency benchmark (raw socket, 5 VMs)
make measure-rtt-intra    # intra-VM latency benchmark (SHM, 1 VM)
make clean                # remove build/, logs/, compiled kernel-module artifacts
```

Every `make test-*` target boots the required number of QEMU VMs from the same statically-linked test binary (differentiated by `so2.vm_id=N` on the kernel command line), waits for a success marker in every VM's console log, and prints a pass/fail summary. Logs land under `logs/<scenario>/<timestamp>/`.

## How it works

### Stage 1 — inter-VM communication (raw sockets)

Minimal end-to-end stack: `Communicator` → `Protocol` → `NIC<RawSocketEngine>`, moving raw Ethernet frames (no IP, broadcast destination `FF:FF:FF:FF:FF:FF`) between at least 5 QEMU VMs on a private virtual network. Each vehicle component is a POSIX process (`fork()`); at this stage each one instantiates its own independent network stack, since intra-vehicle IPC doesn't exist yet (that's stage 2).

- Buffer pool: 50 send + 50 receive `Buffer<Ethernet::Frame>` slots, free-list stack (`Traits<NIC<Engine>>::SEND_BUFFERS`/`RECEIVE_BUFFERS`).
- Observer wiring: `NIC → Protocol` filtered by EtherType `0x8888`; `Protocol → Communicator` filtered by logical port.
- Validated with a 5-VM full-mesh scenario (`tests/v3.cpp`) and a ping-pong RTT benchmark.
- RTT ping-pong was chosen over raw packet-capture timestamps because per-VM QEMU clocks aren't directly comparable without the synchronization mechanism built in stage 3.

### Stage 2 — intra-VM communication (System V shared memory)

Introduces `SharedMemoryEngine`, confining the new IPC entirely to the `Engine` layer. `Protocol` becomes `Protocol<SharedMemoryNIC, RawSocketNIC = void>`: every process gets an SHM NIC, but only the vehicle's **gateway** process also instantiates the raw-socket NIC and forwards traffic between the two straight from `Protocol::update()`, no extra queue in between.

- SHM region: System V `shmget`/`semget`, producer-consumer ring buffer (100 slots), a `Component_Entry[]` bootstrap table, and a startup barrier so no message is written before every reader is attached.
- Addressing splits **slot** (fixed SHM ring index, gateway-assigned, resolves self-drop) from **port** (logical component type, portable across engines).
- Semaphores: 1 ring mutex, 1 free-slot counter, 1 per-reader pending counter, which gives broadcast fan-out and self-drop for free without extra bookkeeping.
- Full Ethernet frames, not a stripped-down format, are copied through SHM. That trades a small `memcpy` for zero translation code between the SHM and network paths in `Protocol`.

### Stage 3 — temporal synchronization (SPTP)

Messages gain a monotonically-increasing, collision-free `int64_t` timestamp (`Clock::monotonic_stamp()`, a lock-free CAS loop over `CLOCK_REALTIME`, chosen because it's OS-wide shared and adjustable via `clock_settime`). A new `PacketKind` (`DATA`/`SPTP_SYNC`/`SPTP_REQUEST_SYNC`) lets `Protocol` intercept and consume clock-sync control traffic before it reaches the application queue.

- `SPTP_Protocol<Address>` runs only in the gateway process, reusing the existing raw-socket NIC instead of opening a new one.
- One dedicated RSU acts as the fixed master; all vehicle gateways are slaves. Classic 4-timestamp PTP exchange, EWMA-smoothed delay estimate (α = 0.125), correction applied via `clock_settime`.
- A few extras beyond the spec: a fast pre-sync retry (250 ms) avoids a full 15 s wait if the boot-time exchange is lost; an outlier filter discards offsets ≥ 1 s as noise; a quadrant-change event hook (foreshadowing stage 4) triggers an immediate resync when the NIC detects the vehicle crossed into a new quadrant.

### Stage 4 — spatial synchronization (GPS kernel module)

An out-of-tree Linux kernel module (`kernel/gps_module/gps.c`) exposes a misc device at `/dev/gps` with two `ioctl`s: `GPS_IOC_GET_QUADRANT` (returns the VM's current quadrant 0–3, lazily advancing to a random *adjacent* quadrant once 3 seconds have elapsed) and `GPS_IOC_SET_FIXED` (freezes movement, used by RSUs). State is a single mutex-protected global per module load, so every process in a VM observes the identical quadrant.

- Userspace wrapper `GPS` (`src/network/gps.cpp/.h`) opens the device and exposes `quadrant()`/`set_fixed()`; it returns the sentinel `QUADRANT_NONE = 0xFF` if the module isn't loaded, so the NIC just skips spatial filtering when GPS isn't available.
- `Message::Origin` and `Protocol::Header` gain a `quadrant` field. The `NIC` stamps the sender's quadrant on every outgoing frame and drops incoming frames whose stamped quadrant doesn't match the receiver's own, via injected `read_quadrant`/`write_quadrant` function pointers that keep `NIC` and `Protocol` decoupled.

### Stage 5 — time-triggered publish-subscribe (SmartData)

A new layer, `SmartData<Type>`, sits above `Communicator` and implements Interest/Response messaging without explicit publication. `Type` parameterizes both a `Unit` (a TEDS/IEEE-1451-inspired type code, e.g. `SPEED`, `LIDAR_DISTANCE`, `GPS_POSITION`) and its associated value type, so `SmartData<Speed_Data>` is fully typed end to end.

- **Producer side**: `Binding_Cache` keeps soft-state per-(unit, requester-address) bindings with a lease and background reaper thread, running one `Periodic_Thread` per `Unit` at the *shortest* period currently requested by any interested consumer.
- **Consumer side**: reactive refresh, the Interest only gets re-sent after a window of silence, not on a fixed timer. There's also an optional value mode (`operator Value()`, `fresh()/expired()`) as an alternative to draining a response queue by hand.
- **RSU broker**: `Interest_Tracker` runs in the RSU's gateway process, passively listening to Interest traffic per quadrant, periodically re-announcing active interests, and detecting a consumer's departure purely from SPTP presence timeouts.
- **Quadrant transition handling**: a consumer that moves between the simulated quadrants introduced in stage 4 recovers communication through reactive Interest refresh and RSU re-announcement, with no explicit handover message. This is distinct from the authenticated group handoff specified in stages 6–7 (session-key re-authentication on group change), which was not implemented; see [Project status](#project-status).
- Validated with 10 scenarios in QEMU: fan-in, fan-out, per-unit demux, 20-producer period adherence, explicit and presence-based disinterest lifecycle, late-joiner catch-up via RSU repeat, quadrant-transition handling, and a 24-VM scale test combining all of the above.

## Repository structure

```
SO2/
├── makefile                 # single entry point: build, link, and run all QEMU-based tests
├── src/
│   ├── core/                 # Observer/Observed primitives, Buffer pool, Clock, Traits, RT_Priority
│   ├── network/               # Ethernet framing, NIC<Engine>, GPS userspace wrapper
│   │   └── engine/            # RawSocketEngine, SharedMemoryEngine, SHM region layout
│   ├── channel/                # Protocol<SHM_NIC, RawNIC>, SPTP_Protocol, PacketKind, Vehicle_Protocol alias
│   ├── communication/
│   │   ├── message/            # TypedMessage / MessageHeader (address, timestamp, quadrant, type)
│   │   ├── communicator.h      # blocking send/receive endpoint for application code
│   │   └── smart_data/         # stage 5: Interest/Response layer (SmartData, Binding_Cache, Interest_Tracker, Unit)
│   └── application/            # Vehicle, Gateway, RSU macro-objects + Component/Sensor/Actuator scaffolding
├── kernel/
│   ├── gps_module/              # out-of-tree Linux kernel module (misc device /dev/gps, quadrant ioctl)
│   ├── Image, initramfs.cpio    # prebuilt kernel + initramfs used to boot each QEMU test VM
├── tests/                       # 19 test scenarios + QEMU harness scripts
└── doc/                         # per-stage reports, slides and diagrams
    ├── Overview.pdf
    └── etapa{1..5}/
        ├── <report>.pdf
        ├── <slides>.pdf
        └── diagrams/
```

`kernel/Image`/`initramfs.cpio` and `kernel/gps_module/gps.ko` are committed prebuilt binaries (an explicit `.gitignore` exception) so `make` boots and runs every test without anyone needing a local kernel-build tree. Rebuilding `gps.c` itself is the only thing that requires one (`make gps-rebuild`).

## Project status

**Implemented:** Stages 1–5
**Not implemented:** Stages 6–7

- **Stage 6 — secure group communication**: per-message MAC (authenticity/integrity), dynamic group formation by geography with a session key distributed by an elected leader/RSU, and an explicit join-Interest message type.
- **Stage 7 — mobility & realistic simulation**: automatic re-authentication when a mobile agent crosses into a new group (building on stage 6's session keys), plus integration with the realistic automotive simulation the course provides.

## Academic context

This is the submission for **INE5424 — Operating Systems II** (UFSC, 2026/1). The assignment specification asks for a 7-stage reliable communication library for autonomous vehicle systems; `doc/Overview.pdf` has the architecture overview presented in class, and each stage's report, slides and diagrams live under `doc/etapa{1..5}/`, matching the commit that was actually submitted for grading at that stage.

Each stage was built on its own feature branch, validated against its QEMU scenarios, and merged into `main` once it passed.

## Authors

- Arthur Erpen
- Caetano Peruzzo
- Gabriel Herrera
