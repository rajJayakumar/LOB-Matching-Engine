# Phase 3 Performance Log

## Environment

All measurements are captured on the profiling VM, never on the macOS dev machine.

- **VM:** GCP `c4-standard-4` (x86-64, Intel Granite Rapids)
- **PMU level:** `standard` (core + L1/L2 + branch events; no LLC events)
- **OS:** Debian 13
- **Compiler:** GCC (system, Debian 13)
- **Build flags:** `-O3 -fno-omit-frame-pointer -march=native`
- **`OB_NATIVE_ARCH`:** ON (default for local/VM builds; OFF for portability)

### VM creation

```bash
gcloud compute instances create orderbook-perf \
  --zone=us-central1-c \
  --machine-type=c4-standard-4 \
  --performance-monitoring-unit=standard \
  --image-family=debian-13 --image-project=debian-cloud \
  --boot-disk-size=50GB --boot-disk-type=hyperdisk-balanced
```

### VM setup

```bash
sudo apt update && sudo apt install -y linux-perf build-essential cmake git
echo 'kernel.perf_event_paranoid=1' | sudo tee /etc/sysctl.d/99-perf.conf
sudo sysctl kernel.perf_event_paranoid=1
```

### Validated `perf stat` event list

`standard` PMU does **not** expose LLC events. Do not use `perf stat -d` or the generic
`cache-misses` alias (both resolve to the unsupported LLC counter). Use:

```bash
perf stat -e cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses <cmd>
```

This yields three key metrics:
- **IPC** = `instructions / cycles`
- **Branch-mispredict rate** = `branch-misses / branches`
- **L1 d-cache miss rate** = `L1-dcache-load-misses / L1-dcache-loads`

### Timer overhead

Timer overhead measured once and subtracted from per-op timings. Timed operations are guarded
against dead-code elimination with `DoNotOptimize` / `ClobberMemory`.

*(Value to be filled in Task 3.2.)*

---

## Baseline (Task 3.4)

*(To be filled after running on the VM.)*

---

## Running numbers log

| Task | Change | p50 (ns) | p99 (ns) | p99.9 (ns) | Throughput (msg/s) | L1 miss % | IPC | Branch miss % | Notes |
|------|--------|----------|----------|------------|--------------------|-----------|----|---------------|-------|
| 3.4  | Baseline | — | — | — | — | — | — | — | *(to be filled)* |
