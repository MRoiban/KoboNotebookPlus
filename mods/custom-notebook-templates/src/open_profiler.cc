#include "open_profiler.h"

#include "plugin_runtime.h"
#include "plugin_state.h"
#include "settings.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

namespace cnt {
namespace open_profiler {

struct Sampler {
    enum { kCapacity = 1024, kStackWords = 256 };
    std::uintptr_t pc[kCapacity];
    std::uintptr_t lr[kCapacity];
    std::uint16_t words[kCapacity];
    std::uintptr_t stack[kCapacity][kStackWords];
    volatile sig_atomic_t count;
    volatile sig_atomic_t dropped;
    std::uintptr_t stackLow;
    std::uintptr_t stackHigh;
    long long beginNs;
    pid_t tid;
    int timer;
    struct sigaction previous;
};

namespace {

struct Mapping {
    std::uintptr_t start;
    std::uintptr_t end;
    std::uintptr_t offset;
    QByteArray name;
    bool executable;
};

QVector<Mapping> readMappings() {
    QVector<Mapping> result;
    QFile maps(QString::fromLatin1("/proc/self/maps"));
    if (!maps.open(QIODevice::ReadOnly))
        return result;
    // procfs reports size zero; read line by line until EOF.
    for (QByteArray line = maps.readLine(); !line.isEmpty(); line = maps.readLine()) {
        QList<QByteArray> const fields = line.simplified().split(' ');
        if (fields.size() < 5)
            continue;
        QList<QByteArray> const range = fields.at(0).split('-');
        if (range.size() != 2)
            continue;
        Mapping mapping;
        mapping.start = std::strtoul(range.at(0).constData(), nullptr, 16);
        mapping.end = std::strtoul(range.at(1).constData(), nullptr, 16);
        mapping.offset = std::strtoul(fields.at(2).constData(), nullptr, 16);
        mapping.executable = fields.at(1).size() >= 3 && fields.at(1).at(2) == 'x';
        QByteArray const path = fields.size() >= 6 ? fields.at(5) : QByteArray("[anon]");
        int const slash = path.lastIndexOf('/');
        mapping.name = slash >= 0 ? path.mid(slash + 1) : path;
        result.append(mapping);
    }
    std::sort(result.begin(), result.end(),
        [](Mapping const& a, Mapping const& b) { return a.start < b.start; });
    return result;
}

int findMapping(QVector<Mapping> const& mappings, std::uintptr_t address) {
    int low = 0;
    int high = mappings.size() - 1;
    while (low <= high) {
        int const middle = (low + high) / 2;
        Mapping const& mapping = mappings.at(middle);
        if (address < mapping.start)
            high = middle - 1;
        else if (address >= mapping.end)
            low = middle + 1;
        else
            return middle;
    }
    return -1;
}

void onSample(int, siginfo_t*, void* context) {
    Sampler* const sampler = pluginState().openProfiler.sampler;
    if (!sampler || !context
            || static_cast<pid_t>(syscall(SYS_gettid)) != sampler->tid)
        return;
    ucontext_t const* const uc = static_cast<ucontext_t const*>(context);
    int const index = sampler->count;
    if (index >= Sampler::kCapacity) {
        sampler->dropped = sampler->dropped + 1;
        return;
    }
    sampler->pc[index] = uc->uc_mcontext.arm_pc;
    sampler->lr[index] = uc->uc_mcontext.arm_lr;
    // Copy only within the GUI thread's own stack mapping.
    std::uintptr_t const sp = uc->uc_mcontext.arm_sp & ~std::uintptr_t(3);
    std::size_t words = 0;
    if (sp >= sampler->stackLow && sp < sampler->stackHigh) {
        words = (sampler->stackHigh - sp) / sizeof(std::uintptr_t);
        if (words > Sampler::kStackWords)
            words = Sampler::kStackWords;
        std::memcpy(sampler->stack[index], reinterpret_cast<void const*>(sp),
            words * sizeof(std::uintptr_t));
    }
    sampler->words[index] = static_cast<std::uint16_t>(words);
    sampler->count = index + 1;
}

void append(QByteArray* line, QVector<Mapping> const& mappings, std::uintptr_t address,
        bool executableOnly) {
    int const found = findMapping(mappings, address);
    if (found < 0 || (executableOnly && !mappings.at(found).executable)) {
        if (!executableOnly)
            *line += " ?";
        return;
    }
    Mapping const& mapping = mappings.at(found);
    *line += ' ';
    *line += mapping.name;
    *line += "+0x";
    *line += QByteArray::number(qulonglong(address - mapping.start + mapping.offset), 16);
}

} // namespace

long long monotonicNs() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000000000LL + now.tv_nsec;
}

bool begin() {
    RuntimeState& state = pluginState().openProfiler;
    if (state.sampler)
        return false;
    QVector<Mapping> const mappings = readMappings();
    int marker = 0;
    int const stackIndex = findMapping(
        mappings, reinterpret_cast<std::uintptr_t>(&marker));
    if (stackIndex < 0)
        return false;
    Sampler* const sampler = new (std::nothrow) Sampler;
    if (!sampler)
        return false;
    sampler->count = 0;
    sampler->dropped = 0;
    sampler->stackLow = mappings.at(stackIndex).start;
    sampler->stackHigh = mappings.at(stackIndex).end;
    sampler->tid = static_cast<pid_t>(syscall(SYS_gettid));
    sampler->timer = -1;
    sampler->beginNs = monotonicNs();

    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_sigaction = onSample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPROF, &action, &sampler->previous) != 0) {
        delete sampler;
        return false;
    }
    state.sampler = sampler;

    struct sigevent event;
    std::memset(&event, 0, sizeof(event));
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGPROF;
    event._sigev_un._tid = sampler->tid;
    int timer = -1;
    if (syscall(__NR_timer_create, CLOCK_MONOTONIC, &event, &timer) != 0) {
        sigaction(SIGPROF, &sampler->previous, nullptr);
        state.sampler = nullptr;
        delete sampler;
        return false;
    }
    sampler->timer = timer;
    struct itimerspec interval;
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_nsec = 1000000;
    interval.it_value = interval.it_interval;
    if (syscall(__NR_timer_settime, timer, 0, &interval, nullptr) != 0) {
        end(nullptr, 0);
        return false;
    }
    return true;
}

void end(char const* label, long long cutoffMonotonicNs) {
    RuntimeState& state = pluginState().openProfiler;
    Sampler* const sampler = state.sampler;
    if (!sampler)
        return;
    if (sampler->timer >= 0)
        syscall(__NR_timer_delete, sampler->timer);
    // Ignoring discards any pending SIGPROF before the old disposition returns.
    struct sigaction ignore;
    std::memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGPROF, &ignore, nullptr);
    sigaction(SIGPROF, &sampler->previous, nullptr);
    state.sampler = nullptr;

    if (label) {
        try {
            int count = sampler->count;
            if (cutoffMonotonicNs > sampler->beginNs) {
                long long const keep =
                    (cutoffMonotonicNs - sampler->beginNs) / 1000000LL + 1;
                if (keep < count)
                    count = static_cast<int>(keep);
            }
            QVector<Mapping> const mappings = readMappings();
            QFile raw(QString::fromLatin1(
                "/mnt/onboard/.kobo/iink/custom-notebook-templates/templates/open-profile.txt"));
            if (raw.open(QIODevice::WriteOnly | QIODevice::Append)) {
                QByteArray out;
                out += "# ";
                out += label;
                out += " samples=" + QByteArray::number(count)
                    + " dropped=" + QByteArray::number(int(sampler->dropped)) + "\n";
                for (int i = 0; i < count; ++i) {
                    QByteArray line("S");
                    append(&line, mappings, sampler->pc[i] & ~std::uintptr_t(1), false);
                    append(&line, mappings, sampler->lr[i] & ~std::uintptr_t(1), false);
                    line += " |";
                    for (int w = 0; w < sampler->words[i]; ++w)
                        append(&line, mappings, sampler->stack[i][w] & ~std::uintptr_t(1), true);
                    out += line + "\n";
                }
                raw.write(out);
            }
            trace(QLatin1String("perf: open-profile label=") + QLatin1String(label)
                + QLatin1String(" samples=") + QString::number(count)
                + QLatin1String(" dropped=") + QString::number(sampler->dropped)
                + QLatin1String(" interval-ms=1"));
        } catch (...) {
            // Diagnostics never affect notebook open.
        }
    }
    delete sampler;
}

} // namespace open_profiler
} // namespace cnt
