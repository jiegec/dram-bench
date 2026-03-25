#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <ios>
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

// Ramulator2 includes
#include "addr_mapper/addr_mapper.h"
#include "base/base.h"
#include "base/config.h"
#include "dram/dram.h"
#include "memory_system/memory_system.h"

// Global state for callbacks (shared between both implementations)
static std::atomic<uint64_t> g_pendingTransactions(0);
static std::atomic<uint64_t> g_completedTransactions(0);
static std::atomic<uint64_t> g_totalBytesTransferred(0);

// Random access modes
enum class RandomAccessMode {
  Sequential,          // Sequential
  SameBGSameBASameRow, // Same bank group, same bank address, same row
  RandBGRandBARandRow, // Random bank group, random bank address, random row
  SameBGRandBASameRow, // Same bank group, random bank address, same row
  SameBGSameBARandRow, // Same bank group, same bank address, random row
  RandBGSameBASameRow  // Random bank group, same bank address, same row
};

// Address mapping modes
enum class AddressMappingMode {
  RoChRaBaBgCo, // Row, Channel, Rank, Bank, Bank Group, Column
  RoBaBgRaCoCh, // Row, Bank, Bank Group, Rank, Column, Channel
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

    // DRAM timing parameters
    int tRCD;   // ACT to internal read or write delay time
    int tRP;    // PRE command period
    int tRAS;   // ACT to PRE command period
    int tREFI;  // Average periodic refresh interval
    int tCCD;   //  CAS_n to CAS_n command delay
    int tCCD_S; // CAS_n to CAS_n command delay for same bank group
    int tCCD_L; // CAS_n to CAS_n command delay for different bank group
    int tFAW;   // Four activate window

    // Set default parameters
    DRAMConfig()
        : tCK(1.5), channels(1), ranks(1), bankGroups(1), banksPerGroup(8),
          rows(16384), columns(1024), busWidth(64), burstLength(8),
          addressMapping(AddressMappingMode::RoChRaBaBgCo), tRCD(0), tRP(0),
          tRAS(0), tREFI(0), tCCD(0), tCCD_S(0), tCCD_L(0), tFAW(0) {}

    // Compute theoretical bandwidth based on parameters
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
  virtual bool tryAddTransaction(uint64_t addr) = 0;
  virtual void clockTick() = 0;
  virtual void printStats() = 0;
  virtual std::string getSimulatorName() const = 0;

  void
  runBenchmark(uint64_t numTransactions,
               RandomAccessMode mode = RandomAccessMode::RandBGRandBARandRow) {
    std::string modeStr;
    switch (mode) {
    case RandomAccessMode::Sequential:
      modeStr = "Sequential";
      break;
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

    resetStats();
    runRandomBankLoop(numTransactions, mode);
    printStats();
  }

  void printDRAMConfig(const std::string &simulatorName) const {
    std::cout << "\n  DRAM Configuration (" << simulatorName
              << "):" << std::endl;
    std::cout << "    Clock Period (tCK): " << dramConfig.tCK << " ns"
              << std::endl;
    std::cout << "    Channels: " << dramConfig.channels << std::endl;
    std::cout << "    Ranks: " << dramConfig.ranks << std::endl;
    std::cout << "    Bank Groups: " << dramConfig.bankGroups << std::endl;
    std::cout << "    Banks per Group: " << dramConfig.banksPerGroup
              << std::endl;
    std::cout << "    Rows: " << dramConfig.rows << std::endl;
    std::cout << "    Columns: " << dramConfig.columns << std::endl;
    std::cout << "    Bus Width: " << dramConfig.busWidth << " bits/channel"
              << std::endl;
    std::cout << "    Burst Length: " << dramConfig.burstLength << std::endl;
    switch (dramConfig.addressMapping) {
    case AddressMappingMode::RoChRaBaBgCo:
      std::cout << "    Address Mapping: RoChRaBaBgCo" << std::endl;
      break;
    case AddressMappingMode::RoBaBgRaCoCh:
      std::cout << "    Address Mapping: RoBaBgRaCoCh" << std::endl;
      break;
    }
    std::cout << "    Theoretical Max Bandwidth: " << std::fixed
              << std::setprecision(2)
              << dramConfig.getTheoreticalBandwidthGBps() << " GB/s"
              << std::endl;
    std::cout << "  Timing Parameters:" << std::endl;
    std::cout << "    tRCD: " << dramConfig.tRCD << " cycles" << std::endl;
    std::cout << "    tRP: " << dramConfig.tRP << " cycles" << std::endl;
    std::cout << "    tRAS: " << dramConfig.tRAS << " cycles" << std::endl;
    std::cout << "    tREFI: " << dramConfig.tREFI << " cycles" << std::endl;
    std::cout << "    tCCD: " << dramConfig.tCCD << " cycles" << std::endl;
    std::cout << "    tCCD_S: " << dramConfig.tCCD_S << " cycles" << std::endl;
    std::cout << "    tCCD_L: " << dramConfig.tCCD_L << " cycles" << std::endl;
    std::cout << "    tFAW: " << dramConfig.tFAW << " cycles" << std::endl;
  }

protected:
  uint64_t endCycle;
  DRAMConfig dramConfig;

  // Generate random bank addresses
  // Unified benchmark loop with pre-generated addresses
  void runBenchmarkLoop(const std::vector<uint64_t> &addresses) {
    uint64_t currentCycle = 0;
    uint64_t transactionsIssued = 0;
    uint64_t numTransactions = addresses.size();

    while (transactionsIssued < numTransactions) {
      uint64_t addr = addresses[transactionsIssued];
      if (tryAddTransaction(addr)) {
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

  // Generate sequential addresses
  std::vector<uint64_t> generateSequential(uint64_t numTransactions) {
    std::vector<uint64_t> addresses;
    addresses.reserve(numTransactions);

    for (uint64_t i = 0; i < numTransactions; i++) {
      addresses.push_back(i * 64);
    }
    return addresses;
  }

  // Generate addresses: Same bank group, random bank, row
  std::vector<uint64_t> generateSameBGSameBASameRow(uint64_t numTransactions) {
    std::vector<uint64_t> addresses;
    addresses.reserve(numTransactions);

    uint64_t col = 0;
    for (uint64_t i = 0; i < numTransactions; i++) {
      addresses.push_back(composeAddress(
          0, 0, col / (dramConfig.columns / dramConfig.burstLength),
          col % (dramConfig.columns / dramConfig.burstLength)));
      col++;
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

    std::random_device rd;
    std::mt19937_64 gen(rd());

    for (uint64_t i = 0; i < numTransactions; i++) {
      bg.push_back(i % dramConfig.bankGroups);
    }
    std::shuffle(bg.begin(), bg.end(), gen);

    std::map<uint64_t, uint64_t> cols;
    for (uint64_t i = 0; i < numTransactions; i++) {
      uint64_t &col = cols[bg[i]];
      addresses.push_back(composeAddress(
          bg[i], 0, col / (dramConfig.columns / dramConfig.burstLength),
          col % (dramConfig.columns / dramConfig.burstLength)));
      col++;
    }
    return addresses;
  }

  // Compose physical address from bank group, bank, row, and column
  uint64_t composeAddress(uint64_t bankGroup, uint64_t bank, uint64_t row,
                          uint64_t col) const {
    // Column
    assert(col < dramConfig.columns / dramConfig.burstLength);
    int colBits = 0;
    int temp = dramConfig.columns / dramConfig.burstLength;
    while (temp > 1) {
      colBits++;
      assert(temp % 2 == 0);
      temp >>= 1;
    }

    // Bank Group
    assert(bankGroup < dramConfig.bankGroups);
    int bgBits = 0;
    temp = dramConfig.bankGroups;
    while (temp > 1) {
      bgBits++;
      assert(temp % 2 == 0);
      temp >>= 1;
    }

    // Bank
    int bankBits = 0;
    assert(bank < dramConfig.banksPerGroup);
    temp = dramConfig.banksPerGroup;
    while (temp > 1) {
      bankBits++;
      temp >>= 1;
    }

    // Rank
    int rankBits = 0;
    temp = dramConfig.ranks;
    while (temp > 1) {
      rankBits++;
      temp >>= 1;
    }

    if (dramConfig.addressMapping == AddressMappingMode::RoChRaBaBgCo) {
      // DRAM address mapping: [row][channel][rank][bank][bank group][column]
      uint64_t addr = 0;

      // Column is at the lowest bits (6 bits for 64-byte cache line)
      addr |= col << 6;

      // Bank group next (log2(bankGroups) bits)
      addr |= bankGroup << (6 + colBits);

      // Bank address next (log2(banksPerGroup) bits)
      addr |= bank << (6 + colBits + bgBits);

      // Rank next
      // Row is at the highest bits
      addr |= row << (6 + colBits + bankBits + rankBits + bgBits);

      return addr;
    } else if (dramConfig.addressMapping == AddressMappingMode::RoBaBgRaCoCh) {
      // DRAM address mapping: [row][bank][bank group][rank][column][channel]
      uint64_t addr = 0;

      // Channel is at the lowest bits (6 bits for 64-byte cache line)
      // Column next
      addr |= col << 6;

      // Rank next
      // Bank group next (log2(bankGroups) bits)
      addr |= bankGroup << (6 + colBits + rankBits);

      // Bank address next (log2(banksPerGroup) bits)
      addr |= bank << (6 + colBits + rankBits + bgBits);

      // Row is at the highest bits
      addr |= row << (6 + colBits + rankBits + bgBits + bankBits);

      return addr;
    } else {
      assert(false && "TODO");
    }
  }

  // Wrapper for random bank benchmark with mode selection
  void runRandomBankLoop(
      uint64_t numTransactions,
      RandomAccessMode mode = RandomAccessMode::RandBGRandBARandRow) {
    std::vector<uint64_t> addresses;

    switch (mode) {
    case RandomAccessMode::Sequential:
      addresses = generateSequential(numTransactions);
      break;
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

class DummyFrontend final : public Ramulator::IFrontEnd {
  void tick() {}
  bool is_finished() { return false; }
};

// Ramulator2 implementation
class Ramulator2Benchmark : public MemoryBenchmark {
private:
  Ramulator::IMemorySystem *mem;
  DummyFrontend *frontend;
  std::string outputFile;

  void readComplete(Ramulator::Request &req) {
    g_completedTransactions++;
    g_pendingTransactions--;
    g_totalBytesTransferred += 64;
  }

public:
  Ramulator2Benchmark(const std::string &configFile,
                      const std::string &outputDir) {
    // Parse the YAML configuration
    YAML::Node config = Ramulator::Config::parse_config_file(configFile, {});

    // Create the memory system
    mem = Ramulator::Factory::create_memory_system(config);
    frontend = new DummyFrontend;
    mem->connect_frontend(frontend);

    // Try to get DRAM timing and organization info
    auto dram = mem->get_ifce<Ramulator::IDRAM>();
    dramConfig.tCK = dram->m_timing_vals("tCK_ps") / 1000.0;
    dramConfig.channels = dram->get_level_size("channel");
    dramConfig.ranks = dram->get_level_size("rank");
    dramConfig.bankGroups = dram->get_level_size("bankgroup");
    if (dramConfig.bankGroups == (uint64_t)-1) {
      // nonexistent, e.g. DDR3
      dramConfig.bankGroups = 1;
    }
    dramConfig.banksPerGroup = dram->get_level_size("bank");
    dramConfig.rows = dram->get_level_size("row");
    dramConfig.columns = dram->get_level_size("column");

    // Read timing parameters (cycle counts)
    dramConfig.tRCD = dram->m_timing_vals("nRCD");
    dramConfig.tRP = dram->m_timing_vals("nRP");
    dramConfig.tRAS = dram->m_timing_vals("nRAS");
    dramConfig.tREFI = dram->m_timing_vals("nREFI");
    if (dram->m_timings.contains("nCCD")) {
      dramConfig.tCCD = dram->m_timing_vals("nCCD");
    }
    if (dram->m_timings.contains("nCCDS")) {
      dramConfig.tCCD_S = dram->m_timing_vals("nCCDS");
    }
    if (dram->m_timings.contains("nCCDL")) {
      dramConfig.tCCD_L = dram->m_timing_vals("nCCDL");
    }
    dramConfig.tFAW = dram->m_timing_vals("nFAW");

    // Read address mapping
    auto mapper = mem->get_ifce<Ramulator::IAddrMapper>();
    if (mapper->m_impl->get_name() == "RoBaRaCoCh") {
      // Order: RoBaBgRaCoCh according to source code
      dramConfig.addressMapping = AddressMappingMode::RoBaBgRaCoCh;
    } else {
      assert(false && "TODO");
    }

    outputFile = outputDir + "/ramulator2.yaml";
  }

  ~Ramulator2Benchmark() {
    delete mem;
    delete frontend;
  }

  bool tryAddTransaction(uint64_t addr) override {
    Ramulator::Request req(
        addr, Ramulator::Request::Type::Read, 0,
        [this](Ramulator::Request &r) { this->readComplete(r); });
    return mem->send(req);
  }

  void clockTick() override { mem->tick(); }

  void printStats() override {
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    mem->m_impl->print_stats(emitter);
    emitter << YAML::EndMap;
    std::fstream f(outputFile, std::ios_base::out | std::ios_base::trunc);
    f << emitter.c_str() << std::endl;
  }

  std::string getSimulatorName() const override { return "Ramulator2"; }
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
    dramConfig.tRCD = config.tRCD;
    dramConfig.tRP = config.tRP;
    dramConfig.tRAS = config.tRAS;
    dramConfig.tREFI = config.tREFI;
    dramConfig.tCCD = config.tCCD_S;
    dramConfig.tCCD_S = config.tCCD_S;
    dramConfig.tCCD_L = config.tCCD_L;
    dramConfig.tFAW = config.tFAW;

    if (config.address_mapping == "rochrababgco") {
      dramConfig.addressMapping = AddressMappingMode::RoChRaBaBgCo;
    } else if (config.address_mapping == "robabgracoch") {
      dramConfig.addressMapping = AddressMappingMode::RoBaBgRaCoCh;
    } else {
      assert(false && "Unrecognized address mapping");
    }
  }

  ~DRAMSim3Benchmark() { delete mem; }

  bool tryAddTransaction(uint64_t addr) override {
    if (mem->WillAcceptTransaction(addr, false)) {
      mem->AddTransaction(addr, false);
      return true;
    } else {
      return false;
    }
  }

  void clockTick() override { mem->ClockTick(); }

  void printStats() override { mem->PrintStats(); }

  std::string getSimulatorName() const override { return "DRAMSim3"; }
};

void printResults(const std::string &name,
                  const MemoryBenchmark::Stats &stats) {
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
}

void printUsage(const char *progName) {
  std::cout << "Usage: " << progName << " [OPTIONS] [NUM_TRANSACTIONS]"
            << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -s, --simulator SIM  Simulator to use: dramsim3, ramulator2 "
               "(default: dramsim3)"
            << std::endl;
  std::cout << "  -c, --config FILE    Configuration file (INI for dramsim3, "
               "YAML for ramulator2)"
            << std::endl;
  std::cout << "  -o, --output DIR     Output directory (default: results)"
            << std::endl;
  std::cout << "  -h, --help           Show this help message" << std::endl;
  std::cout << std::endl;
  std::cout << "Examples:" << std::endl;
  std::cout
      << "  " << progName
      << " -s dramsim3 -c submodules/DRAMSim3/configs/HBM2_8Gb_x128.ini 50000"
      << std::endl;
  std::cout
      << "  " << progName
      << " -s ramulator2 -c submodules/ramulator2/example_config.yaml 50000"
      << std::endl;
}

int main(int argc, char *argv[]) {
  std::cout << "========================================" << std::endl;
  std::cout << "Unified DRAM Bandwidth Benchmark" << std::endl;
  std::cout << "========================================" << std::endl;

  // Configuration
  std::string simulator = "dramsim3";

  // Defaults
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

  if (argIdx + 1 < argc) {
    std::cerr << "Unknown option after number of transactions: "
              << argv[argIdx + 1] << std::endl;
    printUsage(argv[0]);
    return 1;
  }

  std::cout << "\nConfiguration:" << std::endl;
  std::cout << "  Simulator: " << simulator << std::endl;
  std::cout << "  Transactions: " << numTransactions << std::endl;
  std::cout << "  Config: " << configFile << std::endl;
  if (simulator != "dramsim3" && simulator != "ramulator2") {
    std::cerr << "Error: Unknown simulator '" << simulator
              << "'. Use 'dramsim3' or 'ramulator2'." << std::endl;
    return 1;
  }

  try {
    std::unique_ptr<MemoryBenchmark> benchmark;

    // Run all random access modes
    std::vector<std::pair<std::string, RandomAccessMode>> randomModes = {
        {"Sequential", RandomAccessMode::Sequential},
        {"SameBGSameBASameRow", RandomAccessMode::SameBGSameBASameRow},
        {"RandBGRandBARandRow", RandomAccessMode::RandBGRandBARandRow},
        {"SameBGRandBASameRow", RandomAccessMode::SameBGRandBASameRow},
        {"SameBGSameBARandRow", RandomAccessMode::SameBGSameBARandRow},
        {"RandBGSameBASameRow", RandomAccessMode::RandBGSameBASameRow},
    };

    int scenairo = 1;
    for (std::pair<std::string, RandomAccessMode> randomMode : randomModes) {
      // create output folders
      std::string realOutputDir = outputDir + "/" + randomMode.first;
      mkdir(outputDir.c_str(), 0755);
      mkdir(realOutputDir.c_str(), 0755);

      if (simulator == "dramsim3") {
        benchmark = std::unique_ptr<MemoryBenchmark>(
            new DRAMSim3Benchmark(configFile, realOutputDir));
      } else if (simulator == "ramulator2") {
        benchmark = std::unique_ptr<MemoryBenchmark>(
            new Ramulator2Benchmark(configFile, realOutputDir));
      }

      if (scenairo == 1) {
        benchmark->printDRAMConfig(simulator);
      }

      // Scenario i: Sequential Access
      std::cout << "\n" << std::string(50, '=') << std::endl;
      std::cout << "SCENARIO " << scenairo++ << ": " << randomMode.first
                << std::endl;
      std::cout << std::string(50, '=') << std::endl;

      benchmark->runBenchmark(numTransactions, randomMode.second);
      auto randStats = benchmark->getStats();
      printResults(randomMode.first, randStats);
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "\nBenchmark Complete!" << std::endl;
  return 0;
}
