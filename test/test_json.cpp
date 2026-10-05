#include "buffer.hpp"
#include "json.hpp"
#include <gtest/gtest.h>
#include <string>

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