// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <concepts>
#include <string>

template <typename S>
concept SqlStringInterface =
requires(S s,
         S const cs,
         std::size_t n,
         S::value_type ch,
         std::basic_string_view<typename S::value_type> sv)
{
    /* =========================
       Associated types
       ========================= */
    typename S::value_type;
    typename S::iterator;
    typename S::const_iterator;
    typename S::pointer_type;
    typename S::const_pointer_type;

    /* =========================
       Construction
       ========================= */
    std::is_default_constructible_v<S>;
    std::is_copy_constructible_v<S>;
    std::is_move_constructible_v<S>;

    /* =========================
       Capacity & size
       ========================= */
    { cs.size() } -> std::same_as<std::size_t>;
    { cs.capacity() } -> std::same_as<std::size_t>;
    { cs.empty() } -> std::same_as<bool>;

    /* =========================
       Modifiers
       ========================= */
    { s.clear() } -> std::same_as<void>;
    { s.resize(n) } -> std::same_as<void>;
    { s.setsize(n) } -> std::same_as<void>;
    { s.reserve(n) } -> std::same_as<void>;

    { s.push_back(ch) } -> std::same_as<void>;
    { s.pop_back() } -> std::same_as<void>;

    /* =========================
       Element access
       ========================= */
    { s[n] } -> std::same_as<typename S::value_type&>;
    { cs[n] } -> std::same_as<typename S::value_type const&>;

    /* =========================
       Iterators & data
       ========================= */
    { s.begin() } -> std::same_as<typename S::iterator>;
    { s.end() } -> std::same_as<typename S::iterator>;
    { cs.begin() } -> std::same_as<typename S::const_iterator>;
    { cs.end() } -> std::same_as<typename S::const_iterator>;

    { s.data() } -> std::same_as<typename S::pointer_type>;
    { cs.data() } -> std::same_as<typename S::const_pointer_type>;
    { s.c_str() } -> std::same_as<typename S::const_pointer_type>;
    { cs.c_str() } -> std::same_as<typename S::const_pointer_type>;

    /* =========================
       String views & conversions
       ========================= */
    { cs.substr() } ->
        std::same_as<std::basic_string_view<typename S::value_type>>;

    { cs.str() } ->
        std::same_as<std::basic_string_view<typename S::value_type>>;

    { cs.ToString() } ->
        std::same_as<std::basic_string<typename S::value_type>>;

    { cs.ToStringView() } ->
        std::same_as<std::basic_string_view<typename S::value_type>>;

    static_cast<std::basic_string<typename S::value_type>>(cs);
    static_cast<std::basic_string_view<typename S::value_type>>(cs);

    /* =========================
       Comparisons
       ========================= */
    { cs == sv } -> std::same_as<bool>;
    { cs != sv } -> std::same_as<bool>;
};
