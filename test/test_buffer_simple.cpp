#include "buffer.hpp"
#include <gtest/gtest.h>

using namespace lite3cpp;

TEST(BufferSimpleTest, SetGetI64) {
  Buffer buffer;
  buffer.init_object();

  buffer.set_i64(0, "age", 30);
  EXPECT_EQ(buffer.get_i64(0, "age"), 30);
}

TEST(BufferSimpleTest, SetGetStr) {
  Buffer buffer;
  buffer.init_object();

  buffer.set_str(0, "name", "Jason");
  EXPECT_EQ(buffer.get_str(0, "name"), "Jason");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
