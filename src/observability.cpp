#include "observability.hpp"

namespace lite3cpp {

#ifndef LITE3CPP_DISABLE_OBSERVABILITY

class NullLogger : public ILogger {
public:
  bool log(LogLevel, std::string_view, std::string_view,
           std::chrono::microseconds, size_t, std::string_view) override {
    return true;
  }
};

class NullMetrics : public IMetrics {
public:
  bool record_latency(std::string_view, double) override { return true; }
  bool increment_operation_count(std::string_view, std::string_view) override {
    return true;
  }
  bool set_buffer_usage(size_t) override { return true; }
  bool set_buffer_capacity(size_t) override { return true; }
  bool increment_node_splits() override { return true; }
  bool increment_hash_collisions() override { return true; }

  bool record_bytes_received(size_t) override { return true; }
  bool record_bytes_sent(size_t) override { return true; }
  bool increment_active_connections() override { return true; }
  bool decrement_active_connections() override { return true; }
  bool record_error(int) override { return true; }
};

static NullLogger null_logger;
static NullMetrics null_metrics;

std::atomic<ILogger *> g_logger = &null_logger;
std::atomic<IMetrics *> g_metrics = &null_metrics;

void set_logger(ILogger *logger) {
  if (logger) {
    g_logger.store(logger, std::memory_order_release);
  } else {
    g_logger.store(&null_logger, std::memory_order_release);
  }
}

void set_metrics(IMetrics *metrics) {
  if (metrics) {
    g_metrics.store(metrics, std::memory_order_release);
  } else {
    g_metrics.store(&null_metrics, std::memory_order_release);
  }
}

std::atomic<LogLevel> g_log_level_threshold = LogLevel::Info;

void set_log_level_threshold(LogLevel level) {
  g_log_level_threshold.store(level, std::memory_order_release);
}

#endif // LITE3CPP_DISABLE_OBSERVABILITY

} // namespace lite3cpp
