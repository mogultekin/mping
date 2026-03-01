#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <algorithm>

struct Result {
    std::string ip;
    double time_ms;
};

std::mutex results_mutex;
std::vector<Result> results;

void ping_host(const std::string &ip) {
    std::array<char, 128> buffer;
    std::string result;
    std::string cmd = "ping -c1 -w1 " + ip + " 2>/dev/null";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);

    std::smatch match;
    std::regex rgx("time=([0-9.]+) ms");

    if (std::regex_search(result, match, rgx)) {
        double t = std::stod(match[1]);
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back({ip, t});
    }
}

std::vector<std::string> expand_range(const std::string &input) {
    std::vector<std::string> ips;
    std::smatch match;

    // CIDR format
    if (std::regex_match(input, match, std::regex(R"((\d+)\.(\d+)\.(\d+)\.(\d+)/(\d+))"))) {
        int a = stoi(match[1]);
        int b = stoi(match[2]);
        int c = stoi(match[3]);
        int d = stoi(match[4]);
        int prefix = stoi(match[5]);
        uint32_t base = (a << 24) | (b << 16) | (c << 8) | d;
        uint32_t mask = prefix == 0 ? 0 : (~0U << (32 - prefix));
        uint32_t network = base & mask;
        uint32_t broadcast = network | ~mask;

        for (uint32_t ip = network + 1; ip < broadcast; ++ip) {
            std::ostringstream oss;
            oss << ((ip >> 24) & 0xFF) << "."
                << ((ip >> 16) & 0xFF) << "."
                << ((ip >> 8) & 0xFF) << "."
                << (ip & 0xFF);
            ips.push_back(oss.str());
        }
    }
    // Last-octet range, e.g. 192.168.1.10-50
    else if (std::regex_match(input, match, std::regex(R"((\d+)\.(\d+)\.(\d+)\.(\d+)-(\d+))"))) {
        int a = stoi(match[1]);
        int b = stoi(match[2]);
        int c = stoi(match[3]);
        int start = stoi(match[4]);
        int end = stoi(match[5]);
        for (int d = start; d <= end; ++d) {
            std::ostringstream oss;
            oss << a << "." << b << "." << c << "." << d;
            ips.push_back(oss.str());
        }
    }

    return ips;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: mping <CIDR|range>\n";
        return 1;
    }

    std::string input(argv[1]);
    auto ips = expand_range(input);

    std::vector<std::thread> threads;
    for (auto &ip : ips) {
        threads.emplace_back(ping_host, ip);
    }
    for (auto &t : threads) t.join();

    // Sort results by IP (numeric order)
    std::sort(results.begin(), results.end(), [](const Result &a, const Result &b) {
        auto ip_to_uint = [](const std::string &ip) {
            unsigned int a, b, c, d;
            sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
            return (a << 24) | (b << 16) | (c << 8) | d;
        };
        return ip_to_uint(a.ip) < ip_to_uint(b.ip);
    });

    for (auto &r : results) {
        std::cout << r.ip << "\t" << r.time_ms << " ms" << std::endl;
    }

    return 0;
}
