// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2021 Heal Research

#include "operon/operators/non_dominated_sorter/dominance_degree_sort.hpp"

namespace Operon {
    using Vec = Eigen::Matrix<int64_t, -1, 1, Eigen::ColMajor>;
    using Mat = Eigen::Matrix<int64_t, -1, -1, Eigen::ColMajor>;

    inline auto ComputeComparisonMatrix(Operon::Span<Operon::Individual const> pop, Mat const& idx, Eigen::Index colIdx) noexcept
    {
        auto const n = static_cast<Eigen::Index>(pop.size());
        Mat c = Mat::Zero(n, n);
        Mat::ConstColXpr b = idx.col(colIdx);
        c.row(b(0)).fill(1); // NOLINT
        for (auto i = 1; i < n; ++i) {
            if (pop[b(i)][colIdx] == pop[b(i-1)][colIdx]) {
                c.row(b(i)) = c.row(b(i-1));
            } else {
                for (auto j = i; j < n; ++j) {
                    c(b(i), b(j)) = 1;
                }
            }
        }
        return c;
    }

    inline auto ComparisonMatrixSum(Operon::Span<Operon::Individual const> pop, Mat const& idx) noexcept {
        Mat d = ComputeComparisonMatrix(pop, idx, 0);
        for (int i = 1; i < idx.cols(); ++i) {
            d.noalias() += ComputeComparisonMatrix(pop, idx, i);
        }
        return d;
    }

    inline auto ComputeDegreeMatrix(Operon::Span<Operon::Individual const> pop, Mat const& idx) noexcept
    {
        auto const n = static_cast<Eigen::Index>(pop.size());
        auto const m = static_cast<Eigen::Index>(pop.front().Fitness.size());
        Mat d = ComparisonMatrixSum(pop, idx);
        for (auto i = 0; i < n; ++i) {
            for (auto j = i; j < n; ++j) {
                if (d(i, j) == m && d(j, i) == m) {
                    d(i, j) = d(j, i) = 0;
                }
            }
        }
        return d;
    }


    auto DominanceDegreeSorter::Sort(Operon::Span<Operon::Individual const> pop) const -> NondominatedSorterBase::Result
    {
        auto const n = static_cast<Eigen::Index>(pop.size());
        auto const m = static_cast<Eigen::Index>(pop.front().Fitness.size());

        Mat idx = Vec::LinSpaced(n, 0, n-1).replicate(1, m);
        for (auto i = 0; i < m; ++i) {
            auto *data = idx.col(i).data();
            std::sort(data, data + n, [&](auto a, auto b) { return pop[a][i] < pop[b][i]; });
        }
        Mat d = ComputeDegreeMatrix(pop, idx);
        auto count = 0L; // number of assigned solutions
        std::vector<std::vector<size_t>> fronts;
        std::vector<size_t> tmp(n);
        std::iota(tmp.begin(), tmp.end(), 0UL);

        std::vector<size_t> remaining;
        while (count < n) {
            std::vector<size_t> front;
            for (auto i : tmp) {
                if (std::all_of(tmp.begin(), tmp.end(), [&](auto j) { return d(j, i) < m; })) {
                    front.push_back(i);
                } else {
                    remaining.push_back(i);
                }
            }
            tmp.swap(remaining);
            remaining.clear();
            count += static_cast<int64_t>(front.size());
            fronts.push_back(front);
        }
        return fronts;
    }
} // namespace Operon
