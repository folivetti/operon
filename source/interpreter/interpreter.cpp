// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2022 Heal Research

#include <taskflow/taskflow.hpp>
#include "operon/interpreter/interpreter.hpp"

namespace Operon {
    auto Evaluate(Interpreter const& interpreter, std::vector<Operon::Tree> const& trees, Dataset const& dataset, Range range, Operon::Span<Operon::Scalar> result, size_t nthreads) noexcept -> void {
        tf::Executor ex(nthreads);
        tf::Taskflow tf;
        auto const s = range.Size();
        auto const n = trees.size();
        tf.for_each_index(decltype(n){0}, n, decltype(n){1}, [&](size_t i) {
            interpreter.Evaluate<Operon::Scalar>(trees[i], dataset, range, result.subspan(i * s, s));
        });
        ex.run(tf).wait();
    }
} // namespace Operon