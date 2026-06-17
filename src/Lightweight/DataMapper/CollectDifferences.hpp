// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../Description.hpp"

#include <reflection-cpp/reflection.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Lightweight
{

template <typename Record>
struct DifferenceView
{

    DifferenceView(Record const* lhs, Record const* rhs) noexcept:
        lhs(lhs),
        rhs(rhs)
    {
    }

    template <typename Callback>
    void Iterate(Callback const& callback) noexcept
    {
        Reflection::template_for<0, RecordMemberCount<Record>>([&]<auto I>() {
            if (std::find(indexes.begin(), indexes.end(), I) != indexes.end())
            {
                callback(GetRecordMemberAt<I>(*lhs), GetRecordMemberAt<I>(*rhs));
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

} // namespace Lightweight
