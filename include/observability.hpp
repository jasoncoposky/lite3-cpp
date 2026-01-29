#ifndef LITE3CPP_OBSERVABILITY_HPP
#define LITE3CPP_OBSERVABILITY_HPP

#include <atomic>
#include <chrono>
#include <string_view>

namespace lite3cpp {

enum class LogLevel { Debug, Info, Warn, Error };

class ILogger {
public:
  virtual ~ILogger() = default;
  virtual bool log(LogLevel level, std::string_view message,
                   std::string_view operation,
                   std::chrono::microseconds duration, size_t buffer_offset,
                   std::string_view key = "") = 0;
};

class IMetrics {
public:
  virtual ~IMetrics() = default;
  virtual bool record_latency(std::string_view operation, double seconds) = 0;
  virtual bool increment_operation_count(std::string_view operation,
                                         std::string_view status) = 0;
  virtual bool set_buffer_usage(size_t used_bytes) = 0;
  virtual bool set_buffer_capacity(size_t capacity_bytes) = 0;
  virtual bool increment_node_splits() = 0;
  virtual bool increment_hash_collisions() = 0;

  // Reduced Traffic Metrics
  virtual bool record_bytes_received(size_t bytes) = 0;
  virtual bool record_bytes_sent(size_t bytes) = 0;

  // Active Connection Gauge
  virtual bool increment_active_connections() = 0;
  virtual bool decrement_active_connections() = 0;

  // Errors
  virtual bool record_error(int status_code) = 0;

  // Replication/Sync Metrics
  virtual bool increment_sync_ops(std::string_view type) = 0;
  virtual bool increment_keys_repaired() = 0;
  virtual bool increment_mesh_bytes(std::string_view lane, size_t bytes,
                                    bool is_send) = 0;
};

#ifndef LITE3CPP_DISABLE_OBSERVABILITY
// Global atomic pointers for the current logger and metrics implementations.
// The lite3-cpp library does NOT take ownership of the objects pointed to by
// g_logger or g_metrics. Their lifetime must be managed by the caller.
extern std::atomic<ILogger *> g_logger;
extern std::atomic<IMetrics *> g_metrics;

// Sets the global logger.
// The lite3-cpp library does NOT take ownership of the 'logger' object.
// The caller is responsible for ensuring the 'logger' object remains valid
// for the entire duration it is set and used by the library.
void set_logger(ILogger *logger);

// Sets the global metrics collector.
// The lite3-cpp library does NOT take ownership of the 'metrics' object.
// The caller is responsible for ensuring the 'metrics' object remains valid
// for the entire duration it is set and used by the library.
void set_metrics(IMetrics *metrics);

// Global atomic log level threshold. Messages with a level lower than this
// threshold will not be processed by the global logger. Defaults to
// LogLevel::Info.
extern std::atomic<LogLevel> g_log_level_threshold;

// Sets the global log level threshold.
// Log messages with a level lower than the set threshold will be filtered out.
void set_log_level_threshold(LogLevel level);
#else
// No-op implementations when observability is disabled
inline void set_logger(ILogger *) {}
inline void set_metrics(IMetrics *) {}
inline void set_log_level_threshold(LogLevel) {}
#endif

template <typename... Args>
inline bool log_if_enabled(LogLevel level, std::string_view message,
                           std::string_view operation,
                           std::chrono::microseconds duration,
                           size_t buffer_offset, std::string_view key = "") {
#ifndef LITE3CPP_DISABLE_OBSERVABILITY
  if (level >= g_log_level_threshold.load(std::memory_order_acquire)) {
    ILogger *logger = g_logger.load(std::memory_order_acquire);
    if (logger) { // Should always be true due to NullLogger, but good practice
      return logger->log(level, message, operation, duration, buffer_offset,
                         key);
    }
  }
#endif
  return true; // Indicate that logging was attempted. Individual logger can
               // fail.
}

} // namespace lite3cpp

#endif // LITE3CPP_OBSERVABILITY_HPP
