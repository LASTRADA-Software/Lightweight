#include <cstdlib>
#include <source_location>
#include <string>
#include <string_view>

namespace Demo
{

// ---------------------------------------------------------------------------------
// JoinStringLiterals<>

namespace detail
{
    template <std::array V>
    struct make_static
    {
        static constexpr auto value = V;
    };

    template <std::string_view const&... Strs>
    constexpr std::string_view Join()
    {
        constexpr auto joined_arr = []() {
            constexpr size_t len = (Strs.size() + ... + 0);
            std::array<char, len + 1> arr {};
            auto append = [i = 0U, &arr](auto const& s) mutable {
                for (auto c: s)
                    arr[i++] = c;
            };
            (append(Strs), ...);
            arr[len] = 0;
            return arr;
        }();
        auto& static_arr = make_static<joined_arr>::value;
        return { static_arr.data(), static_arr.size() - 1 };
    }
} // namespace detail

// Helper to get the value out
template <std::string_view const&... Strs>
inline constexpr auto JoinStringLiterals = detail::Join<Strs...>();

// ---------------------------------------------------------------------------------
// CountMebmers<>

namespace detail
{
    // This helper-struct is only used by CountMembers to count the number of members in an aggregate type
    struct Dummy final
    {
        template <class T>
        [[maybe_unused]] constexpr operator T() const;
    };

    template <class AggregateType, class... Args>
        requires(std::is_aggregate_v<AggregateType>)
    constexpr inline auto CountMembers = []() constexpr {
        if constexpr (requires { AggregateType { Args {}..., Dummy {} }; }) // NOLINT(modernize-use-designated-initializers)
            return CountMembers<AggregateType, Args..., Dummy>;
        else
            return sizeof...(Args);
    }();
} // namespace detail

template <class T>
    requires(std::is_aggregate_v<std::remove_cvref_t<T>>)
constexpr inline auto CountMembers = detail::CountMembers<std::remove_cvref_t<T>>;

// ---------------------------------------------------------------------------------
// ToTuple<>

constexpr size_t MaxReflectionMemerCount = 5;

template <class T, size_t N = CountMembers<T>>
    requires(N <= MaxReflectionMemerCount)
constexpr decltype(auto) ToTuple(T&& t) noexcept
{
    if constexpr (N == 0)
        return std::tuple {};

    else if constexpr (N == 1)
    {
        auto&& [p0] = std::forward<T>(t);
        return std::tie(p0);
    }
    else if constexpr (N == 2)
    {
        auto&& [p0, p1] = std::forward<T>(t);
        return std::tie(p0, p1);
    }
    else if constexpr (N == 3)
    {
        auto&& [p0, p1, p2] = std::forward<T>(t);
        return std::tie(p0, p1, p2);
    }
    else if constexpr (N == 4)
    {
        auto&& [p0, p1, p2, p3] = std::forward<T>(t);
        return std::tie(p0, p1, p2, p3);
    }
    else if constexpr (N == 5)
    {
        auto&& [p0, p1, p2, p3, p4] = std::forward<T>(t);
        return std::tie(p0, p1, p2, p3, p4);
    }
}

// ---------------------------------------------------------------------------------
// GetMemberAt<>

template <auto I, typename T>
constexpr decltype(auto) GetMemberAt(T&& t)
{
    return std::get<I>(ToTuple(std::forward<T>(t)));
}

// ---------------------------------------------------------------------------------
// FieldNameOf<>

namespace detail
{
    template <auto P>
        requires(std::is_member_pointer_v<decltype(P)>)
    consteval std::string_view GetName()
    {
#if defined(_MSC_VER) && !defined(__clang__)
        if constexpr (std::is_member_object_pointer_v<decltype(P)>)
        {
            using T = detail::remove_member_pointer<std::decay_t<decltype(P)>>::type;
            constexpr auto p = P;
            return detail::get_name_msvc<T, &(detail::External<T>.*p)>();
        }
        else
        {
            using T = detail::remove_member_pointer<std::decay_t<decltype(P)>>::type;
            return detail::func_name_msvc<T, P>();
        }
#else
        // TODO: Use std::source_location when deprecating clang 14
        std::string_view str = std::source_location::current().function_name();
        str = str.substr(str.find('&') + 1);
        str = str.substr(0, str.find(']'));
        return str.substr(str.rfind("::") + 2);
#endif
    }

    template <auto E>
        requires(std::is_enum_v<decltype(E)>)
    consteval auto GetName()
    {
#if defined(_MSC_VER) && !defined(__clang__)
        std::string_view str = REFLECTION_PRETTY_FUNCTION;
        str = str.substr(str.rfind("::") + 2);
        str = str.substr(0, str.find('>'));
        return str.substr(str.find('<') + 1);
#else
        constexpr auto MarkerStart = std::string_view { "E = " };
        std::string_view str = std::source_location::current().function_name();
        str = str.substr(str.rfind(MarkerStart) + MarkerStart.size());
        str = str.substr(0, str.find(']'));
        return str;
#endif
    }
}

template <auto V>
constexpr std::string_view NameOf = detail::GetName<V>();

// ---------------------------------------------------------------------------------
// MemberClassType<>

namespace detail
{
    template <typename T>
    struct MemberClassTypeHelper;

    template <typename M, typename T>
    struct MemberClassTypeHelper<M T::*>
    {
        using type = std::remove_cvref_t<T>;
    };
} // namespace detail

template <typename T>
using MemberClassType = typename detail::MemberClassTypeHelper<T>::type;

// ---------------------------------------------------------------------------------
// TypeNameOf<>

namespace detail {
    // This helper-struct is only used by CountMembers to count the number of members in an aggregate type
    struct AnyType final
    {
        template <class T>
        [[maybe_unused]] constexpr operator T() const;
    };

    template <auto Ptr>
    [[nodiscard]] consteval auto MangledName()
    {
        return std::source_location::current().function_name();
    }

    template <class T>
    [[nodiscard]] consteval auto MangledName()
    {
        return std::source_location::current().function_name();
    }

    struct REFLE_REFLECTOR
    {
        int REFLE_FIELD;
    };

    struct reflect_type
    {
        static constexpr std::string_view name = MangledName<REFLE_REFLECTOR>();
        static constexpr auto end = name.substr(name.find("REFLE_REFLECTOR") + sizeof("REFLE_REFLECTOR") - 1);
#if defined(__GNUC__) || defined(__clang__)
        static constexpr auto begin = std::string_view { "T = " };
#else
        static constexpr auto begin = std::string_view { "Reflection::detail::MangledName<" };
#endif
    };
}

template <class T>
constexpr auto TypeNameOf = [] {
    constexpr std::string_view name = detail::MangledName<T>();
    constexpr auto begin = name.find(detail::reflect_type::end);
    constexpr auto tmp = name.substr(0, begin);
#if defined(__GNUC__) || defined(__clang__)
    return tmp.substr(tmp.rfind(detail::reflect_type::begin) + detail::reflect_type::begin.size());
#else
    constexpr auto name_with_keyword =
        tmp.substr(tmp.rfind(detail::reflect_type::begin) + detail::reflect_type::begin.size());
    return name_with_keyword.substr(name_with_keyword.find(' ') + 1);
#endif
}();

// ---------------------------------------------------------------------------------
// RecordTableName<>

namespace detail
{
    template <typename Record>
    struct RecordTableNameImpl
    {
        static constexpr std::string_view Value = []() {
            if constexpr (requires { Record::TableName; })
                return Record::TableName;
            else
                return []() {
                    auto const typeName = TypeNameOf<Record>;
                    if (auto const i = typeName.rfind(':'); i != std::string_view::npos)
                        return typeName.substr(i + 1);
                    return typeName;
                }();
        }();
    };
} // namespace detail

template <typename Record>
constexpr std::string_view RecordTableName = detail::RecordTableNameImpl<Record>::Value;

// ---------------------------------------------------------------------------------
// FullNameOf<>

namespace detail
{
    template <auto ReferencedField>
    struct FullyQualifiedNameOfImpl
    {
#if defined(LIGHTWEIGHT_CXX26_REFLECTION)
        static constexpr auto ClassName = RecordTableName<typename[:std::meta::parent_of(ReferencedField):]>;
#else
        static constexpr auto ClassName = RecordTableName<MemberClassType<decltype(ReferencedField)>>;
#endif
        static constexpr auto FieldName = NameOf<ReferencedField>;
        static constexpr auto StorageSize = ClassName.size() + FieldName.size() + 6;

        // Holds the full field name in the format "ClassName"."FieldName"
        static constexpr auto Storage = []() constexpr -> std::array<char, StorageSize> {
            // clang-format off
            auto storage = std::array<char, StorageSize> {};
            std::ranges::copy("\"",      storage.begin());
            std::ranges::copy(ClassName, storage.begin() + 1);
            std::ranges::copy("\".\"",   storage.begin() + 1 + ClassName.size());
            std::ranges::copy(FieldName, storage.begin() + 1 + ClassName.size() + 3);
            std::ranges::copy("\"",      storage.begin() + 1 + ClassName.size() + 3 + FieldName.size());
            storage.back() = '\0';
            // clang-format on
            return storage;
        }();
        static constexpr auto value = std::string_view(Storage.data(), Storage.size() - 1);
    };
} // namespace detail

template <auto ReferencedField>
constexpr inline auto FullyQualifiedNameOf = detail::FullyQualifiedNameOfImpl<ReferencedField>::value;

} // namespace Demo

#include <print>

template <typename T>
void Inspect()
{
    std::println("Type: {}", typeid(T).name());
    std::println("Number of members: {}", Demo::CountMembers<T>);
    std::println();
}

struct Person
{
    std::string FirstName {};
    std::string LastName {};
    int SecretNumber {};
};

int main()
{
    Inspect<Person>();
    std::println("{}", Demo::NameOf<&Person::FirstName>);
    std::println("{}", Demo::NameOf<&Person::SecretNumber>);

    return EXIT_SUCCESS;
}
