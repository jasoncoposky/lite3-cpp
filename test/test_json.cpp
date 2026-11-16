#include "buffer.hpp"
#include "exception.hpp"
#include "json.hpp"
#include "utils/hex.hpp"
#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(JsonTest, EmptyObjectToJson) {
  lite3cpp::Buffer buffer;
  buffer.init_object();
  std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
  EXPECT_EQ(json_str, "{}");
}

TEST(JsonTest, EmptyObjectFromJson) {
  std::string json_str = "{}";
  lite3cpp::Buffer buffer = lite3cpp::lite3_json::from_json_string(json_str);
  EXPECT_EQ(buffer.size(), lite3cpp::config::node_size);
}

TEST(JsonTest, StringObjectRoundTrip) {
  lite3cpp::Buffer buffer;
  buffer.init_object();
  buffer.set_str(0, "name", "test_string");
  std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
  EXPECT_EQ(json_str, R"({"name":"test_string"})");

  lite3cpp::Buffer parsed = lite3cpp::lite3_json::from_json_string(json_str);
  EXPECT_EQ(parsed.get_str(0, "name"), "test_string");
}

TEST(JsonTest, ArrayFieldRoundTrip) {
  lite3cpp::Buffer buffer;
  buffer.init_object();
  size_t arr_ofs = buffer.set_arr(0, "items");
  buffer.arr_append_i64(arr_ofs, 42);
  buffer.arr_append_str(arr_ofs, "hello");
  buffer.arr_append_bool(arr_ofs, true);
  buffer.arr_append_null(arr_ofs);

  std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
  EXPECT_EQ(json_str, R"({"items":[42,"hello",true,null]})");

  lite3cpp::Buffer parsed = lite3cpp::lite3_json::from_json_string(json_str);
  size_t parsed_arr = parsed.get_arr(0, "items");
  EXPECT_EQ(parsed.arr_get_i64(parsed_arr, 0), 42);
  EXPECT_EQ(parsed.arr_get_str(parsed_arr, 1), "hello");
  EXPECT_EQ(parsed.arr_get_bool(parsed_arr, 2), true);
  EXPECT_EQ(parsed.arr_get_type(parsed_arr, 3), lite3cpp::Type::Null);
}

TEST(JsonTest, RootArrayRoundTrip) {
  lite3cpp::Buffer buffer;
  buffer.init_array();
  buffer.arr_append_i64(0, 100);
  buffer.arr_append_str(0, "world");
  buffer.arr_append_bool(0, false);

  std::string json_str = lite3cpp::lite3_json::to_json_string(buffer, 0);
  EXPECT_EQ(json_str, R"([100,"world",false])");

  lite3cpp::Buffer parsed = lite3cpp::lite3_json::from_json_string(json_str);
  EXPECT_EQ(parsed.arr_get_i64(0, 0), 100);
  EXPECT_EQ(parsed.arr_get_str(0, 1), "world");
  EXPECT_EQ(parsed.arr_get_bool(0, 2), false);
}

TEST(JsonTest, HexEncodeDecodeLutRoundTripAllByteValues) {
  std::vector<std::byte> all_bytes(256);
  for (size_t i = 0; i < 256; ++i) {
    all_bytes[i] = static_cast<std::byte>(i);
  }
  std::string encoded = lite3cpp::utils::hex_encode(all_bytes);
  ASSERT_EQ(encoded.size(), 512u);
  EXPECT_EQ(encoded.substr(0, 6), "000102");
  EXPECT_EQ(encoded.substr(506, 6), "fdfeff");

  std::vector<std::byte> decoded = lite3cpp::utils::hex_decode(encoded);
  EXPECT_EQ(decoded, all_bytes);
  EXPECT_TRUE(lite3cpp::utils::hex_encode({}).empty());
}

TEST(JsonTest, LargeMixedArray1000ElementsAndBytesHexToJson) {
  lite3cpp::Buffer buf;
  buf.init_object();
  const std::byte bin[] = {std::byte{0x00}, std::byte{0x0A}, std::byte{0xBC}, std::byte{0xFF}};
  buf.set_bytes(0, "sig", bin);
  buf.set_bytes(0, "empty_sig", std::span<const std::byte>{});

  size_t arr = buf.set_arr(0, "events");
  for (int i = 0; i < 1000; ++i) {
    if (i % 4 == 0) {
      buf.arr_append_i64(arr, i);
    } else if (i % 4 == 1) {
      buf.arr_append_str(arr, "ev_" + std::to_string(i));
    } else if (i % 4 == 2) {
      size_t sub = buf.arr_append_obj(arr);
      buf.set_i64(sub, "idx", i);
    } else {
      buf.arr_append_bytes(arr, bin);
    }
  }

  std::string json_str = lite3cpp::lite3_json::to_json_string(buf, 0);
  EXPECT_NE(json_str.find(R"("sig":"000abcff")"), std::string::npos);
  EXPECT_NE(json_str.find(R"("empty_sig":"")"), std::string::npos);

  lite3cpp::Buffer roundtrip = lite3cpp::lite3_json::from_json_string(json_str);
  EXPECT_EQ(roundtrip.get_str(0, "sig"), "000abcff");
  EXPECT_TRUE(roundtrip.get_str(0, "empty_sig").empty());
  size_t rt_arr = roundtrip.get_arr(0, "events");
  EXPECT_EQ(roundtrip.arr_get_i64(rt_arr, 0), 0);
  EXPECT_EQ(roundtrip.arr_get_str(rt_arr, 1), "ev_1");
  EXPECT_EQ(roundtrip.get_i64(roundtrip.arr_get_obj(rt_arr, 2), "idx"), 2);
  EXPECT_EQ(roundtrip.arr_get_str(rt_arr, 3), "000abcff");
  EXPECT_EQ(roundtrip.arr_get_i64(rt_arr, 996), 996);
  EXPECT_EQ(roundtrip.arr_get_str(rt_arr, 999), "000abcff");

  // Also exercise root-level Type::Array O(N) loop with 1000 mixed elements + nested array + bytes
  lite3cpp::Buffer root_arr;
  root_arr.init_array();
  for (int i = 0; i < 1000; ++i) {
    if (i % 5 == 0) {
      root_arr.arr_append_i64(0, i * 2);
    } else if (i % 5 == 1) {
      root_arr.arr_append_str(0, "r_" + std::to_string(i));
    } else if (i % 5 == 2) {
      root_arr.arr_append_bytes(0, bin);
    } else if (i % 5 == 3) {
      size_t nested_a = root_arr.arr_append_arr(0);
      root_arr.arr_append_bool(nested_a, (i % 2) == 0);
      root_arr.arr_append_null(nested_a);
      root_arr.arr_append_f64(nested_a, 1.5);
    } else {
      size_t nested_o = root_arr.arr_append_obj(0);
      root_arr.set_i64(nested_o, "v", i);
    }
  }

  std::string root_json = lite3cpp::lite3_json::to_json_string(root_arr, 0);
  lite3cpp::Buffer root_rt = lite3cpp::lite3_json::from_json_string(root_json);
  EXPECT_EQ(root_rt.arr_get_i64(0, 0), 0);
  EXPECT_EQ(root_rt.arr_get_str(0, 1), "r_1");
  EXPECT_EQ(root_rt.arr_get_str(0, 2), "000abcff");
  size_t rt_sub_arr = root_rt.arr_get_arr(0, 3);
  EXPECT_EQ(root_rt.arr_get_bool(rt_sub_arr, 0), false);
  EXPECT_EQ(root_rt.arr_get_type(rt_sub_arr, 1), lite3cpp::Type::Null);
  EXPECT_DOUBLE_EQ(root_rt.arr_get_f64(rt_sub_arr, 2), 1.5);
  EXPECT_EQ(root_rt.get_i64(root_rt.arr_get_obj(0, 4), "v"), 4);
  EXPECT_EQ(root_rt.arr_get_i64(0, 995), 1990);
  EXPECT_EQ(root_rt.get_i64(root_rt.arr_get_obj(0, 999), "v"), 999);

  // Corrupt nested node header type mismatch (Type::Array tag -> Type::Object node and vice versa)
  {
    lite3cpp::Buffer valid_nested;
    valid_nested.init_object();
    size_t child_arr = valid_nested.set_arr(0, "arr");
    size_t child_obj = valid_nested.set_obj(0, "obj");

    std::vector<uint8_t> bad_arr(valid_nested.data(), valid_nested.data() + valid_nested.size());
    lite3cpp::MutableNodeView(reinterpret_cast<lite3cpp::PackedNodeLayout*>(bad_arr.data() + child_arr))
        .set_gen_type(1, lite3cpp::Type::Object);
    EXPECT_THROW(
        (void)lite3cpp::lite3_json::to_json_string(
            lite3cpp::Buffer::from_view(bad_arr.data(), bad_arr.size()), 0),
        lite3cpp::exception);

    std::vector<uint8_t> bad_obj(valid_nested.data(), valid_nested.data() + valid_nested.size());
    lite3cpp::MutableNodeView(reinterpret_cast<lite3cpp::PackedNodeLayout*>(bad_obj.data() + child_obj))
        .set_gen_type(1, lite3cpp::Type::Array);
    EXPECT_THROW(
        (void)lite3cpp::lite3_json::to_json_string(
            lite3cpp::Buffer::from_view(bad_obj.data(), bad_obj.size()), 0),
        lite3cpp::exception);
  }
}

TEST(JsonTest, FromJsonStringViewNonNullTerminatedSliceAndEmpty) {
  // Non-null-terminated substring slice inside surrounding non-JSON sentinel characters
  const std::string backing = R"(XXX{"a":42,"nested":{"msg":"hello"},"arr":[1,2,3]}YYY)";
  std::string_view slice(backing.data() + 3, backing.size() - 6);
  ASSERT_EQ(slice.front(), '{');
  ASSERT_EQ(slice.back(), '}');
  // Confirm byte immediately past slice is 'Y' (not '\0')
  ASSERT_EQ(slice.data()[slice.size()], 'Y');

  lite3cpp::Buffer parsed = lite3cpp::lite3_json::from_json_string(slice);
  EXPECT_EQ(parsed.get_i64(0, "a"), 42);
  size_t nested = parsed.get_obj(0, "nested");
  EXPECT_EQ(parsed.get_str(nested, "msg"), "hello");
  size_t arr = parsed.get_arr(0, "arr");
  EXPECT_EQ(parsed.arr_get_i64(arr, 0), 1);
  EXPECT_EQ(parsed.arr_get_i64(arr, 2), 3);

  // Also test const char* literal and std::string overloads remain unambiguous
  lite3cpp::Buffer from_lit = lite3cpp::lite3_json::from_json_string(R"({"lit":true})");
  EXPECT_TRUE(from_lit.get_bool(0, "lit"));
  std::string std_s = R"({"str":"ok"})";
  lite3cpp::Buffer from_str = lite3cpp::lite3_json::from_json_string(std_s);
  EXPECT_EQ(from_str.get_str(0, "str"), "ok");

  // Empty and invalid string_view slices throw lite3cpp::exception
  EXPECT_THROW((void)lite3cpp::lite3_json::from_json_string(std::string_view{}),
               lite3cpp::exception);
  EXPECT_THROW((void)lite3cpp::lite3_json::from_json_string(std::string_view("", 0)),
               lite3cpp::exception);
  std::string_view bad_slice(backing.data(), 10);
  EXPECT_THROW((void)lite3cpp::lite3_json::from_json_string(bad_slice),
               lite3cpp::exception);
}
