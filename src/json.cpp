#include "json.hpp"
#include "buffer.hpp"
#include "exception.hpp"
#include "observability.hpp"
#include "utils/hex.hpp"
#include "yyjson.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

namespace lite3cpp {
namespace lite3_json {

// Forward declarations for helper functions
static void from_yyjson_val(yyjson_val *val, Buffer &buffer, size_t ofs);
static void from_yyjson_val(yyjson_val *val, Buffer &buffer, size_t ofs,
                            std::string_view key);
static yyjson_mut_val *to_yyjson_val(const Buffer &buffer, size_t ofs,
                                     yyjson_mut_doc *doc, size_t depth = 0);

struct ScopedMetric {
  [[maybe_unused]] std::string_view op;
#ifndef LITE3CPP_DISABLE_OBSERVABILITY
  IMetrics *metrics = nullptr;
  std::chrono::high_resolution_clock::time_point start{};
#endif
  ScopedMetric(std::string_view o) : op(o) {
#ifndef LITE3CPP_DISABLE_OBSERVABILITY
    IMetrics *m = lite3cpp::g_metrics.load(std::memory_order_relaxed);
    if (m != nullptr) [[unlikely]] {
      std::atomic_thread_fence(std::memory_order_acquire);
      metrics = m;
      start = std::chrono::high_resolution_clock::now();
    }
#endif
  }
  ~ScopedMetric() {
#ifndef LITE3CPP_DISABLE_OBSERVABILITY
    if (metrics != nullptr) [[unlikely]] {
      try {
        auto end = std::chrono::high_resolution_clock::now();
        double dur = std::chrono::duration<double>(end - start).count();
        metrics->record_latency(op, dur);
        metrics->increment_operation_count(op, "ok");
      } catch (...) {
      }
    }
#endif
  }
};

// Direct internal in-order B-tree traversal over an immutable Buffer.
// Avoids Iterator's per-step is_modified() generation checks and redundant
// operator->() calls while preserving full bounds and height validation.
template <bool IsArray, typename Fn>
static void walk_btree_node(const uint8_t *base, size_t bound, size_t node_ofs,
                            Fn &&fn, size_t btree_depth = 0) {
  if (btree_depth > config::tree_height_max) {
    throw lite3cpp::exception("Corrupt B-tree: max height exceeded");
  }
  if (bound < sizeof(PackedNodeLayout) ||
      node_ofs > bound - sizeof(PackedNodeLayout)) {
    throw lite3cpp::exception("Corrupt B-tree node offset");
  }
  const auto *packed =
      reinterpret_cast<const PackedNodeLayout *>(base + node_ofs);
  NodeView node(packed);
  const uint32_t kc = node.key_count();
  const bool has_children = (packed->child_ofs[0] != 0);

  for (uint32_t i = 0; i < kc; ++i) {
    if (has_children) {
      const uint32_t child = packed->child_ofs[i];
      if (child != 0) {
        walk_btree_node<IsArray>(base, bound, child, fn, btree_depth + 1);
      }
    }
    const size_t kv_ofs = packed->kv_ofs[i];
    if constexpr (IsArray) {
      fn(std::string_view{}, kv_ofs);
    } else {
      uint32_t klen = 0;
      size_t tag_bytes = 0;
      if (!decode_key_tag(base, bound, kv_ofs, klen, tag_bytes)) {
        throw lite3cpp::exception("Corrupted key tag in buffer");
      }
      std::string_view key_sv(
          reinterpret_cast<const char *>(base + kv_ofs + tag_bytes), klen - 1);
      fn(key_sv, kv_ofs + tag_bytes + klen);
    }
  }
  if (has_children) {
    const uint32_t last_child = packed->child_ofs[kc];
    if (last_child != 0) {
      walk_btree_node<IsArray>(base, bound, last_child, fn, btree_depth + 1);
    }
  }
}

std::string to_json_string(const Buffer &buffer, size_t ofs) {
  if (buffer.size() == 0 || ofs >= buffer.size() ||
      (ofs == 0 && buffer.size() < config::node_size)) {
    return "{}";
  }
  ScopedMetric sm("json_serialize");
  lite3cpp::log_if_enabled(lite3cpp::LogLevel::Info, "JSON stringify started.",
                           "JsonStringify", std::chrono::microseconds(0), ofs);
  yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc_guard(
      doc, &yyjson_mut_doc_free);

  const uint8_t *base = buffer.data();
  const size_t bound = buffer.size();

  yyjson_mut_val *root = nullptr;
  if (ofs == 0 && bound >= config::node_size) {
    NodeView node(reinterpret_cast<const PackedNodeLayout *>(base));
    Type root_type = node.type();
    if (root_type == Type::Object) {
      root = yyjson_mut_obj(doc);
      walk_btree_node<false>(
          base, bound, 0, [&](std::string_view key, size_t val_ofs) {
            // Zero-copy key reference: buffer outlives doc.
            yyjson_mut_val *kval = yyjson_mut_strn(doc, key.data(), key.size());
            yyjson_mut_obj_add(root, kval,
                               to_yyjson_val(buffer, val_ofs, doc, 1));
          });
    } else if (root_type == Type::Array) {
      root = yyjson_mut_arr(doc);
      walk_btree_node<true>(
          base, bound, 0, [&](std::string_view, size_t val_ofs) {
            yyjson_mut_arr_add_val(root,
                                   to_yyjson_val(buffer, val_ofs, doc, 1));
          });
    } else {
      root = yyjson_mut_null(doc);
    }
  } else {
    root = to_yyjson_val(buffer, ofs, doc, 0);
  }

  yyjson_mut_doc_set_root(doc, root);
  size_t out_len = 0;
  char *json_str = yyjson_mut_write(doc, 0, &out_len);
  std::string result =
      json_str ? std::string(json_str, out_len) : std::string("{}");
  if (json_str)
    free(json_str);
  return result;
}

Buffer from_json_string(std::string_view json_str) {
  ScopedMetric sm("json_parse");
  lite3cpp::log_if_enabled(lite3cpp::LogLevel::Info, "JSON parse started.",
                           "JsonParse", std::chrono::microseconds(0), 0);
  if (json_str.empty()) {
    throw lite3cpp::exception("Invalid JSON string provided.");
  }
  yyjson_doc *doc = yyjson_read(json_str.data(), json_str.size(), 0);
  if (!doc) {
    throw lite3cpp::exception("Invalid JSON string provided.");
  }
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc_guard(
      doc, &yyjson_doc_free);
  yyjson_val *root = yyjson_doc_get_root(doc);
  Buffer buffer;
  // Pre-size buffer using yyjson's exact value count to avoid repeated
  // reallocations and kernel page faults during B-tree construction.
  const size_t val_count = yyjson_doc_get_val_count(doc);
  const size_t est_bytes =
      std::max<size_t>(config::node_size, val_count * 24 + json_str.size());
  buffer.reserve(est_bytes);
  from_yyjson_val(root, buffer, 0);
  return buffer;
}

static void from_yyjson_val(yyjson_val *val, Buffer &buffer, size_t ofs,
                            std::string_view key) {
  HashedKey hk(key);
  yyjson_type type = yyjson_get_type(val);
  switch (type) {
  case YYJSON_TYPE_NULL:
    buffer.set_null(ofs, hk);
    break;
  case YYJSON_TYPE_BOOL:
    buffer.set_bool(ofs, hk, yyjson_get_bool(val));
    break;
  case YYJSON_TYPE_NUM:
    if (yyjson_is_uint(val)) {
      buffer.set_i64(ofs, hk, static_cast<int64_t>(yyjson_get_uint(val)));
    } else if (yyjson_is_sint(val)) {
      buffer.set_i64(ofs, hk, yyjson_get_sint(val));
    } else {
      buffer.set_f64(ofs, hk, yyjson_get_real(val));
    }
    break;
  case YYJSON_TYPE_STR: {
    std::string_view str_val(yyjson_get_str(val), yyjson_get_len(val));
    buffer.set_str(ofs, hk, str_val);
    break;
  }
  case YYJSON_TYPE_ARR: {
    size_t new_ofs = buffer.set_arr(ofs, hk);
    from_yyjson_val(val, buffer, new_ofs);
    break;
  }
  case YYJSON_TYPE_OBJ: {
    size_t new_ofs = buffer.set_obj(ofs, hk);
    from_yyjson_val(val, buffer, new_ofs);
    break;
  }
  }
}

static void from_yyjson_val(yyjson_val *val, Buffer &buffer, size_t ofs) {
  yyjson_type type = yyjson_get_type(val);
  switch (type) {
  case YYJSON_TYPE_NULL:
  case YYJSON_TYPE_BOOL:
  case YYJSON_TYPE_NUM:
  case YYJSON_TYPE_STR:
    break;
  case YYJSON_TYPE_ARR: {
    if (ofs == 0 && buffer.size() == 0) {
      buffer.init_array();
    }
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(val, &iter);
    yyjson_val *item;
    while ((item = yyjson_arr_iter_next(&iter))) {
      yyjson_type item_type = yyjson_get_type(item);
      switch (item_type) {
      case YYJSON_TYPE_NULL:
        buffer.arr_append_null(ofs);
        break;
      case YYJSON_TYPE_BOOL:
        buffer.arr_append_bool(ofs, yyjson_get_bool(item));
        break;
      case YYJSON_TYPE_NUM:
        if (yyjson_is_uint(item)) {
          buffer.arr_append_i64(ofs,
                                static_cast<int64_t>(yyjson_get_uint(item)));
        } else if (yyjson_is_sint(item)) {
          buffer.arr_append_i64(ofs, yyjson_get_sint(item));
        } else {
          buffer.arr_append_f64(ofs, yyjson_get_real(item));
        }
        break;
      case YYJSON_TYPE_STR: {
        std::string_view str_val(yyjson_get_str(item), yyjson_get_len(item));
        buffer.arr_append_str(ofs, str_val);
        break;
      }
      case YYJSON_TYPE_OBJ: {
        size_t new_ofs = buffer.arr_append_obj(ofs);
        from_yyjson_val(item, buffer, new_ofs);
        break;
      }
      case YYJSON_TYPE_ARR: {
        size_t new_ofs = buffer.arr_append_arr(ofs);
        from_yyjson_val(item, buffer, new_ofs);
        break;
      }
      default:
        break;
      }
    }
    break;
  }
  case YYJSON_TYPE_OBJ: {
    if (ofs == 0 && buffer.size() == 0) {
      buffer.init_object();
    }
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(val, &iter);
    yyjson_val *key, *item;
    while ((key = yyjson_obj_iter_next(&iter))) {
      item = yyjson_obj_iter_get_val(key);
      std::string_view key_sv(yyjson_get_str(key), yyjson_get_len(key));
      from_yyjson_val(item, buffer, ofs, key_sv);
    }
    break;
  }
  }
}

static yyjson_mut_val *to_yyjson_val(const Buffer &buffer, size_t ofs,
                                     yyjson_mut_doc *doc, size_t depth) {
  if (depth > 64) {
    throw lite3cpp::exception("JSON nesting depth exceeded");
  }
  const uint8_t *base = buffer.data();
  const size_t bound = buffer.size();
  if (ofs >= bound) {
    throw lite3cpp::exception("Corrupt value offset");
  }
  const size_t rem = bound - (ofs + 1);
  Type type = static_cast<Type>(base[ofs]);
  switch (type) {
  case Type::Null:
    return yyjson_mut_null(doc);
  case Type::Bool: {
    if (rem < 1) {
      throw lite3cpp::exception("Corrupt value");
    }
    uint8_t raw = 0;
    std::memcpy(&raw, base + ofs + 1, 1);
    return yyjson_mut_bool(doc, raw != 0);
  }
  case Type::Int64: {
    if (rem < sizeof(int64_t)) {
      throw lite3cpp::exception("Corrupt value");
    }
    int64_t val;
    std::memcpy(&val, base + ofs + 1, sizeof(val));
    return yyjson_mut_int(doc, val);
  }
  case Type::Float64: {
    if (rem < sizeof(double)) {
      throw lite3cpp::exception("Corrupt value");
    }
    double val;
    std::memcpy(&val, base + ofs + 1, sizeof(val));
    return yyjson_mut_real(doc, val);
  }
  case Type::String: {
    if (rem < sizeof(uint32_t)) {
      throw lite3cpp::exception("Corrupt string length");
    }
    uint32_t size;
    std::memcpy(&size, base + ofs + 1, sizeof(size));
    if (size > rem - sizeof(uint32_t)) {
      throw lite3cpp::exception("Corrupt string length");
    }
    const char *str_data =
        reinterpret_cast<const char *>(base + ofs + 1 + sizeof(size));
    // Zero-copy string reference: buffer outlives doc.
    return yyjson_mut_strn(doc, str_data, size);
  }
  case Type::Bytes: {
    if (rem < sizeof(uint32_t)) {
      throw lite3cpp::exception("Corrupt bytes length");
    }
    uint32_t size;
    std::memcpy(&size, base + ofs + 1, sizeof(size));
    if (size > rem - sizeof(uint32_t)) {
      throw lite3cpp::exception("Corrupt bytes length");
    }
    const auto *bytes = reinterpret_cast<const std::byte *>(
        base + ofs + 1 + sizeof(size));
    std::string hex_str = utils::hex_encode({bytes, size});
    return yyjson_mut_strncpy(doc, hex_str.data(), hex_str.size());
  }
  case Type::Object: {
    if (rem < config::node_size) {
      throw lite3cpp::exception("Corrupt nested node");
    }
    NodeView nested_node(
        reinterpret_cast<const PackedNodeLayout *>(base + ofs + 1));
    if (nested_node.type() != Type::Object) {
      throw lite3cpp::exception("Corrupt nested node");
    }
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    walk_btree_node<false>(
        base, bound, ofs + 1, [&](std::string_view key, size_t val_ofs) {
          yyjson_mut_val *kval = yyjson_mut_strn(doc, key.data(), key.size());
          yyjson_mut_obj_add(obj, kval,
                             to_yyjson_val(buffer, val_ofs, doc, depth + 1));
        });
    return obj;
  }
  case Type::Array: {
    if (rem < config::node_size) {
      throw lite3cpp::exception("Corrupt nested node");
    }
    NodeView nested_node(
        reinterpret_cast<const PackedNodeLayout *>(base + ofs + 1));
    if (nested_node.type() != Type::Array) {
      throw lite3cpp::exception("Corrupt nested node");
    }
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    walk_btree_node<true>(
        base, bound, ofs + 1, [&](std::string_view, size_t val_ofs) {
          yyjson_mut_arr_add_val(
              arr, to_yyjson_val(buffer, val_ofs, doc, depth + 1));
        });
    return arr;
  }
  default:
    return yyjson_mut_null(doc);
  }
}
} // namespace lite3_json

Buffer::operator std::string() const {
  if (m_used_size < config::node_size) {
    return "";
  }
  return lite3_json::to_json_string(*this, 0);
}

} // namespace lite3cpp
