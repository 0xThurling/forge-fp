#pragma once
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fp {

// Associative-container helpers, generic over the map type (std::map or
// std::unordered_map). `group_by` returns an unordered_map, so these consume
// either kind.

template <class Map>
std::optional<typename Map::mapped_type>
lookup(Map const &m, typename Map::key_type const &k) {
  auto it = m.find(k);
  return it == m.end() ? std::nullopt
                       : std::optional<typename Map::mapped_type>(it->second);
}

template <class Map, class F> Map map_values(Map const &m, F f) {
  Map out;
  if constexpr (requires { out.reserve(m.size()); })
    out.reserve(m.size());
  for (auto const &[k, v] : m)
    out.emplace(k, f(v));
  return out;
}

template <class Map, class F> Map filter_values(Map const &m, F pred) {
  Map out;
  for (auto const &[k, v] : m)
    if (pred(v))
      out.emplace(k, v);
  return out;
}

template <class Map, class F>
Map merge_with(Map const &a, Map const &b, F combine) {
  Map out = a;
  for (auto const &[k, v] : b) {
    auto it = out.find(k);
    if (it == out.end())
      out.emplace(k, v);
    else
      it->second = combine(it->second, v);
  }
  return out;
}

template <class Map> std::vector<typename Map::key_type> keys(Map const &m) {
  std::vector<typename Map::key_type> out;
  out.reserve(m.size());
  for (auto const &[k, v] : m)
    out.push_back(k);
  return out;
}

template <class Map> std::vector<typename Map::mapped_type> values(Map const &m) {
  std::vector<typename Map::mapped_type> out;
  out.reserve(m.size());
  for (auto const &[k, v] : m)
    out.push_back(v);
  return out;
}

template <class K, class V>
std::map<K, V> to_map(std::vector<std::pair<K, V>> const &ps) {
  std::map<K, V> out;
  for (auto const &[k, v] : ps)
    out.emplace(k, v);
  return out;
}

template <class K, class V>
std::unordered_map<K, V>
to_unordered_map(std::vector<std::pair<K, V>> const &ps) {
  std::unordered_map<K, V> out;
  out.reserve(ps.size());
  for (auto const &[k, v] : ps)
    out.emplace(k, v);
  return out;
}

} // namespace fp
