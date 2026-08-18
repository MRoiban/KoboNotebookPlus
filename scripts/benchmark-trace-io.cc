#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

class TemporaryFile {
public:
    explicit TemporaryFile(char const* path) : path_(path), armed_(false) {}

    ~TemporaryFile() {
        if (armed_)
            unlink(path_);
    }

    bool prepare() {
        struct stat existing;
        if (lstat(path_, &existing) == 0 || errno != ENOENT)
            return false;
        int const descriptor = open(
            path_, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0)
            return false;
        close(descriptor);
        armed_ = true;
        return true;
    }

    void clear() {
        if (!armed_)
            return;
        unlink(path_);
        armed_ = false;
    }

private:
    char const* path_;
    bool armed_;
};

double runBenchmark(
        char const* path,
        int iterations,
        bool durable) {
    char const line[] =
        "notebook-performance: representative trace record\n";
    std::chrono::steady_clock::time_point const started =
        std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        int const descriptor = open(path, O_WRONLY | O_APPEND);
        if (descriptor < 0
                || write(descriptor, line, sizeof(line) - 1)
                    != static_cast<ssize_t>(sizeof(line) - 1)
                || (durable && fsync(descriptor) != 0)
                || close(descriptor) != 0) {
            std::cerr << "trace benchmark I/O failed: "
                      << std::strerror(errno) << '\n';
            std::exit(2);
        }
    }
    std::chrono::steady_clock::time_point const finished =
        std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(
        finished - started).count();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: benchmark-trace-io PATH ITERATIONS\n";
        return 2;
    }
    int const iterations = std::atoi(argv[2]);
    if (iterations < 1 || iterations > 10000) {
        std::cerr << "iterations must be between 1 and 10000\n";
        return 2;
    }

    TemporaryFile temporary(argv[1]);
    if (!temporary.prepare()) {
        std::cerr << "refusing to overwrite benchmark path\n";
        return 2;
    }
    double const bufferedMs =
        runBenchmark(argv[1], iterations, false);
    double const durableMs =
        runBenchmark(argv[1], iterations, true);
    temporary.clear();

    std::cout << "iterations=" << iterations
              << " buffered-total-ms=" << bufferedMs
              << " buffered-per-line-ms="
              << bufferedMs / static_cast<double>(iterations)
              << " durable-total-ms=" << durableMs
              << " durable-per-line-ms="
              << durableMs / static_cast<double>(iterations)
              << '\n';
    return 0;
}
