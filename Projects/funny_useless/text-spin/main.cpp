#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <atomic>
#include <vector>
#include <latch>
#include <numeric>

std::string shiftLeft(const std::string& str, int amount = 1) {
    if(str.empty()) return "";

    // Interpret negative amount as a shift to the right
    // Wrap just to be safe
    int n = static_cast<int>(str.size());
    amount = (amount % n + n) % n;
    return str.substr(amount, str.size() - amount) + str.substr(0, amount);
}

struct Batch {
    bool spinToRight = false;
    int updateDelay = 100;
    int cycleLength = -1;
    std::vector<std::string> stringsToCycle = {};
};

void processRunState(std::atomic<bool>& running, std::latch& startLatch) {
    startLatch.arrive_and_wait();

    std::cin.get();

    running.store(false);
}

void processBatch(size_t id,
                  std::vector<std::atomic<int>>& buffer,
                  const std::vector<Batch>& batches,
                  std::atomic<bool>& running,
                  std::latch& startLatch) {
    startLatch.count_down();

    if(batches[id].stringsToCycle.empty()) return;

    int amount = 0;
    int direction = batches[id].spinToRight ? -1 : 1;

    startLatch.wait();

    while(running.load()) {
        buffer[id].store(amount * direction, std::memory_order_relaxed);

        std::this_thread::sleep_for(std::chrono::milliseconds(batches[id].updateDelay));

        if(batches[id].cycleLength <= 0) {
            throw std::runtime_error("Invalid cycle length.");
        }
        amount = (amount + 1) % batches[id].cycleLength;
    }
}

void processAll(const std::vector<Batch>& batches) {
    std::vector<std::thread> threads;
    threads.reserve(batches.size() + 1);

    std::vector<std::atomic<int>> buffer(batches.size());
    std::latch startLatch(batches.size() + 1);
    std::atomic<bool> running = true;

    for(size_t i = 0; i < batches.size(); ++i) {
        threads.emplace_back(processBatch,
                             i,
                             std::ref(buffer),
                             std::ref(batches),
                             std::ref(running),
                             std::ref(startLatch));
    }

    threads.emplace_back(processRunState,
                         std::ref(running),
                         std::ref(startLatch));

    size_t lines = 0;

    for(size_t id = 0; id < batches.size(); ++id) {
        lines += batches[id].stringsToCycle.size();
    }

    startLatch.wait();

    while(true) {
        for(size_t id = 0; id < batches.size(); ++id) {
            for(const auto& str : batches[id].stringsToCycle) {
                std::cout << "\033[2K\r" << shiftLeft(str, buffer[id].load(std::memory_order_relaxed)) << "\n";
            }
        }

        std::cout.flush();

        if(!running.load()) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        // move cursor to start
        std::cout << "\033[" << lines << "A";
    }

    for(auto& t : threads) {
        t.join();
    }
}

void setupCanvas(const std::vector<Batch>& batches) {
    size_t lines = 0;

    for(size_t i = 0; i < batches.size(); ++i) {
        lines += batches[i].stringsToCycle.size();
    }

    // Reserve terminal space
    for(size_t i = 0; i < lines; ++i) {
        std::cout << '\n';
    }

    // Move back to the starting position
    if(lines != 0) std::cout << "\033[" << lines << "A";
}

void calculateCycleLength(std::vector<Batch>& batches) {
    for(auto& batch : batches) {
        // Padding for batches with a custom cycleLength
        if(batch.cycleLength != -1) {
            for(auto& str : batch.stringsToCycle) {
                str.resize(batch.cycleLength, ' ');
            }
            continue;
        }

        for(const auto& str : batch.stringsToCycle) {
            if(str.empty()) continue;

            batch.cycleLength = std::lcm(batch.cycleLength, static_cast<int>(str.size()));
        }
    }
}

void printHelpMessage() {
    constexpr int FLAG_WIDTH = 20;

    auto printFlag = [](const std::string& flag, const std::string& desc) {
        std::cout << "  "
                  << std::left << std::setw(FLAG_WIDTH) << flag
                  << desc << '\n';
    };

    std::cout << "DESCRIPTION:\n"
              << "A simple tool to make text spin.\n\n";

    std::cout << "FLAGS:\n";

    printFlag("--help", "Show this help message.");
    std::cout << '\n';

    printFlag("-r, --right", "Set the spin direction to 'right'");
    std::cout << '\n';

    printFlag("-s, --speed", "Set the delay between each shift cycle (in ms).");
    std::cout << "                      Default: 100\n\n";

    printFlag("-l, --length", "Set the length of the strings.");
    std::cout << "                      Default: Use given strings' length\n\n";

    printFlag(":", "Used to separate groups for e.g. applying different settings.");

    std::cout
        << "EXAMPLE USAGE:\n"
        << "  ./text-spin 'text'\n\n"
        << "  ./text-spin -s 300 'text1' 'text2'\n\n"
        << "  ./text-spin -r -l 2 'text1' : -s 50 'text2'\n\n"
        << "Note: Hit [Enter] to stop the execution\n";
}

// TODO: if there are too many lines, they start printing into stdin (terminal command line)
int main(int argc, char* argv[]) {
    if(argc == 1) {
        std::cerr << "Text-spin: No arguments provided. Try './text-spin --help' for more information.\n";
        return 1;
    }

    std::string arg;

    Batch data;

    std::vector<Batch> batches = {};

    for(int i = 1; i < argc; ++i) {
        arg = argv[i];

        if(arg == ":") {
            batches.push_back(data);
            data = Batch();
        }
        else if(arg == "-s" || arg == "--speed") {
            ++i;
            if(argc <= i) {
                std::cerr << "Not enough arguments provided. Try './text-spin --help' for more information.\n";
                return 1;
            }
            data.updateDelay = std::stoi(argv[i]);
        }
        else if(arg == "-l" || arg == "--length") {
            ++i;
            if(argc <= i) {
                std::cerr << "Not enough arguments provided. Try './text-spin --help' for more information.\n";
                return 1;
            }
            data.cycleLength = std::stoi(argv[i]);
        }
        else if(arg == "-r" || arg == "--right") {
            data.spinToRight = true;
        }
        else if(arg == "--help") {
            printHelpMessage();
            return 0;
        }
        else {
            data.stringsToCycle.push_back(arg);
        }
        if(i == argc - 1) {
            batches.push_back(data);
        }
    }

    calculateCycleLength(batches);
    setupCanvas(batches);

    processAll(batches);
}