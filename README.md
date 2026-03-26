# DRAM Bandwidth Testing in DRAM Simulators

This is a small hobby project I put together to play around with DRAM simulators. I wanted to see how memory bandwidth differs across various configurations and compare two popular simulators: DRAMSim3 and Ramulator2.

## What This Project Does

Basically it runs a bunch of memory access tests and checks bandwidth utilization. Each pattern runs some transactions, then we see how much the actual bandwidth differs from theoretical maximum.

## Building

```bash
git clone --recursive https://github.com/jiegec/dram-bench
cd dram-bench
make
```

First build takes a while since it needs to fetch submodules and build Ramulator2's static library.

## Usage

Simplest way is to just run:

```bash
./run.sh
```

This runs DDR3 and DDR4 tests on both simulators sequentially.

To run a specific configuration:

```bash
./bandwidth_benchmark -s dramsim3 -c submodules/DRAMSim3/configs/DDR3_8Gb_x8_1866.ini 50000
./bandwidth_benchmark -s ramulator2 -c DDR4_8Gb_x8_3200.yaml 50000
```

## Limitations

This is just a toy project for personal use, so:

- Only tests read operations, not writes (adding writes is simple, just a few lines)
- Ramulator2 uses RoBaRaCoCh address mapping by default, while DRAMSim3 uses RoChRaBaBgCo. Keep this in mind when comparing results.
- No visualization, just text output
- No systematic validation of simulator accuracy

## Related Blog Post

This project was used for experiments in a blog post (written in Chinese): <https://jia.je/hardware/2026/03/26/sdram-bandwidth/>.
