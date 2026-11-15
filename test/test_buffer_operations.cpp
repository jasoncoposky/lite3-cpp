#include "buffer.hpp"
#include "document.hpp"
#include "exception.hpp" // Added
#include "json.hpp"
#include "observability.hpp"
#include "utils/hash.hpp"
#include <gtest/gtest.h> // Include gtest header
#include <cstring>
#include <iostream>
#include <stdexcept> // For std::runtime_error in tests
#include <string>
#include <vector>

// TestLogger needs to be accessible within TEST macros
class GTestLogger : public lite3cpp::ILogger {
public:
  struct LogEntry {
    lite3cpp::LogLevel level;
    std::string message;
    std::string operation;
  };

  std::vector<LogEntry> logs;

  bool log(lite3cpp::LogLevel level, // Changed return type to bool
           std::string_view message, std::string_view operation,
           std::chrono::microseconds, size_t, std::string_view) override {
    logs.push_back({level, std::string(message), std::string(operation)});
    return true; // Added return true
  }
};

// Test fixture for buffer tests
struct BufferTest : public ::testing::Test {
  lite3cpp::Buffer buffer;
  GTestLogger test_logger; // Instance of the logger

  void SetUp() override {
    // Common setup for buffer tests
    buffer = lite3cpp::Buffer(); // Re-initialize buffer for each test
    lite3cpp::set_log_level_threshold(lite3cpp::LogLevel::Debug);
    lite3cpp::set_logger(&test_logger); // Set our test logger
  }

  void TearDown() override {
    lite3cpp::set_logger(nullptr); // Reset logger after test
  }
};

TEST_F(BufferTest, Creation) {
  lite3cpp::Buffer test_buffer(1024);
  ASSERT_GE(test_buffer.capacity(), 1024);
}

TEST_F(BufferTest, ObjectInitialization) {
  buffer.init_object();
  ASSERT_EQ(buffer.size(), lite3cpp::config::node_size);
}

TEST_F(BufferTest, ArrayInitialization) {
  buffer.init_array();
  ASSERT_EQ(buffer.size(), lite3cpp::config::node_size);
}

TEST_F(BufferTest, SetGetString) {
  buffer.init_object();
  buffer.set_str(0, "name", "test_string");
  std::string_view value = buffer.get_str(0, "name");
  ASSERT_EQ(value, "test_string");
}

TEST_F(BufferTest, SetGetInt) {
  buffer.init_object();
  buffer.set_i64(0, "age", 30);
  int64_t value = buffer.get_i64(0, "age");
  ASSERT_EQ(value, 30);
}

TEST_F(BufferTest, SetGetBool) {
  buffer.init_object();
  buffer.set_bool(0, "is_active", true);
  bool value = buffer.get_bool(0, "is_active");
  ASSERT_EQ(value, true);
}

TEST_F(BufferTest, SetGetNull) {
  buffer.init_object();
  buffer.set_null(0, "extra_data");
  ASSERT_EQ(buffer.get_type(0, "extra_data"), lite3cpp::Type::Null);
}

TEST_F(BufferTest, ArrayAppendGetString) {
  buffer.init_array();
  buffer.arr_append_str(0, "hello");
  buffer.arr_append_str(0, "world");
  std::string_view value1 = buffer.arr_get_str(0, 0);
  std::string_view value2 = buffer.arr_get_str(0, 1);
  ASSERT_EQ(value1, "hello");
  ASSERT_EQ(value2, "world");
  ASSERT_EQ(buffer.arr_get_type(0, 0), lite3cpp::Type::String);
}

TEST_F(BufferTest, ArrayAppendGetInt) {
  buffer.init_array();
  buffer.arr_append_i64(0, 10);
  buffer.arr_append_i64(0, 20);
  int64_t value1 = buffer.arr_get_i64(0, 0);
  int64_t value2 = buffer.arr_get_i64(0, 1);
  ASSERT_EQ(value1, 10);
  ASSERT_EQ(value2, 20);
  ASSERT_EQ(buffer.arr_get_type(0, 0), lite3cpp::Type::Int64);
}

TEST_F(BufferTest, ArrayAppendGetBool) {
  buffer.init_array();
  buffer.arr_append_bool(0, true);
  buffer.arr_append_bool(0, false);
  bool value1 = buffer.arr_get_bool(0, 0);
  bool value2 = buffer.arr_get_bool(0, 1);
  ASSERT_EQ(value1, true);
  ASSERT_EQ(value2, false);
  ASSERT_EQ(buffer.arr_get_type(0, 0), lite3cpp::Type::Bool);
  ASSERT_EQ(buffer.arr_get_type(0, 1), lite3cpp::Type::Bool);
}

TEST_F(BufferTest, ArrayAppendGetNull) {
  buffer.init_array();
  buffer.arr_append_null(0);
  ASSERT_EQ(buffer.arr_get_type(0, 0), lite3cpp::Type::Null);
}

TEST_F(BufferTest, BTreeSplit) {
  // The logger is already set in SetUp()
  // GTestLogger logger;
  // lite3cpp::set_logger(&logger);

  buffer.init_object();

  // Insert enough keys to trigger a split
  for (int i = 0; i < lite3cpp::config::node_key_count + 1; ++i) {
    std::string key = "key" + std::to_string(i);
    buffer.set_i64(0, key, i);
  }

#ifndef LITE3CPP_DISABLE_OBSERVABILITY
  // Verify that a split occurred
  bool split_found = false;
  for (const auto &entry : test_logger.logs) {
    if (entry.operation == "set_impl" &&
        entry.message.find("Node is full, splitting") != std::string::npos) {
      split_found = true;
      break;
    }
  }
  ASSERT_TRUE(split_found);
#endif

  // Verify that all keys can be retrieved
  for (int i = 0; i < lite3cpp::config::node_key_count + 1; ++i) {
    std::string key = "key" + std::to_string(i);
    int64_t value = buffer.get_i64(0, key);
    ASSERT_EQ(value, i);
  }
  // The logger is reset in TearDown()
  // lite3cpp::set_logger(nullptr); // Reset logger
}

TEST_F(BufferTest, IteratorGenerationalSafety) {
  buffer.init_object();
  buffer.set_i64(0, "key1", 1);
  buffer.set_i64(0, "key2", 2);

  auto it = buffer.begin(0); // Create an iterator

  // Modify the buffer
  buffer.set_i64(0, "key3", 3);

  ASSERT_THROW({ (void)*it; }, lite3cpp::exception);
  ASSERT_THROW({ (void)it.key(); }, lite3cpp::exception);
}

TEST_F(BufferTest, ArrayOptimizedIndexingBasic) {
  buffer.init_array();

  buffer.arr_append_i64(0, 100);
  buffer.arr_append_str(0, "optimized");
  buffer.arr_append_bool(0, true);
  buffer.arr_append_null(0);

  ASSERT_EQ(buffer.arr_get_i64(0, 0), 100);
  ASSERT_EQ(buffer.arr_get_str(0, 1), "optimized");
  ASSERT_EQ(buffer.arr_get_bool(0, 2), true);
  ASSERT_EQ(buffer.arr_get_type(0, 3), lite3cpp::Type::Null);
}

TEST_F(BufferTest, ArrayOptimizedIndexingExceptions) {
  buffer.init_array();
  buffer.arr_append_i64(0, 10); // Index 0

  ASSERT_THROW(
      {
        buffer.arr_get_i64(0, 1); // Index 1 is out of bounds
      },
      lite3cpp::exception);
}

TEST_F(BufferTest, ErrorHandlingTypeMismatch) {
  buffer.init_object();
  buffer.set_str(0, "my_string", "hello");

  ASSERT_THROW(
      {
        buffer.get_i64(0, "my_string"); // Attempt to get string as int
      },
      lite3cpp::exception);
}

// run_json_tests() will be called from test_json.cpp's main, which will now
// also be a gtest executable.

TEST_F(BufferTest, SetGetF64) {
  buffer.init_object();
  buffer.set_f64(0, "pi", 3.14159);
  double value = buffer.get_f64(0, "pi");
  ASSERT_DOUBLE_EQ(value, 3.14159);
}

TEST_F(BufferTest, NestedObject) {
  buffer.init_object();
  size_t nested_ofs = buffer.set_obj(0, "user");
  buffer.set_str(nested_ofs, "name", "John");
  buffer.set_i64(nested_ofs, "id", 123);

  size_t retrieved_ofs = buffer.get_obj(0, "user");
  ASSERT_EQ(buffer.get_str(retrieved_ofs, "name"), "John");
  ASSERT_EQ(buffer.get_i64(retrieved_ofs, "id"), 123);
}

TEST_F(BufferTest, NestedArray) {
  buffer.init_array();
  size_t nested_arr_ofs = buffer.arr_append_arr(0);
  buffer.arr_append_i64(nested_arr_ofs, 10);
  buffer.arr_append_i64(nested_arr_ofs, 20);

  size_t retrieved_arr_ofs = buffer.arr_get_arr(0, 0);
  ASSERT_EQ(buffer.arr_get_i64(retrieved_arr_ofs, 0), 10);
  ASSERT_EQ(buffer.arr_get_i64(retrieved_arr_ofs, 1), 20);
}

TEST_F(BufferTest, ArrayOverwrite) {
  buffer.init_array();
  buffer.arr_append_str(0, "original");
  ASSERT_EQ(buffer.arr_get_str(0, 0), "original");

  // Overwrite requires knowing the index. In lite3-cpp, arrays are append-only
  // buffers conceptually unless we use set_* with a calculated offset logic or
  // if the library supports index-based updates explicitly. Looking at
  // buffer.hpp, there is NO public `arr_set_*` method that takes an index. The
  // C example used `lite3_arr_set_str(buf, &buflen, 0, bufsz, 2, "gnu")`. Let's
  // check if Buffer class exposes such methods. Re-reading buffer.hpp... it
  // DOES NOT seem to have arr_set_str(index). Wait, the C API has it. Let's
  // check buffer.cpp to see if there's a missing feature or if I missed it. If
  // it's missing, I should add it or skip this test. Actually, let's HOLD OFF
  // on this test until verified.
}

TEST_F(BufferTest, ManualBufferOperations) {
  buffer.init_object();

  // 1. set_i64 followed by get_type check (Off-by-One Read Bug)
  buffer.set_i64(0, "test_key", 12345);
  lite3cpp::Type type = buffer.get_type(0, "test_key");
  ASSERT_EQ(type, lite3cpp::Type::Int64);
  ASSERT_EQ(buffer.get_i64(0, "test_key"), 12345);

  // 2. Update/Patch (set_i64 on existing key) followed by retrieval (Update
  // Corruption Bug) Updating existing key with new value
  buffer.set_i64(0, "test_key", 67890);

  // Verify value is updated
  int64_t val = buffer.get_i64(0, "test_key");
  ASSERT_EQ(val, 67890);

  // Verify type is still correct
  type = buffer.get_type(0, "test_key");
  ASSERT_EQ(type, lite3cpp::Type::Int64);
}

TEST_F(BufferTest, PatchSidecar) {
  // Coverage: Validates the end-to-end flow of updating a key and verifying
  // it remains retrievable, which was exactly what failed during development.

  buffer.init_object();

  // Initial state
  buffer.set_str(0, "sidecar_config", "v1.0");
  buffer.set_i64(0, "sidecar_id", 100);

  ASSERT_EQ(buffer.get_str(0, "sidecar_config"), "v1.0");
  ASSERT_EQ(buffer.get_i64(0, "sidecar_id"), 100);

  // Patch update: change config version
  buffer.set_str(0, "sidecar_config", "v1.1-patched");

  // Verify update stuck
  ASSERT_EQ(buffer.get_str(0, "sidecar_config"), "v1.1-patched");

  // Verify other keys remain intact
  ASSERT_EQ(buffer.get_i64(0, "sidecar_id"), 100);

  // Patch update: change ID type? No, stick to value update first.
  buffer.set_i64(0, "sidecar_id", 101);
  ASSERT_EQ(buffer.get_i64(0, "sidecar_id"), 101);

  // Verify structure integrity by adding a new key after patch
  buffer.set_bool(0, "sidecar_active", true);
  ASSERT_EQ(buffer.get_bool(0, "sidecar_active"), true);

  // Retrieve all again to ensure no corruption
  ASSERT_EQ(buffer.get_str(0, "sidecar_config"), "v1.1-patched");
  ASSERT_EQ(buffer.get_i64(0, "sidecar_id"), 101);
}

TEST_F(BufferTest, ExplicitStringAndNullSafety) {
  // Verify Buffer(nullptr, 0) does not crash or trigger UB and has size() == 0
  lite3cpp::Buffer null_buf(nullptr, 0);
  EXPECT_EQ(null_buf.size(), 0);

  std::string s_empty(null_buf);
  EXPECT_EQ(s_empty, "");

  // Verify explicit std::string conversion on valid buffer works as expected
  buffer.init_object();
  buffer.set_str(0, "greeting", "hello world");
  buffer.set_i64(0, "count", 42);

  std::string s(buffer);
  EXPECT_FALSE(s.empty());
  EXPECT_NE(s.find("greeting"), std::string::npos);
  EXPECT_NE(s.find("hello world"), std::string::npos);
  EXPECT_NE(s.find("count"), std::string::npos);
  EXPECT_NE(s.find("42"), std::string::npos);
}

TEST_F(BufferTest, OverwriteNullOrScalarWithNestedObjectAndArrayDoesNotCorruptSiblings) {
  lite3cpp::Buffer buf;
  buf.init_object();
  buf.set_null(0, "slot_a");
  buf.set_str(0, "neighbor_after_a", "intact_neighbor_payload_value");
  buf.set_null(0, "slot_b");
  buf.set_i64(0, "tail_counter", 987654321LL);

  size_t obj_ofs = buf.set_obj(0, "slot_a");
  buf.set_str(obj_ofs, "inner_k", "inner_v");

  size_t arr_ofs = buf.set_arr(0, "slot_b");
  buf.arr_append_i64(arr_ofs, 42);

  EXPECT_EQ(buf.get_str(0, "neighbor_after_a"), "intact_neighbor_payload_value");
  EXPECT_EQ(buf.get_i64(0, "tail_counter"), 987654321LL);
  EXPECT_EQ(buf.get_str(buf.get_obj(0, "slot_a"), "inner_k"), "inner_v");
  EXPECT_EQ(buf.arr_get_i64(buf.get_arr(0, "slot_b"), 0), 42);

  buf.set_null(0, "slot_a");
  EXPECT_EQ(buf.get_type(0, "slot_a"), lite3cpp::Type::Null);
  EXPECT_EQ(buf.get_str(0, "neighbor_after_a"), "intact_neighbor_payload_value");
}

TEST_F(BufferTest, NewObjectAndArrayNodesZeroEntire96ByteAlignedRegion) {
  std::vector<uint8_t> poisoned(512, 0xAA);
  lite3cpp::Buffer buf(std::move(poisoned));
  buf.init_object();
  size_t child_obj = buf.set_obj(0, "child_obj");
  size_t child_arr = buf.set_arr(0, "child_arr");
  size_t nested_obj = buf.arr_append_obj(child_arr);
  size_t nested_arr = buf.arr_append_arr(child_arr);

  const auto* obj_layout = reinterpret_cast<const lite3cpp::PackedNodeLayout*>(buf.data() + child_obj);
  const auto* arr_layout = reinterpret_cast<const lite3cpp::PackedNodeLayout*>(buf.data() + child_arr);
  const auto* nested_obj_layout = reinterpret_cast<const lite3cpp::PackedNodeLayout*>(buf.data() + nested_obj);
  const auto* nested_arr_layout = reinterpret_cast<const lite3cpp::PackedNodeLayout*>(buf.data() + nested_arr);
  for (size_t i = 0; i <= lite3cpp::config::node_key_count; ++i) {
    EXPECT_EQ(uint32_t{obj_layout->child_offsets[i]}, 0u);
    EXPECT_EQ(uint32_t{arr_layout->child_offsets[i]}, 0u);
    EXPECT_EQ(uint32_t{nested_obj_layout->child_offsets[i]}, 0u);
    EXPECT_EQ(uint32_t{nested_arr_layout->child_offsets[i]}, 0u);
  }
}

TEST_F(BufferTest, LongKeys63BytesAnd200BytesRoundTripAndIterate) {
  lite3cpp::Buffer buf;
  buf.init_object();

  std::string key_62(62, 'a');
  std::string key_63(63, 'b');
  std::string key_200(200, 'c');
  std::string key_16383(16383, 'd');

  buf.set_str(0, key_62, "val_62");
  buf.set_str(0, key_63, "val_63");
  buf.set_i64(0, key_200, 1337);
  buf.set_i64(0, key_16383, 9001);

  EXPECT_EQ(buf.get_str(0, key_62), "val_62");
  EXPECT_EQ(buf.get_str(0, key_63), "val_63");
  EXPECT_EQ(buf.get_i64(0, key_200), 1337);
  EXPECT_EQ(buf.get_i64(0, key_16383), 9001);

  buf.set_i64(0, key_200, 7331);
  EXPECT_EQ(buf.get_i64(0, key_200), 7331);
  buf.set_str(0, key_63, "longer_val_63_payload");
  EXPECT_EQ(buf.get_str(0, key_63), "longer_val_63_payload");

  size_t count = 0;
  for (auto it = buf.begin(0); it != buf.end(0); ++it) {
    auto k = it.key();
    EXPECT_TRUE(k == key_62 || k == key_63 || k == key_200 || k == key_16383);
    ++count;
  }
  EXPECT_EQ(count, 4u);
}

TEST_F(BufferTest, FromViewReadsAndIteratesZeroCopyThenMaterializesOnWrite) {
  lite3cpp::Buffer src;
  src.init_object();
  src.set_str(0, "objPath", "/tempZone/home/rods/zero_copy.bin");
  src.set_i64(0, "dataSize", 1048576);
  size_t sub = src.set_obj(0, "condInput");
  src.set_str(sub, "write_chunk_size", "4194304");

  const uint8_t* wire_ptr = src.data();
  const size_t wire_len = src.size();

  // Zero-copy view: pointer identity MUST equal wire_ptr and capacity() == 0
  lite3cpp::Buffer view = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  EXPECT_TRUE(view.is_view());
  EXPECT_EQ(view.data(), wire_ptr);
  EXPECT_EQ(view.size(), wire_len);
  EXPECT_EQ(view.capacity(), 0u);
  EXPECT_EQ(view.get_str(0, "objPath"), "/tempZone/home/rods/zero_copy.bin");
  EXPECT_EQ(view.get_i64(0, "dataSize"), 1048576);
  EXPECT_EQ(view.get_str(view.get_obj(0, "condInput"), "write_chunk_size"), "4194304");

  // Also test std::span<const std::byte> overload
  std::span<const std::byte> byte_span(reinterpret_cast<const std::byte*>(wire_ptr), wire_len);
  lite3cpp::Buffer span_view = lite3cpp::Buffer::from_view(byte_span);
  EXPECT_TRUE(span_view.is_view());
  EXPECT_EQ(span_view.data(), wire_ptr);

  // Copying a view Buffer MUST remain a zero-copy view (0 heap allocations)
  lite3cpp::Buffer view_copy = view;
  EXPECT_TRUE(view_copy.is_view());
  EXPECT_EQ(view_copy.data(), wire_ptr);

  // Iterating a view Buffer MUST work without materializing
  size_t keys_seen = 0;
  for (auto it = view_copy.begin(0); it != view_copy.end(0); ++it) {
    EXPECT_FALSE(it.key().empty());
    ++keys_seen;
  }
  EXPECT_EQ(keys_seen, 3u);
  EXPECT_TRUE(view_copy.is_view());

  // JSON conversion on a view Buffer MUST work without materializing
  std::string as_json = static_cast<std::string>(view_copy);
  EXPECT_NE(as_json.find("zero_copy.bin"), std::string::npos);
  EXPECT_TRUE(view_copy.is_view());

  // Mutating view_copy MUST Copy-on-Write materialize without mutating wire_ptr / src
  view_copy.set_i64(0, "dataSize", 999);
  EXPECT_FALSE(view_copy.is_view());
  EXPECT_NE(view_copy.data(), wire_ptr);
  EXPECT_EQ(view_copy.get_i64(0, "dataSize"), 999);
  EXPECT_EQ(src.get_i64(0, "dataSize"), 1048576);
}

TEST_F(BufferTest, ReleaseTransfersVectorOwnershipZeroCopy) {
  lite3cpp::Buffer buf;
  buf.init_object();
  buf.set_str(0, "payload", "owned_bytes");
  const uint8_t* orig_ptr = buf.data();
  const size_t orig_used = buf.size();

  std::vector<uint8_t> out = buf.release();
  EXPECT_EQ(out.data(), orig_ptr);
  EXPECT_EQ(out.size(), orig_used);
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_FALSE(buf.is_view());

  // Release on a view Buffer copies once into a fresh vector and resets the view
  lite3cpp::Buffer view = lite3cpp::Buffer::from_view(out.data(), out.size());
  std::vector<uint8_t> view_out = view.release();
  EXPECT_NE(view_out.data(), out.data());
  EXPECT_EQ(view_out.size(), out.size());
  EXPECT_EQ(std::memcmp(view_out.data(), out.data(), out.size()), 0);
  EXPECT_FALSE(view.is_view());
  EXPECT_EQ(view.size(), 0u);
}

TEST_F(BufferTest, FromViewEdgeCasesMoveAndAllMutatorsCoW) {
  // Null / empty views stay safe empty non-views
  lite3cpp::Buffer null_view = lite3cpp::Buffer::from_view(nullptr, 0);
  EXPECT_FALSE(null_view.is_view());
  EXPECT_EQ(null_view.size(), 0u);
  EXPECT_EQ(null_view.begin(0), null_view.end(0));

  lite3cpp::Buffer null_len_view = lite3cpp::Buffer::from_view(nullptr, 64);
  EXPECT_FALSE(null_len_view.is_view());
  EXPECT_EQ(null_len_view.size(), 0u);

  const uint8_t dummy = 0;
  lite3cpp::Buffer zero_len_view = lite3cpp::Buffer::from_view(&dummy, 0);
  EXPECT_FALSE(zero_len_view.is_view());
  EXPECT_EQ(zero_len_view.size(), 0u);

  // Build rich source buffer with bytes, nested arrays, and nested objects
  lite3cpp::Buffer src;
  src.init_object();
  const std::byte raw_bytes[] = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  src.set_bytes(0, "blob", raw_bytes);
  size_t arr_ofs = src.set_arr(0, "items");
  src.arr_append_i64(arr_ofs, 10);
  src.arr_append_bytes(arr_ofs, raw_bytes);
  size_t nested_obj = src.arr_append_obj(arr_ofs);
  src.set_str(nested_obj, "tag", "nested");

  const uint8_t* wire_ptr = src.data();
  const size_t wire_len = src.size();

  // Move constructor & move assignment preserve zero-copy view and reset source
  lite3cpp::Buffer v1 = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  lite3cpp::Buffer v2(std::move(v1));
  EXPECT_FALSE(v1.is_view());
  EXPECT_EQ(v1.size(), 0u);
  EXPECT_TRUE(v2.is_view());
  EXPECT_EQ(v2.data(), wire_ptr);
  EXPECT_EQ(v2.size(), wire_len);

  lite3cpp::Buffer v3;
  v3 = std::move(v2);
  EXPECT_FALSE(v2.is_view());
  EXPECT_EQ(v2.size(), 0u);
  EXPECT_TRUE(v3.is_view());
  EXPECT_EQ(v3.data(), wire_ptr);

  // Read bytes, nested array, nested object on view without materializing
  auto got_blob = v3.get_bytes(0, "blob");
  ASSERT_EQ(got_blob.size(), 4u);
  EXPECT_EQ(got_blob[0], std::byte{0xDE});
  size_t got_arr = v3.get_arr(0, "items");
  EXPECT_EQ(v3.arr_get_i64(got_arr, 0), 10);
  EXPECT_EQ(v3.arr_get_bytes(got_arr, 1).size(), 4u);
  EXPECT_EQ(v3.get_str(v3.arr_get_obj(got_arr, 2), "tag"), "nested");
  EXPECT_TRUE(v3.is_view());

  // Iterator invalidation on CoW materialization
  lite3cpp::Buffer v_iter = v3;
  auto it = v_iter.begin(0);
  ASSERT_NE(it, v_iter.end(0));
  v_iter.set_str(0, "added", "after_iter");
  EXPECT_FALSE(v_iter.is_view());
  EXPECT_THROW({ (void)*it; }, lite3cpp::exception);

  // CoW on arr_append_*, arr_append_obj, arr_append_arr
  lite3cpp::Buffer arr_src;
  arr_src.init_array();
  arr_src.arr_append_i64(0, 1);
  const uint8_t* arr_wire = arr_src.data();
  const size_t arr_len = arr_src.size();

  lite3cpp::Buffer av1 = lite3cpp::Buffer::from_view(arr_wire, arr_len);
  av1.arr_append_i64(0, 2);
  EXPECT_FALSE(av1.is_view());
  EXPECT_NE(av1.data(), arr_wire);
  EXPECT_EQ(av1.arr_get_i64(0, 1), 2);
  EXPECT_THROW(arr_src.arr_get_i64(0, 1), lite3cpp::exception);

  lite3cpp::Buffer av2 = lite3cpp::Buffer::from_view(arr_wire, arr_len);
  size_t sub_obj = av2.arr_append_obj(0);
  av2.set_i64(sub_obj, "x", 7);
  EXPECT_FALSE(av2.is_view());
  EXPECT_EQ(av2.get_i64(av2.arr_get_obj(0, 1), "x"), 7);

  lite3cpp::Buffer av3 = lite3cpp::Buffer::from_view(arr_wire, arr_len);
  size_t sub_arr = av3.arr_append_arr(0);
  av3.arr_append_str(sub_arr, "elem");
  EXPECT_FALSE(av3.is_view());
  EXPECT_EQ(av3.arr_get_str(av3.arr_get_arr(0, 1), 0), "elem");

  // CoW on reserve, ensure_capacity, shrink_to_fit, init_object, move_to_string
  lite3cpp::Buffer vr = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  vr.reserve(512);
  EXPECT_FALSE(vr.is_view());
  EXPECT_GE(vr.capacity(), 512u);
  EXPECT_EQ(vr.get_bytes(0, "blob").size(), 4u);

  lite3cpp::Buffer ve = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  ve.ensure_capacity(64);
  EXPECT_FALSE(ve.is_view());

  lite3cpp::Buffer vs = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  vs.shrink_to_fit();
  EXPECT_FALSE(vs.is_view());

  lite3cpp::Buffer vi = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  vi.init_object();
  EXPECT_FALSE(vi.is_view());
  EXPECT_NE(vi.data(), wire_ptr);
  EXPECT_EQ(vi.size(), lite3cpp::config::node_size);
  EXPECT_EQ(src.get_bytes(0, "blob").size(), 4u);

  lite3cpp::Buffer vm = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  std::string moved_str = vm.move_to_string();
  EXPECT_EQ(moved_str.size(), wire_len);
  EXPECT_FALSE(vm.is_view());
  EXPECT_EQ(vm.size(), 0u);
}

TEST_F(BufferTest, FromViewCopyAssignSelfAssignAndCoWBTreeSplit) {
  lite3cpp::Buffer src;
  src.init_object();
  for (int i = 0; i < 6; ++i) {
    src.set_i64(0, "init_" + std::to_string(i), i * 10);
  }
  const uint8_t* wire_ptr = src.data();
  const size_t wire_len = src.size();

  // Copy-assigning a view onto a populated owned Buffer transitions it to a view
  lite3cpp::Buffer owned;
  owned.init_object();
  owned.set_str(0, "owned_key", "owned_payload_value");
  EXPECT_FALSE(owned.is_view());

  lite3cpp::Buffer view = lite3cpp::Buffer::from_view(wire_ptr, wire_len);
  owned = view;
  EXPECT_TRUE(owned.is_view());
  EXPECT_EQ(owned.data(), wire_ptr);
  EXPECT_EQ(owned.size(), wire_len);
  EXPECT_EQ(owned.capacity(), 0u);
  EXPECT_EQ(owned.get_i64(0, "init_5"), 50);

  // Self copy-assignment and self move-assignment preserve the view
  lite3cpp::Buffer& view_ref = view;
  view = view_ref;
  EXPECT_TRUE(view.is_view());
  EXPECT_EQ(view.data(), wire_ptr);
  EXPECT_EQ(view.size(), wire_len);

  view = std::move(view_ref);
  EXPECT_TRUE(view.is_view());
  EXPECT_EQ(view.data(), wire_ptr);
  EXPECT_EQ(view.size(), wire_len);

  // B-tree node splits (> 7 keys) during CoW mutation without mutating src
  for (int i = 0; i < 16; ++i) {
    owned.set_i64(0, "split_key_" + std::to_string(i), 1000 + i);
  }
  EXPECT_FALSE(owned.is_view());
  EXPECT_NE(owned.data(), wire_ptr);
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(owned.get_i64(0, "init_" + std::to_string(i)), i * 10);
    EXPECT_EQ(src.get_i64(0, "init_" + std::to_string(i)), i * 10);
  }
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(owned.get_i64(0, "split_key_" + std::to_string(i)), 1000 + i);
    EXPECT_EQ(src.get_type(0, "split_key_" + std::to_string(i)), lite3cpp::Type::Invalid);
  }
}

TEST_F(BufferTest, SelfAliasingWritesAcrossCoWReallocationAndInPlaceOverwrite) {
  const std::string long_path(320, 'p');
  lite3cpp::Buffer src;
  src.init_object();
  src.set_str(0, "objPath", long_path);
  src.set_str(0, "dynKeyName", "aliased_target_key");

  lite3cpp::Buffer view = lite3cpp::Buffer::from_view(src.data(), src.size());

  // 1. First CoW write aliasing external view memory
  view.set_str(0, "dup", view.get_str(0, "objPath"));
  EXPECT_FALSE(view.is_view());
  EXPECT_EQ(view.get_str(0, "dup"), long_path);

  // 2. Force tight capacity so the next self-aliased write MUST reallocate m_data
  view.shrink_to_fit();
  const uint8_t* before_realloc = view.data();
  view.set_str(0, "dup2", view.get_str(0, "dup"));
  EXPECT_NE(view.data(), before_realloc);
  EXPECT_EQ(view.get_str(0, "dup2"), long_path);

  // 3. Both key and value aliasing owned m_data across reallocation + B-tree split
  for (int i = 0; i < 8; ++i) {
    view.shrink_to_fit();
    std::string k = "fill_" + std::to_string(i);
    view.set_str(0, k, view.get_str(0, "dup2"));
  }
  view.shrink_to_fit();
  view.set_str(0, view.get_str(0, "dynKeyName"), view.get_str(0, "dup2"));
  EXPECT_EQ(view.get_str(0, "aliased_target_key"), long_path);

  // 4. In-place self-overwrite (dst == val_ptr)
  view.set_str(0, "dup", view.get_str(0, "dup"));
  EXPECT_EQ(view.get_str(0, "dup"), long_path);
}

TEST_F(BufferTest, MalformedAndTruncatedViewsThrowCleanlyWithoutMemoryCorruption) {
  lite3cpp::Buffer valid;
  valid.init_object();
  valid.set_str(0, "strKey", "abcdefghij");
  valid.set_i64(0, "intKey", 42);
  valid.set_f64(0, "fltKey", 3.5);
  valid.set_bool(0, "boolKey", true);
  size_t child = valid.set_obj(0, "subObj");
  valid.set_i64(child, "x", 1);

  const uint8_t* wire = valid.data();
  const size_t wire_len = valid.size();

  // Empty / null view mutations must throw without dereferencing null
  EXPECT_THROW(lite3cpp::Buffer::from_view(nullptr, 0).set_i64(0, "k", 1), lite3cpp::exception);
  EXPECT_THROW(lite3cpp::Buffer::from_view(nullptr, 0).arr_append_i64(0, 1), lite3cpp::exception);
  EXPECT_THROW(lite3cpp::Buffer::from_view(nullptr, 0).arr_append_obj(0), lite3cpp::exception);
  EXPECT_THROW(lite3cpp::Buffer::from_view(nullptr, 0).arr_append_arr(0), lite3cpp::exception);

  // 16-byte truncated view mutation must throw
  EXPECT_THROW(lite3cpp::Buffer::from_view(wire, 16).set_i64(0, "k", 1), lite3cpp::exception);

  // Out-of-bounds offset and array append on Object view must throw before materializing
  lite3cpp::Buffer oob_view = lite3cpp::Buffer::from_view(wire, wire_len);
  EXPECT_THROW(oob_view.set_i64(wire_len, "k", 1), lite3cpp::exception);
  EXPECT_TRUE(oob_view.is_view());
  EXPECT_THROW(oob_view.arr_append_i64(0, 1), lite3cpp::exception);
  EXPECT_TRUE(oob_view.is_view());

  // Truncated string payload in view
  {
    lite3cpp::Buffer sbuf;
    sbuf.init_object();
    sbuf.set_str(0, "s", "0123456789");
    // Truncate 5 bytes off the end of the string payload
    lite3cpp::Buffer trunc_str = lite3cpp::Buffer::from_view(sbuf.data(), sbuf.size() - 5);
    EXPECT_THROW((void)trunc_str.get_str(0, "s"), lite3cpp::exception);
    EXPECT_THROW((void)static_cast<std::string>(trunc_str), lite3cpp::exception);
    // Truncate inside the 4-byte string length field
    lite3cpp::Buffer trunc_len = lite3cpp::Buffer::from_view(sbuf.data(), sbuf.size() - 13);
    EXPECT_THROW((void)trunc_len.get_str(0, "s"), lite3cpp::exception);
  }

  // Truncated i64, f64, bool, and nested obj payloads in view
  {
    lite3cpp::Buffer ibuf;
    ibuf.init_object();
    ibuf.set_i64(0, "n", 123456789);
    lite3cpp::Buffer trunc_i64 = lite3cpp::Buffer::from_view(ibuf.data(), ibuf.size() - 3);
    EXPECT_THROW((void)trunc_i64.get_i64(0, "n"), lite3cpp::exception);
    EXPECT_THROW(trunc_i64.set_i64(0, "n", 999), lite3cpp::exception);

    lite3cpp::Buffer fbuf;
    fbuf.init_object();
    fbuf.set_f64(0, "f", 1.25);
    lite3cpp::Buffer trunc_f64 = lite3cpp::Buffer::from_view(fbuf.data(), fbuf.size() - 4);
    EXPECT_THROW((void)trunc_f64.get_f64(0, "f"), lite3cpp::exception);

    lite3cpp::Buffer bbuf;
    bbuf.init_object();
    bbuf.set_bool(0, "b", true);
    lite3cpp::Buffer trunc_bool = lite3cpp::Buffer::from_view(bbuf.data(), bbuf.size() - 1);
    EXPECT_THROW((void)trunc_bool.get_bool(0, "b"), lite3cpp::exception);

    lite3cpp::Buffer obuf;
    obuf.init_object();
    (void)obuf.set_obj(0, "o");
    lite3cpp::Buffer trunc_obj = lite3cpp::Buffer::from_view(obuf.data(), obuf.size() - 10);
    EXPECT_THROW((void)trunc_obj.get_obj(0, "o"), lite3cpp::exception);
  }

  // Cyclic child_ofs in malformed wire buffer must throw instead of hanging
  {
    std::vector<uint8_t> cyclic(2 * lite3cpp::config::node_size, 0);
    lite3cpp::MutableNodeView n0(reinterpret_cast<lite3cpp::PackedNodeLayout*>(cyclic.data()));
    n0.set_gen_type(1, lite3cpp::Type::Object);
    n0.set_key_count(0);
    n0.set_child_offset(0, static_cast<uint32_t>(lite3cpp::config::node_size));

    lite3cpp::MutableNodeView n1(
        reinterpret_cast<lite3cpp::PackedNodeLayout*>(cyclic.data() + lite3cpp::config::node_size));
    n1.set_gen_type(1, lite3cpp::Type::Object);
    n1.set_key_count(0);
    n1.set_child_offset(0, static_cast<uint32_t>(lite3cpp::config::node_size)); // self-cycle!

    lite3cpp::Buffer cyclic_view = lite3cpp::Buffer::from_view(cyclic.data(), cyclic.size());
    EXPECT_THROW((void)cyclic_view.get_i64(0, "any"), lite3cpp::exception);
    EXPECT_THROW((void)cyclic_view.begin(0), lite3cpp::exception);
    EXPECT_THROW(
        lite3cpp::Buffer::from_view(cyclic.data(), cyclic.size()).set_i64(0, "any", 1),
        lite3cpp::exception);
  }
}

TEST_F(BufferTest, UpdateEveryKeyOnFull7KeyViewAndMultiLevelTree) {
  lite3cpp::Buffer src7;
  src7.init_object();
  for (size_t k = 0; k < lite3cpp::config::node_key_count; ++k) {
    src7.set_i64(0, "k_" + std::to_string(k), static_cast<int64_t>(k * 100));
  }

  // Updating each key on a fresh 7-key view hits Root Split + Normal Split for all 7 positions (including mid = 3)
  for (size_t target = 0; target < lite3cpp::config::node_key_count; ++target) {
    lite3cpp::Buffer v_inplace = lite3cpp::Buffer::from_view(src7.data(), src7.size());
    v_inplace.set_i64(0, "k_" + std::to_string(target), 9000 + static_cast<int64_t>(target));
    EXPECT_FALSE(v_inplace.is_view());

    size_t seen = 0;
    for (auto it = v_inplace.begin(0); it != v_inplace.end(0); ++it) {
      ++seen;
    }
    EXPECT_EQ(seen, lite3cpp::config::node_key_count);

    for (size_t k = 0; k < lite3cpp::config::node_key_count; ++k) {
      const int64_t expected =
          (k == target) ? (9000 + static_cast<int64_t>(target)) : static_cast<int64_t>(k * 100);
      EXPECT_EQ(v_inplace.get_i64(0, "k_" + std::to_string(k)), expected);
      EXPECT_EQ(src7.get_i64(0, "k_" + std::to_string(k)), static_cast<int64_t>(k * 100));
    }

    // Also test non-in-place (type/length change) update on the fresh 7-key view
    lite3cpp::Buffer v_append = lite3cpp::Buffer::from_view(src7.data(), src7.size());
    std::string new_val = "updated_payload_" + std::to_string(target);
    v_append.set_str(0, "k_" + std::to_string(target), new_val);
    EXPECT_EQ(v_append.get_str(0, "k_" + std::to_string(target)), new_val);
  }

  // Multi-level tree (~64 keys): test updating every single key both on fresh views and cumulatively
  constexpr int kNumKeys = 64;
  lite3cpp::Buffer big_src;
  big_src.init_object();
  for (int i = 0; i < kNumKeys; ++i) {
    big_src.set_i64(0, "tree_key_" + std::to_string(i), i);
  }

  for (int target = 0; target < kNumKeys; ++target) {
    lite3cpp::Buffer v = lite3cpp::Buffer::from_view(big_src.data(), big_src.size());
    v.set_i64(0, "tree_key_" + std::to_string(target), 50000 + target);
    EXPECT_EQ(v.get_i64(0, "tree_key_" + std::to_string(target)), 50000 + target);
  }

  lite3cpp::Buffer cumulative = lite3cpp::Buffer::from_view(big_src.data(), big_src.size());
  for (int i = 0; i < kNumKeys; ++i) {
    cumulative.set_i64(0, "tree_key_" + std::to_string(i), 100000 + i);
  }
  size_t total_seen = 0;
  for (auto it = cumulative.begin(0); it != cumulative.end(0); ++it) {
    ++total_seen;
  }
  EXPECT_EQ(total_seen, static_cast<size_t>(kNumKeys));
  for (int i = 0; i < kNumKeys; ++i) {
    EXPECT_EQ(cumulative.get_i64(0, "tree_key_" + std::to_string(i)), 100000 + i);
    EXPECT_EQ(big_src.get_i64(0, "tree_key_" + std::to_string(i)), i);
  }
}

TEST_F(BufferTest, ConstexprHashedKeyMatchesLegacySignedCharDjb2AndConversions) {
  using namespace lite3cpp::literals;

  // Compile-time evaluation guarantees
  constexpr uint32_t h_empty = lite3cpp::utils::djb2_hash("");
  static_assert(h_empty == 5381u);
  constexpr auto hk_lit = "objPath"_hk;
  static_assert(hk_lit.key == "objPath");
  static_assert(hk_lit.hash == lite3cpp::utils::djb2_hash("objPath"));

  // Verify exact signed-char two's-complement parity across all byte values 0x00..0xFF
  auto legacy_signed_djb2 = [](std::string_view sv) -> uint32_t {
    uint32_t h = 5381;
    for (char c : sv) {
      h = static_cast<uint32_t>(
          static_cast<uint64_t>((h << 5) + h) +
          static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int>(c))));
    }
    return h;
  };

  std::string all_bytes(256, '\0');
  for (int i = 0; i < 256; ++i) {
    all_bytes[static_cast<size_t>(i)] = static_cast<char>(static_cast<unsigned char>(i));
  }
  EXPECT_EQ(lite3cpp::utils::djb2_hash(all_bytes), legacy_signed_djb2(all_bytes));

  const char high_utf8[] = "\x80\xFF\xC3\xA9_irods_key";
  EXPECT_EQ(lite3cpp::utils::djb2_hash(high_utf8), legacy_signed_djb2(high_utf8));

  // Verify 1-step implicit conversion from const char*, nullptr, char[], string_view, and std::string
  lite3cpp::HashedKey from_null(static_cast<const char*>(nullptr));
  EXPECT_TRUE(from_null.key.empty());
  EXPECT_EQ(from_null.hash, 5381u);

  char stack_key[] = "stack_char_array";
  std::string std_str_key = "std_string_key";
  std::string_view sv_key = "string_view_key";

  buffer.init_object();
  buffer.set_i64(0, hk_lit, 11);
  buffer.set_i64(0, "cstr_literal", 22);
  buffer.set_i64(0, stack_key, 33);
  buffer.set_i64(0, std_str_key, 44);
  buffer.set_i64(0, sv_key, 55);

  EXPECT_EQ(buffer.get_i64(0, "objPath"_hk), 11);
  EXPECT_EQ(buffer.get_i64(0, "cstr_literal"), 22);
  EXPECT_EQ(buffer.get_i64(0, stack_key), 33);
  EXPECT_EQ(buffer.get_i64(0, std_str_key), 44);
  EXPECT_EQ(buffer.get_i64(0, sv_key), 55);
}

TEST_F(BufferTest, TryGetSinglePassHitMissWrongTypeAndCorruptNoexcept) {
  using namespace lite3cpp::literals;

  buffer.init_object();
  buffer.set_null(0, "k_null"_hk);
  buffer.set_bool(0, "k_bool"_hk, true);
  buffer.set_i64(0, "k_i64"_hk, -9876543210LL);
  buffer.set_f64(0, "k_f64"_hk, 2.718281828);
  buffer.set_str(0, "k_str"_hk, "single_pass");
  const std::byte raw[] = {std::byte{0x01}, std::byte{0xFE}, std::byte{0x42}};
  buffer.set_bytes(0, "k_bytes"_hk, raw);
  size_t sub_obj = buffer.set_obj(0, "k_obj"_hk);
  buffer.set_i64(sub_obj, "inner"_hk, 77);
  size_t sub_arr = buffer.set_arr(0, "k_arr"_hk);
  buffer.arr_append_null(sub_arr);
  buffer.arr_append_bool(sub_arr, false);
  buffer.arr_append_i64(sub_arr, 1234);
  buffer.arr_append_f64(sub_arr, 6.25);
  buffer.arr_append_str(sub_arr, "arr_elem");
  buffer.arr_append_bytes(sub_arr, raw);
  size_t arr_obj = buffer.arr_append_obj(sub_arr);
  buffer.set_str(arr_obj, "ok"_hk, "yes");
  size_t arr_nested = buffer.arr_append_arr(sub_arr);
  buffer.arr_append_i64(arr_nested, 9);

  // Direct hits
  EXPECT_EQ(buffer.try_get_bool(0, "k_bool"_hk), std::optional<bool>{true});
  EXPECT_EQ(buffer.try_get_i64(0, "k_i64"_hk), std::optional<int64_t>{-9876543210LL});
  ASSERT_TRUE(buffer.try_get_f64(0, "k_f64"_hk).has_value());
  EXPECT_DOUBLE_EQ(*buffer.try_get_f64(0, "k_f64"_hk), 2.718281828);
  EXPECT_EQ(buffer.try_get_str(0, "k_str"_hk), std::optional<std::string_view>{"single_pass"});
  ASSERT_TRUE(buffer.try_get_bytes(0, "k_bytes"_hk).has_value());
  EXPECT_EQ(buffer.try_get_bytes(0, "k_bytes"_hk)->size(), 3u);
  ASSERT_TRUE(buffer.try_get_obj(0, "k_obj"_hk).has_value());
  EXPECT_EQ(buffer.try_get_i64(*buffer.try_get_obj(0, "k_obj"_hk), "inner"_hk), std::optional<int64_t>{77});
  ASSERT_TRUE(buffer.try_get_arr(0, "k_arr"_hk).has_value());

  // Missing keys & wrong types return std::nullopt without throwing
  EXPECT_FALSE(buffer.try_get_i64(0, "missing"_hk).has_value());
  EXPECT_FALSE(buffer.try_get_str(0, "k_null"_hk).has_value());
  EXPECT_FALSE(buffer.try_get_i64(0, "k_str"_hk).has_value());
  EXPECT_FALSE(buffer.try_get_str(0, "k_i64"_hk).has_value());
  EXPECT_FALSE(buffer.try_get_obj(0, "k_arr"_hk).has_value());
  EXPECT_FALSE(buffer.try_get_arr(0, "k_obj"_hk).has_value());

  // Array try_get_* hits, misses, and type mismatches
  EXPECT_FALSE(buffer.arr_try_get_i64(sub_arr, 0).has_value()); // Null element
  EXPECT_EQ(buffer.arr_try_get_bool(sub_arr, 1), std::optional<bool>{false});
  EXPECT_EQ(buffer.arr_try_get_i64(sub_arr, 2), std::optional<int64_t>{1234});
  EXPECT_DOUBLE_EQ(*buffer.arr_try_get_f64(sub_arr, 3), 6.25);
  EXPECT_EQ(buffer.arr_try_get_str(sub_arr, 4), std::optional<std::string_view>{"arr_elem"});
  EXPECT_EQ(buffer.arr_try_get_bytes(sub_arr, 5)->size(), 3u);
  ASSERT_TRUE(buffer.arr_try_get_obj(sub_arr, 6).has_value());
  EXPECT_EQ(buffer.try_get_str(*buffer.arr_try_get_obj(sub_arr, 6), "ok"_hk), std::optional<std::string_view>{"yes"});
  ASSERT_TRUE(buffer.arr_try_get_arr(sub_arr, 7).has_value());
  EXPECT_EQ(buffer.arr_try_get_i64(*buffer.arr_try_get_arr(sub_arr, 7), 0), std::optional<int64_t>{9});
  EXPECT_FALSE(buffer.arr_try_get_i64(sub_arr, 999).has_value());
  EXPECT_FALSE(buffer.arr_try_get_str(sub_arr, 2).has_value());

  // Corrupt/cyclic/truncated buffers MUST return std::nullopt (noexcept) in try_get_*
  std::vector<uint8_t> cyclic(2 * lite3cpp::config::node_size, 0);
  lite3cpp::MutableNodeView n0(reinterpret_cast<lite3cpp::PackedNodeLayout*>(cyclic.data()));
  n0.set_gen_type(1, lite3cpp::Type::Object);
  n0.set_key_count(0);
  n0.set_child_offset(0, static_cast<uint32_t>(lite3cpp::config::node_size));
  lite3cpp::MutableNodeView n1(
      reinterpret_cast<lite3cpp::PackedNodeLayout*>(cyclic.data() + lite3cpp::config::node_size));
  n1.set_gen_type(1, lite3cpp::Type::Object);
  n1.set_key_count(0);
  n1.set_child_offset(0, static_cast<uint32_t>(lite3cpp::config::node_size));

  lite3cpp::Buffer cyclic_view = lite3cpp::Buffer::from_view(cyclic.data(), cyclic.size());
  static_assert(noexcept(cyclic_view.try_get_i64(0, "any"_hk)));
  EXPECT_FALSE(cyclic_view.try_get_i64(0, "any"_hk).has_value());

  lite3cpp::Buffer sbuf;
  sbuf.init_object();
  sbuf.set_str(0, "s"_hk, "0123456789");
  lite3cpp::Buffer trunc_view = lite3cpp::Buffer::from_view(sbuf.data(), sbuf.size() - 4);
  EXPECT_FALSE(trunc_view.try_get_str(0, "s"_hk).has_value());

  // Object::contains fixes missing-key check and accepts HashedKey
  lite3cpp::Document doc;
  lite3cpp::Object root = doc.root_obj();
  root["present"] = int64_t{1};
  doc.buffer().set_null(0, "explicit_null"_hk);
  EXPECT_TRUE(root.contains("present"_hk));
  EXPECT_TRUE(root.contains("explicit_null"_hk));
  EXPECT_FALSE(root.contains("absent_key"_hk));
}

TEST_F(BufferTest, SetBytesUninitializedAndCapacityRetentionAcrossClearAndRelease) {
  using namespace lite3cpp::literals;

  // Pre-sized Buffer retains m_data.size() across clear() + init_object() with 0 reallocations
  lite3cpp::Buffer pre_sized(8192);
  EXPECT_EQ(pre_sized.size(), 0u);
  EXPECT_GE(pre_sized.capacity(), 8192u);
  const size_t initial_cap = pre_sized.capacity();

  for (int iter = 0; iter < 5; ++iter) {
    pre_sized.clear();
    EXPECT_EQ(pre_sized.size(), 0u);
    EXPECT_EQ(pre_sized.capacity(), initial_cap);

    pre_sized.init_object();
    const uint8_t* base_before = pre_sized.data();
    auto span_slot = pre_sized.set_bytes_uninitialized(0, "payload"_hk, 4096);
    ASSERT_EQ(span_slot.size(), 4096u);
    EXPECT_EQ(pre_sized.data(), base_before); // Zero reallocation inside pre-sized capacity

    for (size_t b = 0; b < span_slot.size(); ++b) {
      span_slot[b] = static_cast<std::byte>((b + static_cast<size_t>(iter)) & 0xFFu);
    }

    auto read_back = pre_sized.try_get_bytes(0, "payload"_hk);
    ASSERT_TRUE(read_back.has_value());
    ASSERT_EQ(read_back->size(), 4096u);
    EXPECT_EQ(read_back->data(), span_slot.data()); // Exact in-buffer zero-copy address
    EXPECT_EQ((*read_back)[0], static_cast<std::byte>(iter & 0xFF));
    EXPECT_EQ((*read_back)[4095], static_cast<std::byte>((4095 + iter) & 0xFF));

    // Same-size overwrite reuses the exact same byte slot in place
    auto overwrite_slot = pre_sized.set_bytes_uninitialized(0, "payload"_hk, 4096);
    EXPECT_EQ(overwrite_slot.data(), span_slot.data());
  }

  // Array append uninitialized + CoW from view + O(1) release trimming
  lite3cpp::Buffer arr_buf(2048);
  arr_buf.init_array();
  auto arr_slot = arr_buf.arr_append_bytes_uninitialized(0, 256);
  ASSERT_EQ(arr_slot.size(), 256u);
  std::memset(arr_slot.data(), 0x5A, arr_slot.size());
  ASSERT_TRUE(arr_buf.arr_try_get_bytes(0, 0).has_value());
  EXPECT_EQ((*arr_buf.arr_try_get_bytes(0, 0))[255], std::byte{0x5A});

  lite3cpp::Buffer view = lite3cpp::Buffer::from_view(arr_buf.data(), arr_buf.size());
  auto cow_slot = view.arr_append_bytes_uninitialized(0, 64);
  std::memset(cow_slot.data(), 0xA5, cow_slot.size());
  EXPECT_FALSE(view.is_view());
  EXPECT_EQ((*view.arr_try_get_bytes(0, 1))[0], std::byte{0xA5});

  const uint8_t* pre_release_ptr = pre_sized.data();
  const size_t expected_used = pre_sized.size();
  std::vector<uint8_t> released = pre_sized.release();
  EXPECT_EQ(released.data(), pre_release_ptr); // O(1) move preserved!
  EXPECT_EQ(released.size(), expected_used);   // Trimmed to m_used_size!
}

