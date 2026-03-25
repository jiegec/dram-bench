#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sys/stat.h>
#include <tuple>
#include <vector>

// DRAMSim3 includes
#include "configuration.h"
#include "dramsim3.h"

// Global state for callbacks (shared between both implementations)
static std::atomic<uint64_t> g_pendingTransactions(0);
static std::atomic<uint64_t> g_completedTransactions(0);
static std::atomic<uint64_t> g_totalBytesTransferred(0);

// Random access modes
enum class RandomAccessMode {
  SameBGSameBASameRow, // Same bank group, same bank address, same row
  RandBGRandBARandRow, // Random bank group, random bank address, random row
  SameBGRandBASameRow, // Same bank group, random bank address, same row
  SameBGSameBARandRow, // Same bank group, same bank address, random row
  RandBGSameBASameRow  // Random bank group, same bank address, same row
};

// Address mapping modes
enum class AddressMappingMode {
  RoChRaBaBgCo, // Row, Channel, Rank, Bank, Bank Group, Column
};

// Abstract base class for memory benchmarks
class MemoryBenchmark {
public:
  struct DRAMConfig {
    double tCK;                        // Clock period
    uint64_t channels;                 // Number of channels
    uint64_t ranks;                    // Number of ranks
    uint64_t bankGroups;               // Number of bank groups (DDR4+)
    uint64_t banksPerGroup;            // Banks per group
    uint64_t rows;                     // Number of rows per bank
    uint64_t columns;                  // Number of columns
    uint64_t busWidth;                 // Data bus width
    uint64_t burstLength;              // Burst length
    AddressMappingMode addressMapping; // Address mapping

    // Set default parameters
    DRAMConfig()
        : tCK(1.5), channels(1), ranks(1), bankGroups(1), banksPerGroup(8),
          rows(16384), columns(1024), busWidth(64), burstLength(8),
          addressMapping(AddressMappingMode::RoChRaBaBgCo) {}

    // Compute theoretical bandwidth based on paarmeters
    double getTheoreticalBandwidthGBps() const {
      double transfersPerSec = 2.0 / (tCK * 1e-9);
      double bytesPerTransfer = busWidth / 8.0;
      double bandwidthBps = channels * transfersPerSec * bytesPerTransfer;
      return bandwidthBps / 1e9;
    }
  };

  struct Stats {
    uint64_t totalBytes;
    uint64_t totalCycles;
    uint64_t transactionsCompleted;
    double bandwidthGBps;
    double cycleTimeNs;
    DRAMConfig config;
  };

  virtual ~MemoryBenchmark() = default;

  void resetStats() {
    g_pendingTransactions = 0;
    g_completedTransactions = 0;
    g_totalBytesTransferred = 0;
  }

  Stats getStats() const {
    Stats s;
    s.totalBytes = g_totalBytesTransferred.load();
    s.totalCycles = endCycle;
    s.transactionsCompleted = g_completedTransactions.load();
    s.cycleTimeNs = dramConfig.tCK;
    s.config = dramConfig;

    double totalTimeNs = s.totalCycles * dramConfig.tCK;
    double totalTimeS = totalTimeNs / 1e9;

    if (totalTimeS > 0) {
      s.bandwidthGBps = (s.totalBytes / 1e9) / totalTimeS;
    } else {
      s.bandwidthGBps = 0;
    }

    return s;
  }

  DRAMConfig getDRAMConfig() const { return dramConfig; }

  // Virtual methods for simulator-specific operations
  virtual bool willAcceptTransaction(uint64_t addr) = 0;
  virtual void addTransaction(uint64_t addr) = 0;
  virtual void clockTick() = 0;
  virtual void printStats() = 0;
  virtual std::string getSimulatorName() const = 0;

  void
  runBenchmark(uint64_t numTransactions,
               RandomAccessMode mode = RandomAccessMode::RandBGRandBARandRow) {
    std::string modeStr;
    switch (mode) {
    case RandomAccessMode::SameBGSameBASameRow:
      modeStr = "Same Bank Group, Bank, Row";
      break;
    case RandomAccessMode::RandBGRandBARandRow:
      modeStr = "Random Bank Group, Bank, Row";
      break;
    case RandomAccessMode::SameBGRandBASameRow:
      modeStr = "Same Bank Group, Random Bank, Same Row";
      break;
    case RandomAccessMode::SameBGSameBARandRow:
      modeStr = "Same Bank Group & Bank, Random Row";
      break;
    case RandomAccessMode::RandBGSameBASameRow:
      modeStr = "Random Bank Group, Same Bank, Same Row";
      break;
    }
    std::cout << "\n=== " << modeStr << " Benchmark (" << getSimulatorName()
              << ") ===" << std::endl;
    std::cout << "Transactions: " << numTransactions << std::endl;

    resetStats();
    runRandomBankLoop(numTransactions, mode);
    printStats();
  }

protected:
  uint64_t endCycle;
  DRAMConfig dramConfig;

  void printDRAMConfig(const std::string &simulatorName) const {
    std::cout << "\n  DRAM Configuration (" << simulatorName
              << "):" << std::endl;
    std::cout << "    Channels: " << dramConfig.channels << std::endl;
    std::cout << "    Data Bus: " << dramConfig.busWidth << " bits/channel"
              << std::endl;
    std::cout << "    Burst Length: " << dramConfig.burstLength << std::endl;
    std::cout << "    Bank Groups: " << dramConfig.bankGroups << std::endl;
    std::cout << "    Banks per Group: " << dramConfig.banksPerGroup
              << std::endl;
    std::cout << "    Clock Period (tCK): " << dramConfig.tCK << " ns"
              << std::endl;
    std::cout << "    Theoretical Max Bandwidth: " << std::fixed
              << std::setprecision(2)
              << dramConfig.getTheoreticalBandwidthGBps() << " GB/s"
              << std::endl;
  }

  // Generate random bank addresses
  // Unified benchmark loop with pre-generated addresses
  void runBenchmarkLoop(const std::vector<uint64_t> &addresses) {
    uint64_t currentCycle = 0;
    uint64_t transactionsIssued = 0;
    uint64_t numTransactions = addresses.size();

    while (transactionsIssued < numTransactions) {
      uint64_t addr = addresses[transactionsIssued];
      if (willAcceptTransaction(addr)) {
        addTransaction(addr);
        g_pendingTransactions++;
        transactionsIssued++;
      }
      clockTick();
      currentCycle++;
    }

    while (g_pendingTransactions > 0) {
      clockTick();
      currentCycle++;

      if (currentCycle > 10000000) {
        std::cout << "Warning: Simulation timeout at cycle " << currentCycle
                  << std::endl;
        break;
      }
    }

    endCycle = currentCycle;
  }

  // Generate addresses: Same bank group, random bank, row
  std::vector<uint64_t> generateSameBGSameBASameRow(uint64_t numTransactions) {
    std::vector<uint64_t> addresses;
    addresses.reserve(numTransactions);
    for (uint64_t i = 0; i < numTransactions; i++) {
      addresses.push_back(i * 64);
    }
    return addresses;
  }

  // Generate addresses: Random bank group, random bank, row
  std::vector<uint64_t> generateRandBGRandBARandRow(uint64_t numTransactions) {
    std::vector<uint64_t> addresses;
    addresses.reserve(numTransactions);

    std::vector<uint64_t> bg;
    std::vector<uint64_t> ba;
    std::vector<uint64_t> ro;

    std::random_device rd;
    std::mt19937_64 gen(rd());

    for (uint64_t i = 0; i < numTransactions; i++) {
      bg.push_back(i % dramConfig.bankGroups);
      ba.push_back(i % dramConfig.banksPerGroup);
      ro.push_back(i % dramConfig.rows);
    }
    std::shuffle(bg.begin(), bg.end(), gen);
    std::shuffle(ba.begin(), ba.end(), gen);
    std::shuffle(ro.begin(), ro.end(), gen);

    std::map<std::tuple<uint64_t, uint64_t, uint64_t>, uint64_t> cols;
    for (uint64_t i = 0; i < numTransactions; i++) {
      uint64_t &col = cols[std::make_tuple(bg[i], ba[i], ro[i])];
      addresses.push_back(composeAddress(bg[i], ba[i], ro[i], col));
      col++;
    }
    return addresses;
  }

  // Generate addresses: Same bank group, random bank, same row
  std::vector<uint64_t> generateSameBGRandBASameRow(uint64_t numTransactions) {
    std::vector<uint64_t> addresses;
    addresses.reserve(numTransactions);

    std::vector<uint64_t> ba;

    std::random_device rd;
    std::mt19937_64 gen(rd());

    for (uint64_t i = 0; i < numTransactions; i++) {
      ba.push_back(i % dramConfig.banksPerGroup);
    }
    std::shuffle(ba.begin(), ba.end(), gen);

    std::map<uint64_t, uint64_t> cols;
    for (uint64_t i = 0; i < numTransactions; i++) {
      uint64_t &col = cols[ba[i]];
      addresses.push_back(composeAddress(
          0, ba[i], col / (dramConfig.columns / dramConfig.burstLength),
          col % (dramConfig.columns / dramConfig.burstLength)));
      col++;
    }
    return addresses;
  }

  // Generate addresses: Same bank group, same bank, random row
  std::vector<uint64_t> generateSameBGSameBARandRow(uint64_t numTransactions) {
    std::vector<uint64_t> addresses;
    addresses.reserve(numTransactions);

    std::vector<uint64_t> ro;

    std::random_device rd;
    std::mt19937_64 gen(rd());

    for (uint64_t i = 0; i < numTransactions; i++) {
      ro.push_back(i % dramConfig.rows);
    }
    std::shuffle(ro.begin(), ro.end(), gen);

    std::map<uint64_t, uint64_t> cols;
    for (uint64_t i = 0; i < numTransactions; i++) {
      uint64_t &col = cols[ro[i]];
      addresses.push_back(composeAddress(0, 0, ro[i], col));
      col++;
    }
    return addresses;
  }

  // Generate addresses: Random bank group, same bank, same row
  std::vector<uint64_t> generateRandBGSameBASameRow(uint64_t numTransactions) {
    std::vector<uint64_t> addresses;
    addresses.reserve(numTransactions);

    std::vector<uint64_t> bg;
    std::vector<uint64_t> ba;

    std::random_device rd;
    std::mt19937_64 gen(rd());

    for (uint64_t i = 0; i < numTransactions; i++) {
      bg.push_back(i % dramConfig.bankGroups);
      ba.push_back(0);
    }
    std::shuffle(bg.begin(), bg.end(), gen);
    std::shuffle(ba.begin(), ba.end(), gen);

    std::map<std::tuple<uint64_t, uint64_t>, uint64_t> cols;
    for (uint64_t i = 0; i < numTransactions; i++) {
      uint64_t &col = cols[std::make_tuple(bg[i], ba[i])];
      addresses.push_back(composeAddress(
          bg[i], ba[i], col / (dramConfig.columns / dramConfig.burstLength),
          col % (dramConfig.columns / dramConfig.burstLength)));
      col++;
    }
    return addresses;
  }

  // Compose physical address from bank group, bank, row, and column
  uint64_t composeAddress(uint64_t bankGroup, uint64_t bank, uint64_t row,
                          uint64_t col) const {
    if (dramConfig.addressMapping == AddressMappingMode::RoChRaBaBgCo) {
      // DRAM address mapping: [row][channel][rank][bank][bank group][column]
      uint64_t addr = 0;

      // Column is at the lowest bits (6 bits for 64-byte cache line)
      assert(col < dramConfig.columns / dramConfig.burstLength);
      int colBits = 0;
      int temp = dramConfig.columns / dramConfig.burstLength;
      while (temp > 1) {
        colBits++;
        assert(temp % 2 == 0);
        temp >>= 1;
      }
      addr |= col << 6;

      // Bank group next (log2(bankGroups) bits)
      assert(bankGroup < dramConfig.bankGroups);
      int bgBits = 0;
      temp = dramConfig.bankGroups;
      while (temp > 1) {
        bgBits++;
        assert(temp % 2 == 0);
        temp >>= 1;
      }
      addr |= bankGroup << (6 + colBits);

      // Bank address next (log2(banksPerGroup) bits)
      int bankBits = 0;
      assert(bank < dramConfig.banksPerGroup);
      temp = dramConfig.banksPerGroup;
      while (temp > 1) {
        bankBits++;
        temp >>= 1;
      }
      addr |= bank << (6 + colBits + bgBits);

      // Rank next
      int rankBits = 0;
      temp = dramConfig.ranks;
      while (temp > 1) {
        rankBits++;
        temp >>= 1;
      }

      // Row is at the highest bits
      addr |= row << (6 + colBits + bankBits + rankBits + bgBits);

      return addr;
    } else {
      assert(false);
    }
  }

  // Wrapper for random bank benchmark with mode selection
  void runRandomBankLoop(
      uint64_t numTransactions,
      RandomAccessMode mode = RandomAccessMode::RandBGRandBARandRow) {
    std::vector<uint64_t> addresses;

    switch (mode) {
    case RandomAccessMode::SameBGSameBASameRow:
      addresses = generateSameBGSameBASameRow(numTransactions);
      break;
    case RandomAccessMode::RandBGRandBARandRow:
      addresses = generateRandBGRandBARandRow(numTransactions);
      break;
    case RandomAccessMode::SameBGRandBASameRow:
      addresses = generateSameBGRandBASameRow(numTransactions);
      break;
    case RandomAccessMode::SameBGSameBARandRow:
      addresses = generateSameBGSameBARandRow(numTransactions);
      break;
    case RandomAccessMode::RandBGSameBASameRow:
      addresses = generateRandBGSameBASameRow(numTransactions);
      break;
    }

    // No duplication
    assert(addresses.size() == numTransactions);

    runBenchmarkLoop(addresses);
  }
};

// DRAMSim3 implementation
class DRAMSim3Benchmark : public MemoryBenchmark {
private:
  dramsim3::MemorySystem *mem;

  void readComplete(uint64_t address) {
    g_completedTransactions++;
    g_pendingTransactions--;
    g_totalBytesTransferred += 64;
  }

  void writeComplete(uint64_t address) {
    g_completedTransactions++;
    g_pendingTransactions--;
    g_totalBytesTransferred += 64;
  }

public:
  DRAMSim3Benchmark(const std::string &configFile,
                    const std::string &outputDir) {
    auto readCB = [this](uint64_t addr) { this->readComplete(addr); };
    auto writeCB = [this](uint64_t addr) { this->writeComplete(addr); };

    dramsim3::Config config(configFile, outputDir);

    mem = dramsim3::GetMemorySystem(configFile, outputDir, readCB, writeCB);

    dramConfig.tCK = mem->GetTCK();
    dramConfig.channels = config.channels;
    dramConfig.ranks = config.ranks;
    dramConfig.bankGroups = config.bankgroups;
    dramConfig.banksPerGroup = config.banks_per_group;
    dramConfig.rows = config.rows;
    dramConfig.columns = config.columns;
    dramConfig.busWidth = config.bus_width;
    dramConfig.burstLength = config.BL;

    if (config.address_mapping == "rochrababgco") {
      dramConfig.addressMapping = AddressMappingMode::RoChRaBaBgCo;
    } else {
      assert(false && "Unrecognized address mapping");
    }

    printDRAMConfig("DRAMSim3");
  }

  ~DRAMSim3Benchmark() { delete mem; }

  bool willAcceptTransaction(uint64_t addr) override {
    return mem->WillAcceptTransaction(addr, false);
  }

  void addTransaction(uint64_t addr) override {
    mem->AddTransaction(addr, false);
  }

  void clockTick() override { mem->ClockTick(); }

  void printStats() override { mem->PrintStats(); }

  std::string getSimulatorName() const override { return "DRAMSim3"; }
};

void printResults(const std::string &name, const MemoryBenchmark::Stats &stats,
                  std::chrono::milliseconds duration) {
  double efficiency = 0.0;
  if (stats.config.getTheoreticalBandwidthGBps() > 0) {
    efficiency =
        (stats.bandwidthGBps / stats.config.getTheoreticalBandwidthGBps()) *
        100.0;
  }

  std::cout << "\n" << name << " Results:" << std::endl;
  std::cout << "  Transactions Completed: " << stats.transactionsCompleted
            << std::endl;
  std::cout << "  Total Bytes: " << stats.totalBytes << " bytes (" << std::fixed
            << std::setprecision(2) << stats.totalBytes / (1024.0 * 1024.0)
            << " MB)" << std::endl;
  std::cout << "  Total Cycles: " << stats.totalCycles << std::endl;
  std::cout << "  Cycle Time: " << stats.cycleTimeNs << " ns" << std::endl;
  std::cout << "  Measured Bandwidth: " << std::fixed << std::setprecision(2)
            << stats.bandwidthGBps << " GB/s" << std::endl;
  std::cout << "  Theoretical Max: " << std::fixed << std::setprecision(2)
            << stats.config.getTheoreticalBandwidthGBps() << " GB/s"
            << std::endl;
  std::cout << "  Efficiency: " << std::fixed << std::setprecision(1)
            << efficiency << "%" << std::endl;
  std::cout << "  Simulation Time: " << duration.count() << " ms" << std::endl;
}

void printUsage(const char *progName) {
  std::cout << "Usage: " << progName << " [OPTIONS] [NUM_TRANSACTIONS]"
            << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -s, --simulator SIM  Simulator to use: dramsim3 "
               "(default: dramsim3)"
            << std::endl;
  std::cout << "  -c, --config FILE    DRAMSim3: Configuration file"
            << std::endl;
  std::cout
      << "  -o, --output DIR     DRAMSim3: Output directory (default: results)"
      << std::endl;
  std::cout << "  -h, --help           Show this help message" << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout
      << "  " << progName
      << " -s dramsim3 -c submodules/DRAMSim3/configs/HBM2_8Gb_x128.ini 50000"
      << std::endl;
}

int main(int argc, char *argv[]) {
  std::cout << "========================================" << std::endl;
  std::cout << "Unified DRAM Bandwidth Benchmark" << std::endl;
  std::cout << "========================================" << std::endl;

  // Configuration
  std::string simulator = "dramsim3";

  // DRAMSim3 defaults
  std::string configFile = "submodules/DRAMSim3/configs/DDR4_8Gb_x8_2666.ini";
  std::string outputDir = "results";

  uint64_t numTransactions = 10000;

  // Parse command line arguments
  int argIdx = 1;
  while (argIdx < argc && argv[argIdx][0] == '-') {
    std::string arg = argv[argIdx];

    if (arg == "-h" || arg == "--help") {
      printUsage(argv[0]);
      return 0;
    } else if ((arg == "-s" || arg == "--simulator") && argIdx + 1 < argc) {
      simulator = argv[++argIdx];
    } else if ((arg == "-c" || arg == "--config") && argIdx + 1 < argc) {
      configFile = argv[++argIdx];
    } else if ((arg == "-o" || arg == "--output") && argIdx + 1 < argc) {
      outputDir = argv[++argIdx];
    } else {
      std::cerr << "Unknown option: " << arg << std::endl;
      printUsage(argv[0]);
      return 1;
    }
    argIdx++;
  }

  // Parse number of transactions
  if (argIdx < argc) {
    numTransactions = std::stoull(argv[argIdx]);
  }

  std::cout << "\nConfiguration:" << std::endl;
  std::cout << "  Simulator: " << simulator << std::endl;
  std::cout << "  Transactions: " << numTransactions << std::endl;
  if (simulator == "dramsim3") {
    std::cout << "  Config: " << configFile << std::endl;
    std::cout << "  Output: " << outputDir << std::endl;
  } else {
    std::cerr << "Error: Unknown simulator '" << simulator
              << "'. Use 'dramsim3'." << std::endl;
    return 1;
  }

  try {
    std::unique_ptr<MemoryBenchmark> benchmark;

    // Run all random access modes
    std::vector<std::pair<std::string, RandomAccessMode>> randomModes = {
        {"SameBGSameBASameRow", RandomAccessMode::SameBGSameBASameRow},
        {"RandBGRandBARandRow", RandomAccessMode::RandBGRandBARandRow},
        {"SameBGRandBASameRow", RandomAccessMode::SameBGRandBASameRow},
        {"SameBGSameBARandRow", RandomAccessMode::SameBGSameBARandRow},
        {"RandBGSameBASameRow", RandomAccessMode::RandBGSameBASameRow},
    };

    int scenairo = 1;
    for (std::pair<std::string, RandomAccessMode> randomMode : randomModes) {
      if (simulator == "dramsim3") {
        std::string realOutputDir = outputDir + "/" + randomMode.first;
        mkdir(realOutputDir.c_str(), 0755);
        benchmark = std::unique_ptr<MemoryBenchmark>(
            new DRAMSim3Benchmark(configFile, realOutputDir));
      }

      // Scenario i: Sequential Access
      std::cout << "\n" << std::string(50, '=') << std::endl;
      std::cout << "SCENARIO " << scenairo++ << ": " << randomMode.first
                << std::endl;
      std::cout << std::string(50, '=') << std::endl;

      auto start = std::chrono::high_resolution_clock::now();
      benchmark->runBenchmark(numTransactions, randomMode.second);
      auto end = std::chrono::high_resolution_clock::now();
      auto randStats = benchmark->getStats();
      auto randDuration =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
      printResults(randomMode.first, randStats, randDuration);
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "\nBenchmark Complete!" << std::endl;
  return 0;
}
