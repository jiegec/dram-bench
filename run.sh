#!/bin/sh
set -x -v
make && ./bandwidth_benchmark -s dramsim3 -c submodules/DRAMSim3/configs/DDR3_8Gb_x8_1866.ini -o results-ddr3 50000 | tee ddr3-dramsim3.log
make && ./bandwidth_benchmark -s ramulator2 -c DDR3_8Gb_x8_1866.yaml -o results-ddr3 50000 | tee ddr3-ramulator2.log
make && ./bandwidth_benchmark -s dramsim3 -c submodules/DRAMSim3/configs/DDR4_8Gb_x8_3200.ini -o results-ddr4 50000 | tee ddr4-dramsim3.log
make && ./bandwidth_benchmark -s ramulator2 -c DDR4_8Gb_x8_3200.yaml -o results-ddr4 50000 | tee ddr4-ramulator2.log
make && ./bandwidth_benchmark -s ramulator2 -c DDR5_8Gb_x8_3200.yaml -o results-ddr5 50000 | tee ddr5-ramulator2.log
