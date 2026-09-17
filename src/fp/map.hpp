#pragma once
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace fp {

template <class K, class V>
std::optional<V> lookup(std::map<K, V> const &m, K const &k) {
  auto it = m.find(k);
  return it == m.end() ? std::nullopt : std::optional<V>(it->second);
}

template <class K, class V, class F>
std::map<K, V> map_values(std::map<K, V> const &m, F f) {
  std::map<K, V> out;
  for (auto const &[k, v] : m)
    out.emplace(k, f(v));
  return out;
}

template <class K, class V, class F>
std::map<K, V> filter_values(std::map<K, V> const &m, F pred) {
  std::map<K, V> out;
  for (auto const &[k, v] : m)
    if (pred(v))
      out.emplace(k, v);
  return out;
}

template <class K, class V, class F>
std::map<K, V> merge_with(std::map<K, V> const &a, std::map<K, V> const &b,
                          F combine) {
  std::map<K, V> out = a;
  for (auto const &[k, v] : b) {
    auto it = out.find(k);
    if (it == out.end())
      out.emplace(k, v);
    else
      it->second = combine(it->second, v);
  }
  return out;
}

template <class K, class V> std::vector<K> keys(std::map<K, V> const &m) {
  std::vector<K> out;
  out.reserve(m.size());
  for (auto const &[k, v] : m)
    out.push_back(k);
  return out;
}

template <class K, class V> std::vector<V> values(std::map<K, V> const &m) {
  std::vector<V> out;
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

} // namespace fp
