// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Ruixiang Du
//
// Compile-time helpers: optional-hook detection and RTTI-free type names.
#ifndef CTFSM_TRAITS_HPP_
#define CTFSM_TRAITS_HPP_

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ctfsm {
namespace detail {

// Compile-time, RTTI-free type name extracted from the compiler's function
// signature. Falls back to "?" on unknown compilers.
template <class T>
constexpr std::string_view raw_type_name() {
#if defined(__clang__) || defined(__GNUC__)
  constexpr std::string_view p = __PRETTY_FUNCTION__;
  constexpr std::string_view key = "T = ";
  const auto start = p.find(key) + key.size();
  const auto end = p.find_first_of(";]", start);
  return p.substr(start, end - start);
#else
  return "?";
#endif
}

constexpr std::string_view strip_namespace(std::string_view n) noexcept {
  const auto pos = n.rfind("::");
  return pos == std::string_view::npos ? n : n.substr(pos + 2);
}

template <class T, class = void>
struct has_kname : std::false_type {};
template <class T>
struct has_kname<T, std::void_t<decltype(std::string_view{T::kName})>>
    : std::true_type {};

template <class S, class C, class = void>
struct has_on_enter : std::false_type {};
template <class S, class C>
struct has_on_enter<
    S, C, std::void_t<decltype(std::declval<S&>().OnEnter(std::declval<C&>()))>>
    : std::true_type {};

template <class S, class C, class = void>
struct has_on_exit : std::false_type {};
template <class S, class C>
struct has_on_exit<
    S, C, std::void_t<decltype(std::declval<S&>().OnExit(std::declval<C&>()))>>
    : std::true_type {};

template <class S, class C, class = void>
struct has_update : std::false_type {};
template <class S, class C>
struct has_update<
    S, C, std::void_t<decltype(std::declval<S&>().Update(std::declval<C&>()))>>
    : std::true_type {};

}  // namespace detail

// Human-readable name for a state/event/guard type: `T::kName` if the type
// defines it, else the namespace-stripped compile-time type name. No RTTI.
template <class T>
constexpr std::string_view name() noexcept {
  if constexpr (detail::has_kname<T>::value) {
    return std::string_view{T::kName};
  } else {
    return detail::strip_namespace(detail::raw_type_name<T>());
  }
}

}  // namespace ctfsm

#endif  // CTFSM_TRAITS_HPP_
