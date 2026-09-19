# Week 1 — environment, build, and QEMU notes

## Toolchain (no sudo, no apt)

Everything lives in `tools/` (gitignored). Before any build:

```bash
source tools/env.sh
```

- `tools/bin/riscv64-unknown-elf-*` are shims onto the xPack
  `riscv-none-elf-*` toolchain (GCC 15.2.0, GDB 16.3) — the Makefile's
  `TOOLPREFIX` probe finds them by their xv6-style prefix.
- `tools/xpack-qemu-riscv-9.2.4-1/bin` provides `qemu-system-riscv64`.

## Build flags we had to add (do not remove)

1. `-mabi=lp64` (CFLAGS + .S rule). xPack GCC defaults to the RV32 ABI, so
   plain `-march=rv64gc` fails with `cc1: error: ABI requires '-march=rv32'`.
2. `-m elf64lriscv` in `LDFLAGS`. xPack `ld` defaults to the `elf32-littleriscv`
   emulation and either refuses to merge 64-bit objects (`ABI is incompatible
   with that of the selected emulation`) or segfaults.
3. `check-qemu-version` compares versions with `sort -V` instead of `bc`
   (not installed on stock WSL).

## Full build from a clean tree

```bash
source tools/env.sh
make clean && make -j$(nproc)     # kernel/kernel + all user progs + mkfs/mkfs
make fs.img                       # 2000-block image; mkfs prints nmeta/balloc info
make qemu CPUS=1
```

`mkfs/mkfs` is built with the host `gcc` (it runs on WSL, not on the board).

## QEMU invocation (what `make qemu` expands to)

```
qemu-system-riscv64 \
  -machine virt -bios none -kernel kernel/kernel -m 128M \
  -smp $(CPUS) -nographic \
  -global virtio-mmio.force-legacy=false \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
```

- `-bios none`: xv6 ships its own `-kernel` boot path (entry.S → start.c).
- `-m 128M`: RAM size; swap work in week 7 will bound against this.
- `virtio-mmio.force-legacy=false` + `virtio-blk-device`: the disk is a
  modern (v2) virtio-blk over mmio — matches virtio_disk.c.
- Exit QEMU with `Ctrl-a x`.

## CPUS behavior

- Makefile default is `CPUS=3`.
- **`CPUS=1` for daily work**: TCG-only WSL (no KVM in WSL2) makes SMP slow
  and timing-dependent; single-hart runs are the most deterministic and all
  baseline numbers are recorded at CPUS=1.
- **`CPUS=2` for SMP gates** (lock-order checks, race hunts): still TCG, so
  treat timings as relative, never absolute ns. Observed deltas (see
  `bench/baseline.txt`): syscall ~13% slower, fork ~22% faster, create ~19%
  slower vs CPUS=1 — plausibly the second hart absorbing interrupts/fs work
  while the bench hart spins less, within TCG noise. Re-measure per release.

## Tests

```bash
./test-xv6.py usertests        # full usertests under QEMU without typing
./test-xv6.py -q usertests     # quick subset
```

## Baseline numbers

Live in `bench/baseline.txt` (syscall, fork, open/close, create/unlink,
512-byte write/read throughput, usertests wall time). Produced by `bench`
(`user/bench.c`, in UPROGS): run `bench` at the xv6 prompt, CPUS=1 and CPUS=2.
These are the before/after reference for every optimization in later weeks.
