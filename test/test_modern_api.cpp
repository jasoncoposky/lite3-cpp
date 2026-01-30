#include "document.hpp"
#include <gtest/gtest.h>
#include <iostream>

using namespace lite3cpp;

TEST(ModernAPITest, BasicObjectAssignment) {
  Document doc;
  Object root = doc.root_obj();

  root["name"] = "Jason";
  root["age"] = (int64_t)30;
  root["pi"] = 3.14159;
  root["active"] = true;

  ASSERT_TRUE(root["name"] == "Jason");
  ASSERT_TRUE(root["age"] == 30LL);
  ASSERT_DOUBLE_EQ(static_cast<double>(root["pi"]), 3.14159);
  ASSERT_TRUE(root["active"] == true);
}

TEST(ModernAPITest, NestedObjectAssignment) {
  Document doc;
  Object root = doc.root_obj();

  // "user" does not exist. operator[] should return a proxy.
  // When we access ["name"], it's a nested key.
  // Wait, current operator[] implementation for MISSING key returns a proxy
  // with m_key. But if we access a child of that proxy? root["user"] ->
  // Value(ofs=0, key="user"). root["user"]["name"] -> operator[] on the proxy.
  // The proxy needs to resolve itself first.

  // My implementation:
  // if (m_offset == 0) { if (!key.empty()) try get_obj catch set_obj... }

  // So root["user"] will call set_obj(0, "user") -> returns new offset.
  // Then returns Value(new_ofs).
  // Then ["name"] returns Value(new_ofs, key="name").
  // Then = "John" calls set_str.

  root["user"]["name"] = "John";

  ASSERT_TRUE(root["user"]["name"] == "John");
}

TEST(ModernAPITest, ArrayWorkload) {
  Document doc;
  doc.buffer().init_array();
  Array root = doc.root_arr();

  root.push_back(100);
  root.push_back("hello");

  ASSERT_TRUE(root[0] == 100LL);
  ASSERT_TRUE(root[1] == "hello");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
