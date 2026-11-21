#include "buffer.hpp"
#include "json.hpp"

#include "lite3.h"
#include "flatbuffers/flexbuffers.h"
#include "simdjson.h"
#include "velocypack/Builder.h"
#include "velocypack/Iterator.h"
#include "velocypack/Parser.h"
#include "velocypack/Slice.h"
#include "yyjson.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace {

template <typename T>
inline void do_not_optimize(T const& value) {
  asm volatile("" : : "r,m"(value) : "memory");
}

struct BenchResult {
  std::string name;
  double median_ns;
  double mops;
};

template <typename Fn>
BenchResult run_bench(const std::string& name, size_t iters_per_trial,
                      int trials, Fn&& fn) {
  // Warmup
  size_t warmup = std::max<size_t>(1, iters_per_trial / 10);
  for (size_t i = 0; i < warmup; ++i) {
    fn(i);
  }

  std::vector<double> samples_ns;
  samples_ns.reserve(trials);
  for (int t = 0; t < trials; ++t) {
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < iters_per_trial; ++i) {
      fn(i);
    }
    auto t1 = std::chrono::steady_clock::now();
    double elapsed_ns =
        std::chrono::duration<double, std::nano>(t1 - t0).count();
    samples_ns.push_back(elapsed_ns / static_cast<double>(iters_per_trial));
  }

  std::sort(samples_ns.begin(), samples_ns.end());
  double median = samples_ns[samples_ns.size() / 2];
  double mops = (median > 0.0) ? (1000.0 / median) : 0.0;
  return {name, median, mops};
}

void print_table(const std::string& title,
                 const std::vector<BenchResult>& results) {
  std::cout << "\n=== " << title << " ===\n";
  std::cout << std::left << std::setw(28) << "Library" << std::right
            << std::setw(14) << "Median (ns)" << std::setw(14) << "Mops/s"
            << std::setw(20) << "vs Baseline (Row 1)" << "\n";
  std::cout << std::string(76, '-') << "\n";

  double baseline_ns = results.empty() ? 1.0 : results[0].median_ns;
  for (size_t idx = 0; idx < results.size(); ++idx) {
    const auto& r = results[idx];
    char rel_buf[32];
    if (idx == 0) {
      std::snprintf(rel_buf, sizeof(rel_buf), "baseline");
    } else if (r.median_ns <= baseline_ns) {
      double factor = (r.median_ns > 0.0) ? (baseline_ns / r.median_ns) : 1.0;
      std::snprintf(rel_buf, sizeof(rel_buf), "%.2fx faster", factor);
    } else {
      double factor = (baseline_ns > 0.0) ? (r.median_ns / baseline_ns) : 1.0;
      std::snprintf(rel_buf, sizeof(rel_buf), "%.2fx slower", factor);
    }
    std::cout << std::left << std::setw(28) << r.name << std::right
              << std::fixed << std::setprecision(2) << std::setw(14)
              << r.median_ns << std::setw(14) << r.mops << std::setw(20)
              << rel_buf << "\n";
  }
}

struct Dataset {
  size_t count;
  std::vector<std::string> keys;
  std::vector<std::string> str_vals;
  std::vector<lite3cpp::HashedKey> cpp_hkeys;
  std::vector<lite3_key_data> c_hkeys;

  explicit Dataset(size_t n) : count(n) {
    keys.reserve(n);
    str_vals.reserve(n);
    cpp_hkeys.reserve(n);
    c_hkeys.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      keys.push_back("field_key_" + std::to_string(i));
      str_vals.push_back("payload_value_" + std::to_string(i));
    }
    for (size_t i = 0; i < n; ++i) {
      cpp_hkeys.emplace_back(keys[i]);
      c_hkeys.push_back(lite3_get_key_data(keys[i].c_str()));
    }
  }
};

// Build a mixed object (even keys -> int64, odd keys -> string)
lite3cpp::Buffer build_lite3cpp_obj(const Dataset& ds) {
  lite3cpp::Buffer buf;
  buf.reserve(ds.count * 128 + 4096);
  buf.init_object();
  for (size_t i = 0; i < ds.count; ++i) {
    if ((i & 1) == 0) {
      buf.set_i64(0, ds.keys[i], static_cast<int64_t>(i * 17));
    } else {
      buf.set_str(0, ds.keys[i], ds.str_vals[i]);
    }
  }
  return buf;
}

std::vector<unsigned char> build_lite3c_obj(const Dataset& ds,
                                            size_t& out_buflen) {
  std::vector<unsigned char> buf(ds.count * 128 + 4096);
  out_buflen = 0;
  if (lite3_init_obj(buf.data(), &out_buflen, buf.size()) < 0) {
    std::abort();
  }
  for (size_t i = 0; i < ds.count; ++i) {
    int rc = 0;
    if ((i & 1) == 0) {
      rc = lite3_set_i64(buf.data(), &out_buflen, 0, buf.size(),
                         ds.keys[i].c_str(), static_cast<int64_t>(i * 17));
    } else {
      rc = lite3_set_str_n(buf.data(), &out_buflen, 0, buf.size(),
                           ds.keys[i].c_str(), ds.str_vals[i].c_str(),
                           ds.str_vals[i].size());
    }
    if (rc < 0) {
      std::abort();
    }
  }
  return buf;
}

std::vector<uint8_t> build_flexbuffers_obj(const Dataset& ds) {
  flexbuffers::Builder fbb(ds.count * 64 + 1024);
  fbb.Map([&]() {
    for (size_t i = 0; i < ds.count; ++i) {
      if ((i & 1) == 0) {
        fbb.Int(ds.keys[i].c_str(), static_cast<int64_t>(i * 17));
      } else {
        fbb.String(ds.keys[i].c_str(), ds.str_vals[i]);
      }
    }
  });
  fbb.Finish();
  return fbb.GetBuffer();
}

std::string build_yyjson_obj_str(const Dataset& ds) {
  yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  for (size_t i = 0; i < ds.count; ++i) {
    if ((i & 1) == 0) {
      yyjson_mut_obj_add_sint(doc, root, ds.keys[i].c_str(),
                              static_cast<int64_t>(i * 17));
    } else {
      yyjson_mut_obj_add_strn(doc, root, ds.keys[i].c_str(),
                              ds.str_vals[i].c_str(), ds.str_vals[i].size());
    }
  }
  size_t len = 0;
  char* json = yyjson_mut_write(doc, 0, &len);
  std::string out(json, len);
  std::free(json);
  yyjson_mut_doc_free(doc);
  return out;
}

arangodb::velocypack::Buffer<uint8_t> build_vpack_obj(const Dataset& ds) {
  namespace vpack = arangodb::velocypack;
  vpack::Buffer<uint8_t> buf;
  vpack::Builder b(buf);
  b.openObject();
  for (size_t i = 0; i < ds.count; ++i) {
    if ((i & 1) == 0) {
      b.add(ds.keys[i], vpack::Value(static_cast<int64_t>(i * 17)));
    } else {
      b.add(ds.keys[i], vpack::Value(ds.str_vals[i]));
    }
  }
  b.close();
  return buf;
}

} // namespace

int main() {
  constexpr int TRIALS = 7;

  Dataset ds64(64);
  Dataset ds1000(1000);

  // Pre-build buffers for 64-key mixed object
  lite3cpp::Buffer cpp_obj64 = build_lite3cpp_obj(ds64);
  size_t c_obj64_len = 0;
  std::vector<unsigned char> c_obj64 = build_lite3c_obj(ds64, c_obj64_len);
  std::vector<uint8_t> flex_obj64 = build_flexbuffers_obj(ds64);
  auto vpack_obj64 = build_vpack_obj(ds64);
  std::string json_obj64 = build_yyjson_obj_str(ds64);
  simdjson::padded_string padded_json64(json_obj64);

  // Pre-build buffers for 1000-key mixed object
  lite3cpp::Buffer cpp_obj1000 = build_lite3cpp_obj(ds1000);
  size_t c_obj1000_len = 0;
  std::vector<unsigned char> c_obj1000 =
      build_lite3c_obj(ds1000, c_obj1000_len);
  std::vector<uint8_t> flex_obj1000 = build_flexbuffers_obj(ds1000);
  auto vpack_obj1000 = build_vpack_obj(ds1000);
  std::string json_obj1000 = build_yyjson_obj_str(ds1000);
  simdjson::padded_string padded_json1000(json_obj1000);

  // -------------------------------------------------------------------------
  // 1. Wire / Buffer Size Comparison
  // -------------------------------------------------------------------------
  std::cout << "=== 1. Wire / Buffer Size (bytes) ===\n";
  std::cout << std::left << std::setw(28) << "Library" << std::right
            << std::setw(18) << "64-key obj (B)" << std::setw(18)
            << "1000-key obj (B)" << "\n";
  std::cout << std::string(64, '-') << "\n";
  std::cout << std::left << std::setw(28) << "lite3-cpp" << std::right
            << std::setw(18) << cpp_obj64.size() << std::setw(18)
            << cpp_obj1000.size() << "\n";
  std::cout << std::left << std::setw(28) << "lite3 (C ref)" << std::right
            << std::setw(18) << c_obj64_len << std::setw(18) << c_obj1000_len
            << "\n";
  std::cout << std::left << std::setw(28) << "FlexBuffers" << std::right
            << std::setw(18) << flex_obj64.size() << std::setw(18)
            << flex_obj1000.size() << "\n";
  std::cout << std::left << std::setw(28) << "VelocyPack" << std::right
            << std::setw(18) << vpack_obj64.size() << std::setw(18)
            << vpack_obj1000.size() << "\n";
  std::cout << std::left << std::setw(28) << "JSON (yyjson compact)"
            << std::right << std::setw(18) << json_obj64.size()
            << std::setw(18) << json_obj1000.size() << "\n";

  // -------------------------------------------------------------------------
  // 2. Zero-Copy Wrap + Single-Key Lookup (64-key obj, raw bytes -> 1 i64)
  // -------------------------------------------------------------------------
  {
    const size_t target_idx = 42;
    const std::string& target_key = ds64.keys[target_idx];
    const lite3cpp::HashedKey target_hk = ds64.cpp_hkeys[target_idx];
    const lite3_key_data target_ck = ds64.c_hkeys[target_idx];

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (BufferView)", 200000, TRIALS, [&](size_t) {
      lite3cpp::BufferView view(cpp_obj64.data(), cpp_obj64.size());
      auto v = view.try_get_i64(0, target_key);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3-cpp (BufferView+hk)", 200000, TRIALS, [&](size_t) {
      lite3cpp::BufferView view(cpp_obj64.data(), cpp_obj64.size());
      auto v = view.try_get_i64(0, target_hk);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3-cpp (from_view)", 200000, TRIALS, [&](size_t) {
      auto view = lite3cpp::Buffer::from_view(cpp_obj64.data(), cpp_obj64.size());
      auto v = view.try_get_i64(0, target_key);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3-cpp (view + prehash)", 200000, TRIALS, [&](size_t) {
      auto view = lite3cpp::Buffer::from_view(cpp_obj64.data(), cpp_obj64.size());
      auto v = view.try_get_i64(0, target_hk);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref)", 200000, TRIALS, [&](size_t) {
      int64_t v = 0;
      int rc = lite3_get_i64(c_obj64.data(), c_obj64_len, 0, target_key.c_str(), &v);
      do_not_optimize(rc);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref + prehash)", 200000, TRIALS, [&](size_t) {
      int64_t v = 0;
      int rc = _lite3_get_i64_impl(c_obj64.data(), c_obj64_len, 0,
                                   target_key.c_str(), target_ck, &v);
      do_not_optimize(rc);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("FlexBuffers (GetRoot)", 200000, TRIALS, [&](size_t) {
      auto root = flexbuffers::GetRoot(flex_obj64.data(), flex_obj64.size()).AsMap();
      int64_t v = root[target_key.c_str()].AsInt64();
      do_not_optimize(v);
    }));
    res.push_back(run_bench("VelocyPack (Slice)", 200000, TRIALS, [&](size_t) {
      arangodb::velocypack::Slice s(vpack_obj64.data());
      int64_t v = s.get(target_key).getInt();
      do_not_optimize(v);
    }));
    simdjson::ondemand::parser od_parser;
    res.push_back(run_bench("simdjson (On-Demand)", 20000, TRIALS, [&](size_t) {
      auto doc = od_parser.iterate(padded_json64);
      int64_t v = doc[target_key].get_int64().value();
      do_not_optimize(v);
    }));
    res.push_back(run_bench("yyjson (parse + lookup)", 20000, TRIALS, [&](size_t) {
      yyjson_doc* doc = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
      yyjson_val* val = yyjson_obj_getn(yyjson_doc_get_root(doc),
                                        target_key.data(), target_key.size());
      int64_t v = yyjson_get_sint(val);
      do_not_optimize(v);
      yyjson_doc_free(doc);
    }));
    print_table("2. Cold Wrap + Single Key Lookup (64-key obj)", res);
  }

  // -------------------------------------------------------------------------
  // 3. Hot Random Key Lookup (64-key obj, already loaded)
  // -------------------------------------------------------------------------
  {
    auto flex_map64 =
        flexbuffers::GetRoot(flex_obj64.data(), flex_obj64.size()).AsMap();
    yyjson_doc* yy_doc64 = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
    yyjson_val* yy_root64 = yyjson_doc_get_root(yy_doc64);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (try_get)", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63; // even keys are i64
      auto v = cpp_obj64.try_get_i64(0, ds64.keys[idx]);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3-cpp (prehash)", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63;
      auto v = cpp_obj64.try_get_i64(0, ds64.cpp_hkeys[idx]);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref)", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63;
      int64_t v = 0;
      int rc = lite3_get_i64(c_obj64.data(), c_obj64_len, 0,
                             ds64.keys[idx].c_str(), &v);
      do_not_optimize(rc);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref + prehash)", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63;
      int64_t v = 0;
      int rc = _lite3_get_i64_impl(c_obj64.data(), c_obj64_len, 0,
                                   ds64.keys[idx].c_str(), ds64.c_hkeys[idx], &v);
      do_not_optimize(rc);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("FlexBuffers", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63;
      int64_t v = flex_map64[ds64.keys[idx].c_str()].AsInt64();
      do_not_optimize(v);
    }));
    arangodb::velocypack::Slice vp_slice64(vpack_obj64.data());
    res.push_back(run_bench("VelocyPack (Slice)", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63;
      int64_t v = vp_slice64.get(ds64.keys[idx]).getInt();
      do_not_optimize(v);
    }));
    simdjson::dom::parser sj_dom64;
    auto sj_root64 = sj_dom64.parse(padded_json64).get_object().value();
    res.push_back(run_bench("simdjson (DOM scan)", 100000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63;
      int64_t v = sj_root64[ds64.keys[idx]].get_int64().value();
      do_not_optimize(v);
    }));
    res.push_back(run_bench("yyjson (DOM linear scan)", 100000, TRIALS, [&](size_t i) {
      size_t idx = (i * 2) & 63;
      yyjson_val* val = yyjson_obj_getn(yy_root64, ds64.keys[idx].data(),
                                        ds64.keys[idx].size());
      int64_t v = yyjson_get_sint(val);
      do_not_optimize(v);
    }));
    print_table("3. Hot Key Lookup (64-key obj, i64)", res);
    yyjson_doc_free(yy_doc64);
  }

  // -------------------------------------------------------------------------
  // 4. Hot Random Key Lookup (1000-key obj, scaling test)
  // -------------------------------------------------------------------------
  {
    auto flex_map1000 =
        flexbuffers::GetRoot(flex_obj1000.data(), flex_obj1000.size()).AsMap();
    yyjson_doc* yy_doc1000 =
        yyjson_read(json_obj1000.data(), json_obj1000.size(), 0);
    yyjson_val* yy_root1000 = yyjson_doc_get_root(yy_doc1000);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (try_get)", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      auto v = cpp_obj1000.try_get_i64(0, ds1000.keys[idx]);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3-cpp (prehash)", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      auto v = cpp_obj1000.try_get_i64(0, ds1000.cpp_hkeys[idx]);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref)", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      int64_t v = 0;
      int rc = lite3_get_i64(c_obj1000.data(), c_obj1000_len, 0,
                             ds1000.keys[idx].c_str(), &v);
      do_not_optimize(rc);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref + prehash)", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      int64_t v = 0;
      int rc = _lite3_get_i64_impl(c_obj1000.data(), c_obj1000_len, 0,
                                   ds1000.keys[idx].c_str(),
                                   ds1000.c_hkeys[idx], &v);
      do_not_optimize(rc);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("FlexBuffers", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      int64_t v = flex_map1000[ds1000.keys[idx].c_str()].AsInt64();
      do_not_optimize(v);
    }));
    arangodb::velocypack::Slice vp_slice1000(vpack_obj1000.data());
    res.push_back(run_bench("VelocyPack (Slice)", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      int64_t v = vp_slice1000.get(ds1000.keys[idx]).getInt();
      do_not_optimize(v);
    }));
    simdjson::dom::parser sj_dom1000;
    auto sj_root1000 = sj_dom1000.parse(padded_json1000).get_object().value();
    res.push_back(run_bench("simdjson (DOM scan)", 10000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      int64_t v = sj_root1000[ds1000.keys[idx]].get_int64().value();
      do_not_optimize(v);
    }));
    res.push_back(run_bench("yyjson (DOM linear scan)", 10000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 37) % 500) * 2;
      yyjson_val* val = yyjson_obj_getn(yy_root1000, ds1000.keys[idx].data(),
                                        ds1000.keys[idx].size());
      int64_t v = yyjson_get_sint(val);
      do_not_optimize(v);
    }));
    print_table("4. Hot Key Lookup (1000-key obj, O(log N) vs O(N))", res);
    yyjson_doc_free(yy_doc1000);
  }

  // -------------------------------------------------------------------------
  // 5. Read-Modify-Write (Update 1 i64 key in 64-key obj -> serialized bytes)
  // -------------------------------------------------------------------------
  {
    const size_t target_idx = 42;
    const std::string& target_key = ds64.keys[target_idx];
    const lite3cpp::HashedKey target_hk = ds64.cpp_hkeys[target_idx];
    const lite3_key_data target_ck = ds64.c_hkeys[target_idx];

    lite3cpp::Buffer cpp_mut = build_lite3cpp_obj(ds64);
    size_t c_mut_len = 0;
    std::vector<unsigned char> c_mut = build_lite3c_obj(ds64, c_mut_len);
    flexbuffers::Builder flex_rebuild_fbb(4096);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (in-place)", 200000, TRIALS, [&](size_t i) {
      cpp_mut.set_i64(0, target_key, static_cast<int64_t>(i));
      do_not_optimize(cpp_mut.data());
    }));
    res.push_back(run_bench("lite3-cpp (in-place+hk)", 200000, TRIALS, [&](size_t i) {
      cpp_mut.set_i64(0, target_hk, static_cast<int64_t>(i));
      do_not_optimize(cpp_mut.data());
    }));
    res.push_back(run_bench("lite3 (C ref in-place)", 200000, TRIALS, [&](size_t i) {
      int rc = lite3_set_i64(c_mut.data(), &c_mut_len, 0, c_mut.size(),
                             target_key.c_str(), static_cast<int64_t>(i));
      do_not_optimize(rc);
    }));
    res.push_back(run_bench("lite3 (C ref+prehash)", 200000, TRIALS, [&](size_t i) {
      int rc = _lite3_set_i64_impl(c_mut.data(), &c_mut_len, 0, c_mut.size(),
                                   target_key.c_str(), target_ck,
                                   static_cast<int64_t>(i));
      do_not_optimize(rc);
    }));
    res.push_back(run_bench("FlexBuffers (rebuild)", 5000, TRIALS, [&](size_t i) {
      auto map = flexbuffers::GetRoot(flex_obj64.data(), flex_obj64.size()).AsMap();
      auto keys = map.Keys();
      auto vals = map.Values();
      flex_rebuild_fbb.Clear();
      flex_rebuild_fbb.Map([&]() {
        for (size_t k = 0; k < keys.size(); ++k) {
          const char* kstr = keys[k].AsKey();
          if (std::strcmp(kstr, target_key.c_str()) == 0) {
            flex_rebuild_fbb.Int(kstr, static_cast<int64_t>(i));
          } else if (vals[k].IsInt()) {
            flex_rebuild_fbb.Int(kstr, vals[k].AsInt64());
          } else {
            flex_rebuild_fbb.String(kstr, vals[k].AsString());
          }
        }
      });
      flex_rebuild_fbb.Finish();
      do_not_optimize(flex_rebuild_fbb.GetBuffer().data());
    }));
    arangodb::velocypack::Buffer<uint8_t> vp_rebuild_buf;
    res.push_back(run_bench("VelocyPack (rebuild)", 5000, TRIALS, [&](size_t i) {
      namespace vpack = arangodb::velocypack;
      vp_rebuild_buf.clear();
      vpack::Builder b(vp_rebuild_buf);
      b.openObject();
      for (auto it : vpack::ObjectIterator(vpack::Slice(vpack_obj64.data()))) {
        auto k = it.key.stringView();
        if (k == target_key) {
          b.add(k, vpack::Value(static_cast<int64_t>(i)));
        } else {
          b.add(k, it.value);
        }
      }
      b.close();
      do_not_optimize(vp_rebuild_buf.data());
    }));
    res.push_back(run_bench("yyjson (parse+mut+write)", 5000, TRIALS, [&](size_t i) {
      yyjson_doc* doc = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
      yyjson_mut_doc* mdoc = yyjson_doc_mut_copy(doc, nullptr);
      yyjson_mut_val* root = yyjson_mut_doc_get_root(mdoc);
      yyjson_mut_val* k = yyjson_mut_strn(mdoc, target_key.data(), target_key.size());
      yyjson_mut_val* v = yyjson_mut_sint(mdoc, static_cast<int64_t>(i));
      yyjson_mut_obj_replace(root, k, v);
      size_t out_len = 0;
      char* out = yyjson_mut_write(mdoc, 0, &out_len);
      do_not_optimize(out);
      std::free(out);
      yyjson_mut_doc_free(mdoc);
      yyjson_doc_free(doc);
    }));
    print_table("5. Read-Modify-Write (Update 1 key in 64-key obj -> wire)", res);
  }

  // -------------------------------------------------------------------------
  // 6. Object Construction (64 mixed keys from scratch)
  // -------------------------------------------------------------------------
  {
    std::vector<unsigned char> c_scratch(16384);
    flexbuffers::Builder flex_scratch(4096);
    lite3cpp::Buffer cpp_scratch;
    cpp_scratch.reserve(16384);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (build 64)", 5000, TRIALS, [&](size_t) {
      cpp_scratch.clear();
      cpp_scratch.init_object();
      for (size_t k = 0; k < ds64.count; ++k) {
        if ((k & 1) == 0) {
          cpp_scratch.set_i64(0, ds64.keys[k], static_cast<int64_t>(k * 17));
        } else {
          cpp_scratch.set_str(0, ds64.keys[k], ds64.str_vals[k]);
        }
      }
      do_not_optimize(cpp_scratch.data());
    }));
    res.push_back(run_bench("lite3 (C ref build 64)", 5000, TRIALS, [&](size_t) {
      size_t buflen = 0;
      lite3_init_obj(c_scratch.data(), &buflen, c_scratch.size());
      for (size_t k = 0; k < ds64.count; ++k) {
        if ((k & 1) == 0) {
          lite3_set_i64(c_scratch.data(), &buflen, 0, c_scratch.size(),
                        ds64.keys[k].c_str(), static_cast<int64_t>(k * 17));
        } else {
          lite3_set_str_n(c_scratch.data(), &buflen, 0, c_scratch.size(),
                          ds64.keys[k].c_str(), ds64.str_vals[k].c_str(),
                          ds64.str_vals[k].size());
        }
      }
      do_not_optimize(buflen);
    }));
    res.push_back(run_bench("FlexBuffers (build 64)", 5000, TRIALS, [&](size_t) {
      flex_scratch.Clear();
      flex_scratch.Map([&]() {
        for (size_t k = 0; k < ds64.count; ++k) {
          if ((k & 1) == 0) {
            flex_scratch.Int(ds64.keys[k].c_str(), static_cast<int64_t>(k * 17));
          } else {
            flex_scratch.String(ds64.keys[k].c_str(), ds64.str_vals[k]);
          }
        }
      });
      flex_scratch.Finish();
      do_not_optimize(flex_scratch.GetBuffer().data());
    }));
    arangodb::velocypack::Buffer<uint8_t> vp_scratch;
    res.push_back(run_bench("VelocyPack (build 64)", 5000, TRIALS, [&](size_t) {
      namespace vpack = arangodb::velocypack;
      vp_scratch.clear();
      vpack::Builder b(vp_scratch);
      b.openObject();
      for (size_t k = 0; k < ds64.count; ++k) {
        if ((k & 1) == 0) {
          b.add(ds64.keys[k], vpack::Value(static_cast<int64_t>(k * 17)));
        } else {
          b.add(ds64.keys[k], vpack::Value(ds64.str_vals[k]));
        }
      }
      b.close();
      do_not_optimize(vp_scratch.data());
    }));
    res.push_back(run_bench("yyjson (build+write 64)", 5000, TRIALS, [&](size_t) {
      yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
      yyjson_mut_val* root = yyjson_mut_obj(doc);
      yyjson_mut_doc_set_root(doc, root);
      for (size_t k = 0; k < ds64.count; ++k) {
        if ((k & 1) == 0) {
          yyjson_mut_obj_add_sint(doc, root, ds64.keys[k].c_str(),
                                  static_cast<int64_t>(k * 17));
        } else {
          yyjson_mut_obj_add_strn(doc, root, ds64.keys[k].c_str(),
                                  ds64.str_vals[k].c_str(),
                                  ds64.str_vals[k].size());
        }
      }
      size_t len = 0;
      char* out = yyjson_mut_write(doc, 0, &len);
      do_not_optimize(out);
      std::free(out);
      yyjson_mut_doc_free(doc);
    }));
    print_table("6. Object Construction (64 mixed keys -> wire)", res);
  }

  // -------------------------------------------------------------------------
  // 7. JSON Interop (64-key obj: Binary -> JSON & JSON -> Binary)
  // -------------------------------------------------------------------------
  {
    std::vector<BenchResult> res_enc;
    res_enc.push_back(run_bench("lite3-cpp (to_json)", 5000, TRIALS, [&](size_t) {
      std::string s = lite3cpp::lite3_json::to_json_string(cpp_obj64, 0);
      do_not_optimize(s.data());
    }));
    res_enc.push_back(run_bench("lite3 (C lite3_json_enc)", 5000, TRIALS, [&](size_t) {
      size_t out_len = 0;
      char* s = lite3_json_enc(c_obj64.data(), c_obj64_len, 0, &out_len);
      do_not_optimize(s);
      std::free(s);
    }));
    res_enc.push_back(run_bench("FlexBuffers (ToString)", 2000, TRIALS, [&](size_t) {
      std::string s =
          flexbuffers::GetRoot(flex_obj64.data(), flex_obj64.size()).ToString();
      do_not_optimize(s.data());
    }));
    res_enc.push_back(run_bench("VelocyPack (toJson)", 5000, TRIALS, [&](size_t) {
      std::string s = arangodb::velocypack::Slice(vpack_obj64.data()).toJson();
      do_not_optimize(s.data());
    }));
    yyjson_doc* yy_doc64 = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
    res_enc.push_back(run_bench("yyjson (yyjson_write)", 5000, TRIALS, [&](size_t) {
      size_t out_len = 0;
      char* s = yyjson_write(yy_doc64, 0, &out_len);
      do_not_optimize(s);
      std::free(s);
    }));
    print_table("7a. JSON Encode (64-key obj -> JSON text)", res_enc);
    yyjson_doc_free(yy_doc64);

    std::vector<unsigned char> c_dec_buf(16384);
    std::vector<BenchResult> res_dec;
    res_dec.push_back(run_bench("lite3-cpp (from_json)", 5000, TRIALS, [&](size_t) {
      lite3cpp::Buffer b = lite3cpp::lite3_json::from_json_string(json_obj64);
      do_not_optimize(b.data());
    }));
    res_dec.push_back(run_bench("lite3 (C lite3_json_dec)", 5000, TRIALS, [&](size_t) {
      size_t out_len = 0;
      int rc = lite3_json_dec(c_dec_buf.data(), &out_len, c_dec_buf.size(),
                              json_obj64.data(), json_obj64.size());
      do_not_optimize(rc);
    }));
    res_dec.push_back(run_bench("VelocyPack (fromJson)", 5000, TRIALS, [&](size_t) {
      auto b = arangodb::velocypack::Parser::fromJson(json_obj64);
      do_not_optimize(b->data());
    }));
    simdjson::dom::parser sj_dec_parser;
    res_dec.push_back(run_bench("simdjson (DOM parse)", 10000, TRIALS, [&](size_t) {
      auto doc = sj_dec_parser.parse(padded_json64);
      do_not_optimize(doc);
    }));
    res_dec.push_back(run_bench("yyjson (yyjson_read)", 10000, TRIALS, [&](size_t) {
      yyjson_doc* doc = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
      do_not_optimize(doc);
      yyjson_doc_free(doc);
    }));
    print_table("7b. JSON Decode (64-key JSON text -> Binary/DOM)", res_dec);
  }

  // -------------------------------------------------------------------------
  // 8. Hot String Lookup (64-key obj, odd keys are strings)
  // -------------------------------------------------------------------------
  {
    auto flex_map64 =
        flexbuffers::GetRoot(flex_obj64.data(), flex_obj64.size()).AsMap();
    yyjson_doc* yy_doc64 = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
    yyjson_val* yy_root64 = yyjson_doc_get_root(yy_doc64);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (try_get_str)", 200000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1; // odd keys are strings
      auto v = cpp_obj64.try_get_str(0, ds64.keys[idx]);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3-cpp (str+prehash)", 200000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1;
      auto v = cpp_obj64.try_get_str(0, ds64.cpp_hkeys[idx]);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref get_str)", 200000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1;
      lite3_str s{};
      int rc = lite3_get_str(c_obj64.data(), c_obj64_len, 0,
                             ds64.keys[idx].c_str(), &s);
      do_not_optimize(rc);
      do_not_optimize(s);
    }));
    res.push_back(run_bench("lite3 (C str+prehash)", 200000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1;
      lite3_str s{};
      int rc = _lite3_get_str_impl(c_obj64.data(), c_obj64_len, 0,
                                   ds64.keys[idx].c_str(), ds64.c_hkeys[idx], &s);
      do_not_optimize(rc);
      do_not_optimize(s);
    }));
    res.push_back(run_bench("FlexBuffers (AsString)", 200000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1;
      auto s = flex_map64[ds64.keys[idx].c_str()].AsString();
      do_not_optimize(s);
    }));
    arangodb::velocypack::Slice vp_slice64(vpack_obj64.data());
    res.push_back(run_bench("VelocyPack (Slice str)", 200000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1;
      auto sv = vp_slice64.get(ds64.keys[idx]).stringView();
      do_not_optimize(sv);
    }));
    simdjson::dom::parser sj_dom64;
    auto sj_root64 = sj_dom64.parse(padded_json64).get_object().value();
    res.push_back(run_bench("simdjson (DOM str)", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1;
      auto sv = sj_root64[ds64.keys[idx]].get_string().value();
      do_not_optimize(sv);
    }));
    res.push_back(run_bench("yyjson (DOM str scan)", 100000, TRIALS, [&](size_t i) {
      size_t idx = ((i * 2) & 63) | 1;
      yyjson_val* val = yyjson_obj_getn(yy_root64, ds64.keys[idx].data(),
                                        ds64.keys[idx].size());
      const char* s = yyjson_get_str(val);
      do_not_optimize(s);
    }));
    print_table("8. Hot String Lookup (64-key obj)", res);
    yyjson_doc_free(yy_doc64);
  }

  // -------------------------------------------------------------------------
  // 9. Miss Lookup (non-existent key in 64-key obj)
  // -------------------------------------------------------------------------
  {
    const std::string miss_key = "nonexistent_field_xyz";
    const lite3cpp::HashedKey miss_hk(miss_key);
    const lite3_key_data miss_ck = lite3_get_key_data(miss_key.c_str());

    auto flex_map64 =
        flexbuffers::GetRoot(flex_obj64.data(), flex_obj64.size()).AsMap();
    yyjson_doc* yy_doc64 = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
    yyjson_val* yy_root64 = yyjson_doc_get_root(yy_doc64);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (miss)", 200000, TRIALS, [&](size_t) {
      auto v = cpp_obj64.try_get_i64(0, miss_key);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3-cpp (miss+hk)", 200000, TRIALS, [&](size_t) {
      auto v = cpp_obj64.try_get_i64(0, miss_hk);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C ref miss)", 200000, TRIALS, [&](size_t) {
      int64_t v = 0;
      int rc = lite3_get_i64(c_obj64.data(), c_obj64_len, 0, miss_key.c_str(), &v);
      do_not_optimize(rc);
    }));
    res.push_back(run_bench("lite3 (C miss+prehash)", 200000, TRIALS, [&](size_t) {
      int64_t v = 0;
      int rc = _lite3_get_i64_impl(c_obj64.data(), c_obj64_len, 0,
                                   miss_key.c_str(), miss_ck, &v);
      do_not_optimize(rc);
    }));
    res.push_back(run_bench("FlexBuffers (miss)", 200000, TRIALS, [&](size_t) {
      auto r = flex_map64[miss_key.c_str()];
      bool ok = !r.IsNull();
      do_not_optimize(ok);
    }));
    arangodb::velocypack::Slice vp_slice64(vpack_obj64.data());
    res.push_back(run_bench("VelocyPack (miss)", 200000, TRIALS, [&](size_t) {
      bool none = vp_slice64.get(miss_key).isNone();
      do_not_optimize(none);
    }));
    res.push_back(run_bench("yyjson (DOM miss)", 100000, TRIALS, [&](size_t) {
      yyjson_val* val = yyjson_obj_getn(yy_root64, miss_key.data(), miss_key.size());
      do_not_optimize(val);
    }));
    print_table("9. Negative / Miss Lookup (64-key obj)", res);
    yyjson_doc_free(yy_doc64);
  }

  // -------------------------------------------------------------------------
  // 10. Nested Object Traversal (4 levels deep: l1 -> l2 -> l3 -> target)
  // -------------------------------------------------------------------------
  {
    using namespace lite3cpp::literals;
    lite3cpp::Buffer cpp_nest;
    cpp_nest.reserve(4096);
    cpp_nest.init_object();
    size_t l1 = cpp_nest.set_obj(0, "level1");
    for (int i = 0; i < 8; ++i) cpp_nest.set_i64(l1, "pad_" + std::to_string(i), i);
    size_t l2 = cpp_nest.set_obj(l1, "level2");
    for (int i = 0; i < 8; ++i) cpp_nest.set_i64(l2, "pad_" + std::to_string(i), i);
    size_t l3 = cpp_nest.set_obj(l2, "level3");
    for (int i = 0; i < 8; ++i) cpp_nest.set_i64(l3, "pad_" + std::to_string(i), i);
    cpp_nest.set_i64(l3, "target", 987654321);

    std::vector<unsigned char> c_nest(4096);
    size_t c_nest_len = 0;
    lite3_init_obj(c_nest.data(), &c_nest_len, c_nest.size());
    size_t cl1 = 0, cl2 = 0, cl3 = 0;
    lite3_set_obj(c_nest.data(), &c_nest_len, 0, c_nest.size(), "level1", &cl1);
    for (int i = 0; i < 8; ++i) {
      std::string k = "pad_" + std::to_string(i);
      lite3_set_i64(c_nest.data(), &c_nest_len, cl1, c_nest.size(), k.c_str(), i);
    }
    lite3_set_obj(c_nest.data(), &c_nest_len, cl1, c_nest.size(), "level2", &cl2);
    for (int i = 0; i < 8; ++i) {
      std::string k = "pad_" + std::to_string(i);
      lite3_set_i64(c_nest.data(), &c_nest_len, cl2, c_nest.size(), k.c_str(), i);
    }
    lite3_set_obj(c_nest.data(), &c_nest_len, cl2, c_nest.size(), "level3", &cl3);
    for (int i = 0; i < 8; ++i) {
      std::string k = "pad_" + std::to_string(i);
      lite3_set_i64(c_nest.data(), &c_nest_len, cl3, c_nest.size(), k.c_str(), i);
    }
    lite3_set_i64(c_nest.data(), &c_nest_len, cl3, c_nest.size(), "target", 987654321);

    flexbuffers::Builder fbb(1024);
    fbb.Map([&]() {
      fbb.Map("level1", [&]() {
        for (int i = 0; i < 8; ++i) fbb.Int(("pad_" + std::to_string(i)).c_str(), i);
        fbb.Map("level2", [&]() {
          for (int i = 0; i < 8; ++i) fbb.Int(("pad_" + std::to_string(i)).c_str(), i);
          fbb.Map("level3", [&]() {
            for (int i = 0; i < 8; ++i) fbb.Int(("pad_" + std::to_string(i)).c_str(), i);
            fbb.Int("target", 987654321);
          });
        });
      });
    });
    fbb.Finish();
    auto flex_nest = fbb.GetBuffer();
    auto flex_nest_root = flexbuffers::GetRoot(flex_nest.data(), flex_nest.size()).AsMap();

    std::string nest_json = lite3cpp::lite3_json::to_json_string(cpp_nest, 0);
    auto vp_nest = arangodb::velocypack::Parser::fromJson(nest_json);
    arangodb::velocypack::Slice vp_nest_root(vp_nest->data());
    simdjson::padded_string padded_nest_json(nest_json);
    simdjson::ondemand::parser sj_nest_od;
    yyjson_doc* yy_nest = yyjson_read(nest_json.data(), nest_json.size(), 0);
    yyjson_val* yy_nest_root = yyjson_doc_get_root(yy_nest);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (4-hop _hk)", 200000, TRIALS, [&](size_t) {
      auto o1 = cpp_nest.try_get_obj(0, "level1"_hk);
      auto o2 = cpp_nest.try_get_obj(*o1, "level2"_hk);
      auto o3 = cpp_nest.try_get_obj(*o2, "level3"_hk);
      auto v = cpp_nest.try_get_i64(*o3, "target"_hk);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("lite3 (C 4-hop literals)", 200000, TRIALS, [&](size_t) {
      size_t o1 = 0, o2 = 0, o3 = 0;
      int64_t v = 0;
      lite3_get_obj(c_nest.data(), c_nest_len, 0, "level1", &o1);
      lite3_get_obj(c_nest.data(), c_nest_len, o1, "level2", &o2);
      lite3_get_obj(c_nest.data(), c_nest_len, o2, "level3", &o3);
      lite3_get_i64(c_nest.data(), c_nest_len, o3, "target", &v);
      do_not_optimize(v);
    }));
    res.push_back(run_bench("FlexBuffers (4-hop)", 200000, TRIALS, [&](size_t) {
      int64_t v = flex_nest_root["level1"].AsMap()["level2"].AsMap()["level3"].AsMap()["target"].AsInt64();
      do_not_optimize(v);
    }));
    res.push_back(run_bench("VelocyPack (4-hop)", 200000, TRIALS, [&](size_t) {
      int64_t v = vp_nest_root.get("level1").get("level2").get("level3").get("target").getInt();
      do_not_optimize(v);
    }));
    res.push_back(run_bench("simdjson (On-Demand 4-hop)", 20000, TRIALS, [&](size_t) {
      auto doc = sj_nest_od.iterate(padded_nest_json);
      int64_t v = doc["level1"]["level2"]["level3"]["target"].get_int64().value();
      do_not_optimize(v);
    }));
    res.push_back(run_bench("yyjson (DOM 4-hop)", 200000, TRIALS, [&](size_t) {
      yyjson_val* o1 = yyjson_obj_getn(yy_nest_root, "level1", 6);
      yyjson_val* o2 = yyjson_obj_getn(o1, "level2", 6);
      yyjson_val* o3 = yyjson_obj_getn(o2, "level3", 6);
      int64_t v = yyjson_get_sint(yyjson_obj_getn(o3, "target", 6));
      do_not_optimize(v);
    }));
    print_table("10. Nested Object Traversal (4 levels deep)", res);
    yyjson_doc_free(yy_nest);
  }

  // -------------------------------------------------------------------------
  // 11. Array Construction & Random Access (1000 int64 elements)
  // -------------------------------------------------------------------------
  {
    constexpr size_t ARR_N = 1000;
    lite3cpp::Buffer cpp_arr;
    cpp_arr.reserve(65536);
    std::vector<unsigned char> c_arr(65536);
    size_t c_arr_len = 0;
    flexbuffers::Builder flex_arr_fbb(16384);

    std::vector<BenchResult> res_build;
    res_build.push_back(run_bench("lite3-cpp (append 1000)", 1000, TRIALS, [&](size_t) {
      cpp_arr.clear();
      cpp_arr.init_array();
      for (size_t k = 0; k < ARR_N; ++k) {
        cpp_arr.arr_append_i64(0, static_cast<int64_t>(k * 3));
      }
      do_not_optimize(cpp_arr.data());
    }));
    res_build.push_back(run_bench("lite3 (C append 1000)", 1000, TRIALS, [&](size_t) {
      c_arr_len = 0;
      lite3_init_arr(c_arr.data(), &c_arr_len, c_arr.size());
      for (size_t k = 0; k < ARR_N; ++k) {
        lite3_arr_append_i64(c_arr.data(), &c_arr_len, 0, c_arr.size(),
                             static_cast<int64_t>(k * 3));
      }
      do_not_optimize(c_arr_len);
    }));
    res_build.push_back(run_bench("FlexBuffers (TypedVec 1000)", 1000, TRIALS, [&](size_t) {
      flex_arr_fbb.Clear();
      flex_arr_fbb.TypedVector([&]() {
        for (size_t k = 0; k < ARR_N; ++k) {
          flex_arr_fbb.Int(static_cast<int64_t>(k * 3));
        }
      });
      flex_arr_fbb.Finish();
      do_not_optimize(flex_arr_fbb.GetBuffer().data());
    }));
    arangodb::velocypack::Buffer<uint8_t> vp_arr_buf;
    res_build.push_back(run_bench("VelocyPack (arr 1000)", 1000, TRIALS, [&](size_t) {
      namespace vpack = arangodb::velocypack;
      vp_arr_buf.clear();
      vpack::Builder b(vp_arr_buf);
      b.openArray();
      for (size_t k = 0; k < ARR_N; ++k) {
        b.add(vpack::Value(static_cast<int64_t>(k * 3)));
      }
      b.close();
      do_not_optimize(vp_arr_buf.data());
    }));
    res_build.push_back(run_bench("yyjson (arr build+write)", 1000, TRIALS, [&](size_t) {
      yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
      yyjson_mut_val* root = yyjson_mut_arr(doc);
      yyjson_mut_doc_set_root(doc, root);
      for (size_t k = 0; k < ARR_N; ++k) {
        yyjson_mut_arr_add_sint(doc, root, static_cast<int64_t>(k * 3));
      }
      size_t len = 0;
      char* out = yyjson_mut_write(doc, 0, &len);
      do_not_optimize(out);
      std::free(out);
      yyjson_mut_doc_free(doc);
    }));
    print_table("11a. Array Construction (1000 int64s -> wire)", res_build);

    // Prepare finalized array buffers for random access & iteration
    auto flex_arr_buf = flex_arr_fbb.GetBuffer();
    auto flex_arr_vec = flexbuffers::GetRoot(flex_arr_buf.data(), flex_arr_buf.size()).AsTypedVector();
    std::string arr_json = lite3cpp::lite3_json::to_json_string(cpp_arr, 0);
    yyjson_doc* yy_arr_doc = yyjson_read(arr_json.data(), arr_json.size(), 0);
    yyjson_val* yy_arr_root = yyjson_doc_get_root(yy_arr_doc);

    std::vector<BenchResult> res_get;
    res_get.push_back(run_bench("lite3-cpp (arr_try_get)", 100000, TRIALS, [&](size_t i) {
      uint32_t idx = static_cast<uint32_t>((i * 37) % ARR_N);
      auto v = cpp_arr.arr_try_get_i64(0, idx);
      do_not_optimize(v);
    }));
    res_get.push_back(run_bench("lite3 (C arr_get_i64)", 100000, TRIALS, [&](size_t i) {
      uint32_t idx = static_cast<uint32_t>((i * 37) % ARR_N);
      int64_t v = 0;
      int rc = lite3_arr_get_i64(c_arr.data(), c_arr_len, 0, idx, &v);
      do_not_optimize(rc);
      do_not_optimize(v);
    }));
    res_get.push_back(run_bench("FlexBuffers (TypedVec[i])", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 37) % ARR_N;
      int64_t v = flex_arr_vec[idx].AsInt64();
      do_not_optimize(v);
    }));
    arangodb::velocypack::Slice vp_arr_slice(vp_arr_buf.data());
    res_get.push_back(run_bench("VelocyPack (Slice::at)", 200000, TRIALS, [&](size_t i) {
      size_t idx = (i * 37) % ARR_N;
      int64_t v = vp_arr_slice.at(idx).getInt();
      do_not_optimize(v);
    }));
    res_get.push_back(run_bench("yyjson (yyjson_arr_get)", 20000, TRIALS, [&](size_t i) {
      size_t idx = (i * 37) % ARR_N;
      int64_t v = yyjson_get_sint(yyjson_arr_get(yy_arr_root, idx));
      do_not_optimize(v);
    }));
    print_table("11b. Array Random Access (1000 int64s)", res_get);

    // 11c. Full Array Iteration (1000 int64s)
    std::vector<BenchResult> res_iter;
    res_iter.push_back(run_bench("lite3-cpp (Iterator)", 5000, TRIALS, [&](size_t) {
      int64_t sum = 0;
      const uint8_t* base = cpp_arr.data();
      for (auto it = cpp_arr.begin(0), e = cpp_arr.end(0); it != e; ++it) {
        int64_t v = 0;
        std::memcpy(&v, base + it->value_offset + 1, sizeof(v));
        sum += v;
      }
      do_not_optimize(sum);
    }));
    res_iter.push_back(run_bench("lite3 (C lite3_iter)", 5000, TRIALS, [&](size_t) {
      int64_t sum = 0;
      lite3_iter iter{};
      lite3_iter_create(c_arr.data(), c_arr_len, 0, &iter);
      size_t val_ofs = 0;
      while (lite3_iter_next(c_arr.data(), c_arr_len, &iter, nullptr, &val_ofs) == LITE3_ITER_ITEM) {
        sum += lite3_val_i64(reinterpret_cast<lite3_val*>(c_arr.data() + val_ofs));
      }
      do_not_optimize(sum);
    }));
    res_iter.push_back(run_bench("FlexBuffers (TypedVec loop)", 20000, TRIALS, [&](size_t) {
      int64_t sum = 0;
      for (size_t k = 0; k < flex_arr_vec.size(); ++k) {
        sum += flex_arr_vec[k].AsInt64();
      }
      do_not_optimize(sum);
    }));
    res_iter.push_back(run_bench("VelocyPack (ArrayIter)", 20000, TRIALS, [&](size_t) {
      int64_t sum = 0;
      for (auto s : arangodb::velocypack::ArrayIterator(vp_arr_slice)) {
        sum += s.getInt();
      }
      do_not_optimize(sum);
    }));
    res_iter.push_back(run_bench("yyjson (arr_foreach)", 20000, TRIALS, [&](size_t) {
      int64_t sum = 0;
      size_t idx, max;
      yyjson_val* val;
      yyjson_arr_foreach(yy_arr_root, idx, max, val) {
        sum += yyjson_get_sint(val);
      }
      do_not_optimize(sum);
    }));
    print_table("11c. Full Array Iteration (1000 int64s)", res_iter);
    yyjson_doc_free(yy_arr_doc);
  }

  // -------------------------------------------------------------------------
  // 12. Full Object Iteration (64 mixed keys)
  // -------------------------------------------------------------------------
  {
    auto flex_map64 =
        flexbuffers::GetRoot(flex_obj64.data(), flex_obj64.size()).AsMap();
    yyjson_doc* yy_doc64 = yyjson_read(json_obj64.data(), json_obj64.size(), 0);
    yyjson_val* yy_root64 = yyjson_doc_get_root(yy_doc64);

    std::vector<BenchResult> res;
    res.push_back(run_bench("lite3-cpp (Iterator)", 20000, TRIALS, [&](size_t) {
      int64_t acc = 0;
      const uint8_t* base = cpp_obj64.data();
      for (auto it = cpp_obj64.begin(0), e = cpp_obj64.end(0); it != e; ++it) {
        acc += static_cast<int64_t>(it->key.size());
        if (it->value_type == lite3cpp::Type::Int64) {
          int64_t v = 0;
          std::memcpy(&v, base + it->value_offset + 1, sizeof(v));
          acc += v;
        } else if (it->value_type == lite3cpp::Type::String) {
          uint32_t slen = 0;
          std::memcpy(&slen, base + it->value_offset + 1, sizeof(slen));
          acc += slen;
        }
      }
      do_not_optimize(acc);
    }));
    res.push_back(run_bench("lite3 (C lite3_iter)", 20000, TRIALS, [&](size_t) {
      int64_t acc = 0;
      lite3_iter iter{};
      lite3_iter_create(c_obj64.data(), c_obj64_len, 0, &iter);
      lite3_str key{};
      size_t val_ofs = 0;
      while (lite3_iter_next(c_obj64.data(), c_obj64_len, &iter, &key, &val_ofs) == LITE3_ITER_ITEM) {
        acc += static_cast<int64_t>(key.len);
        auto* val = reinterpret_cast<lite3_val*>(c_obj64.data() + val_ofs);
        if (lite3_val_type(val) == LITE3_TYPE_I64) {
          acc += lite3_val_i64(val);
        } else if (lite3_val_type(val) == LITE3_TYPE_STRING) {
          size_t slen = 0;
          lite3_val_str_n(val, &slen);
          acc += static_cast<int64_t>(slen);
        }
      }
      do_not_optimize(acc);
    }));
    res.push_back(run_bench("FlexBuffers (Map loop)", 20000, TRIALS, [&](size_t) {
      int64_t acc = 0;
      auto keys = flex_map64.Keys();
      auto vals = flex_map64.Values();
      for (size_t k = 0; k < keys.size(); ++k) {
        acc += static_cast<int64_t>(std::strlen(keys[k].AsKey()));
        if (vals[k].IsInt()) {
          acc += vals[k].AsInt64();
        } else if (vals[k].IsString()) {
          acc += static_cast<int64_t>(vals[k].AsString().length());
        }
      }
      do_not_optimize(acc);
    }));
    res.push_back(run_bench("VelocyPack (ObjectIter)", 20000, TRIALS, [&](size_t) {
      int64_t acc = 0;
      for (auto it : arangodb::velocypack::ObjectIterator(
               arangodb::velocypack::Slice(vpack_obj64.data()))) {
        acc += static_cast<int64_t>(it.key.stringView().size());
        if (it.value.isInteger()) {
          acc += it.value.getInt();
        } else if (it.value.isString()) {
          acc += static_cast<int64_t>(it.value.stringView().size());
        }
      }
      do_not_optimize(acc);
    }));
    res.push_back(run_bench("yyjson (obj_foreach)", 50000, TRIALS, [&](size_t) {
      int64_t acc = 0;
      size_t idx, max;
      yyjson_val *k, *v;
      yyjson_obj_foreach(yy_root64, idx, max, k, v) {
        acc += static_cast<int64_t>(yyjson_get_len(k));
        if (yyjson_is_sint(v)) {
          acc += yyjson_get_sint(v);
        } else if (yyjson_is_str(v)) {
          acc += static_cast<int64_t>(yyjson_get_len(v));
        }
      }
      do_not_optimize(acc);
    }));
    print_table("12. Full Object Iteration (64 mixed keys)", res);
    yyjson_doc_free(yy_doc64);
  }

  // -------------------------------------------------------------------------
  // 13. Binary Blob Write & Zero-Copy Read (64 KiB payload)
  // -------------------------------------------------------------------------
  {
    constexpr size_t BLOB_SZ = 65536;
    std::vector<uint8_t> blob(BLOB_SZ, 0xAB);
    std::span<const std::byte> blob_span(
        reinterpret_cast<const std::byte*>(blob.data()), blob.size());

    lite3cpp::Buffer cpp_blob;
    cpp_blob.reserve(BLOB_SZ + 4096);
    std::vector<unsigned char> c_blob(BLOB_SZ + 4096);
    size_t c_blob_len = 0;
    flexbuffers::Builder flex_blob_fbb(BLOB_SZ + 1024);

    std::vector<BenchResult> res_w;
    res_w.push_back(run_bench("lite3-cpp (set_bytes)", 5000, TRIALS, [&](size_t) {
      cpp_blob.clear();
      cpp_blob.init_object();
      cpp_blob.set_bytes(0, "blob", blob_span);
      do_not_optimize(cpp_blob.data());
    }));
    res_w.push_back(run_bench("lite3-cpp (uninit+copy)", 5000, TRIALS, [&](size_t) {
      cpp_blob.clear();
      cpp_blob.init_object();
      auto dst = cpp_blob.set_bytes_uninitialized(0, "blob", BLOB_SZ);
      std::memcpy(dst.data(), blob.data(), BLOB_SZ);
      do_not_optimize(cpp_blob.data());
    }));
    res_w.push_back(run_bench("lite3 (C set_bytes)", 5000, TRIALS, [&](size_t) {
      c_blob_len = 0;
      lite3_init_obj(c_blob.data(), &c_blob_len, c_blob.size());
      lite3_set_bytes(c_blob.data(), &c_blob_len, 0, c_blob.size(),
                      "blob", blob.data(), blob.size());
      do_not_optimize(c_blob_len);
    }));
    res_w.push_back(run_bench("FlexBuffers (Blob 64KiB)", 5000, TRIALS, [&](size_t) {
      flex_blob_fbb.Clear();
      flex_blob_fbb.Map([&]() {
        flex_blob_fbb.Blob("blob", blob.data(), blob.size());
      });
      flex_blob_fbb.Finish();
      do_not_optimize(flex_blob_fbb.GetBuffer().data());
    }));
    arangodb::velocypack::Buffer<uint8_t> vp_blob_buf;
    res_w.push_back(run_bench("VelocyPack (Binary 64KiB)", 5000, TRIALS, [&](size_t) {
      namespace vpack = arangodb::velocypack;
      vp_blob_buf.clear();
      vpack::Builder b(vp_blob_buf);
      b.openObject();
      b.add("blob", vpack::ValuePair(blob.data(), blob.size(), vpack::ValueType::Binary));
      b.close();
      do_not_optimize(vp_blob_buf.data());
    }));
    print_table("13a. Binary Blob Write (64 KiB)", res_w);

    auto flex_blob_buf = flex_blob_fbb.GetBuffer();
    auto flex_blob_map = flexbuffers::GetRoot(flex_blob_buf.data(), flex_blob_buf.size()).AsMap();
    arangodb::velocypack::Slice vp_blob_slice(vp_blob_buf.data());
    using namespace lite3cpp::literals;

    std::vector<BenchResult> res_r;
    res_r.push_back(run_bench("lite3-cpp (try_get_bytes)", 200000, TRIALS, [&](size_t) {
      auto sp = cpp_blob.try_get_bytes(0, "blob"_hk);
      do_not_optimize(sp);
    }));
    res_r.push_back(run_bench("lite3 (C get_bytes)", 200000, TRIALS, [&](size_t) {
      lite3_bytes b{};
      int rc = lite3_get_bytes(c_blob.data(), c_blob_len, 0, "blob", &b);
      do_not_optimize(rc);
      do_not_optimize(b);
    }));
    res_r.push_back(run_bench("FlexBuffers (AsBlob)", 200000, TRIALS, [&](size_t) {
      auto b = flex_blob_map["blob"].AsBlob();
      do_not_optimize(b);
    }));
    res_r.push_back(run_bench("VelocyPack (getBinary)", 200000, TRIALS, [&](size_t) {
      arangodb::velocypack::ValueLength blen = 0;
      const uint8_t* p = vp_blob_slice.get("blob").getBinary(blen);
      do_not_optimize(p);
      do_not_optimize(blen);
    }));
    print_table("13b. Binary Blob Zero-Copy Read (64 KiB)", res_r);
  }

  return 0;
}
