#include "observability.hpp"

namespace lite3cpp {

#ifndef LITE3CPP_DISABLE_OBSERVABILITY

std::atomic<ILogger *> g_logger{nullptr};
std::atomic<IMetrics *> g_metrics{nullptr};

void set_logger(ILogger *logger) {
  g_logger.store(logger, std::memory_order_release);
}

void set_metrics(IMetrics *metrics) {
  g_metrics.store(metrics, std::memory_order_release);
}

std::atomic<LogLevel> g_log_level_threshold = LogLevel::Info;

void set_log_level_threshold(LogLevel level) {
  g_log_level_threshold.store(level, std::memory_order_release);
}

#endif // LITE3CPP_DISABLE_OBSERVABILITY

} // namespace lite3cpp
