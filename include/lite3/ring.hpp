#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lite3 {

using NodeID = uint32_t;

// FNV-1a Hash (Stable)
constexpr uint64_t fnv1a_64(std::string_view s) {
  uint64_t hash = 14695981039346656037ULL;
  for (char c : s) {
    hash ^= static_cast<uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

class ConsistentHash {
public:
  ConsistentHash(int replicas = 100) : replicas_(replicas) {}

  void add_node(NodeID node_id) {
    for (int i = 0; i < replicas_; ++i) {
      std::string vnode_key = std::to_string(node_id) + ":" + std::to_string(i);
      size_t hash = fnv1a_64(vnode_key);
      ring_[hash] = node_id;
    }
  }

  void remove_node(NodeID node_id) {
    for (auto it = ring_.begin(); it != ring_.end();) {
      if (it->second == node_id) {
        it = ring_.erase(it);
      } else {
        ++it;
      }
    }
  }

  NodeID get_node(std::string_view key) const {
    if (ring_.empty())
      return 0;

    size_t hash = fnv1a_64(key);
    auto it = ring_.lower_bound(hash);

    if (it == ring_.end()) {
      return ring_.begin()->second;
    }
    return it->second;
  }

  bool is_owner(std::string_view key, NodeID self_id) const {
    return get_node(key) == self_id;
  }

  size_t size() const { return ring_.size(); }

private:
  int replicas_;
  std::map<size_t, NodeID> ring_; // Hash -> NodeID
};

} // namespace lite3
