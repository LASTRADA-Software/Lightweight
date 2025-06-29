// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <reflection-cpp/reflection.hpp>

#include <any>
#include <iostream>

template <typename Record>
struct DifferenceView
{

    DifferenceView(Record const* lhs, Record const* rhs) noexcept:
        lhs(lhs),
        rhs(rhs)
    {
    }

    template <typename Callback>
    void iterate(Callback const& callback) noexcept
    {
        Reflection::template_for<0, Reflection::CountMembers<Record>>([&]<auto I>() {
            if (std::find(indexes.begin(), indexes.end(), I) != indexes.end())
            {
                callback(Reflection::GetMemberAt<I>(*lhs), Reflection::GetMemberAt<I>(*rhs));
            }
        });
    }

    void push_back(size_t ind) noexcept
    {
        indexes.push_back(ind);
    }

    std::vector<size_t> indexes;
    Record const* lhs;
    Record const* rhs;
};

template <typename Record>
DifferenceView<Record> CollectDifferences(Record const& left, Record const& right) noexcept
{

    DifferenceView<Record> view { &left, &right };

    Reflection::CollectDifferences(
        left, right, [&](size_t ind, [[maybe_unused]] auto const& left_elem, [[maybe_unused]] auto const& right_elem) {
            view.push_back(ind);
        });

    return view;
}
