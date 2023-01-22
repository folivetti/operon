// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2023 Heal Research

#ifndef OPERON_AUTODIFF_REVERSE_DERIVATIVES_HPP
#define OPERON_AUTODIFF_REVERSE_DERIVATIVES_HPP

#include "operon/core/subtree.hpp"

namespace Operon::Autodiff::Reverse {
inline auto Enumerate(auto const& nodes, auto i)
{
    return Subtree<Node const> { nodes, i }.EnumerateIndices();
}

inline auto Indices(auto const& nodes, auto i)
{
    return Subtree<Node const> { nodes, i }.Indices();
}

// derivatives
template <Operon::NodeType N = Operon::NodeType::Add>
struct Derivative {
    inline auto operator()(auto const& nodes, auto const& /*unused*/, auto& rnodes, auto i)
    {
        for (auto [k, j] : Enumerate(nodes, i)) {
            rnodes[i].D[k] = rnodes[j].P;
        }
    }
};

template <>
struct Derivative<Operon::NodeType::Sub> {
    inline auto operator()(auto const& nodes, auto const& /*unused*/, auto& rnodes, auto i)
    {
        for (auto [k, j] : Enumerate(nodes, i)) {
            auto s = k == 0 ? +1 : -1;
            rnodes[i].D[k] = rnodes[j].P * s;
        }
    }
};

template <>
struct Derivative<Operon::NodeType::Mul> {
    inline auto operator()(auto const& nodes, auto const& values, auto& rnodes, auto i)
    {
        auto const& n = nodes[i];
        if (n.Arity == 2) {
            auto j = i-1;
            auto k = j - (nodes[j].Length + 1);
            rnodes[i].D[0] = rnodes[j].P * values.col(k);
            rnodes[i].D[1] = rnodes[k].P * values.col(j);
        } else {
            for (auto [x, j] : Enumerate(nodes, i)) {
                auto& d = rnodes[i].D[x];
                d = rnodes[j].P;
                for (auto k : Indices(nodes, i)) {
                    if (j == k) { continue; }
                    d *= values.col(k);
                }
            }
        }
    }
};

template <>
struct Derivative<Operon::NodeType::Div> {
    inline auto operator()(auto const& nodes, auto const& values, auto& rnodes, auto i)
    {
        auto const& n = nodes[i];

        if (n.Arity > 2) {
            throw std::runtime_error("derivative of division with more than 2 children is not supported");
        }

        auto& d = rnodes[i].D;

        if (n.Arity == 1) {
            auto j = i - 1;
            d[0] = -rnodes[j].P / values.col(j).square();
        } else if (n.Arity == 2) {
            auto j = i - 1;
            auto k = j - (nodes[j].Length + 1);
            d[0] = rnodes[j].P / values.col(k);
            d[1] = -rnodes[k].P * values.col(j) / values.col(k).square();
        } else {
            // TODO
            // f = g * (h * i * ...)
            // u = (h * i * ...)
            // f' = gu' + ug' / u^2
        }
    }
};

// binary symbols
template <>
struct Derivative<Operon::NodeType::Aq> {
    inline auto operator()(auto const& nodes, auto const& values, auto& rnodes, auto i)
    {
        auto j = i - 1;
        auto k = j - (nodes[j].Length + 1);
        rnodes[i].D[0] = rnodes[j].P * values.col(i) / values.col(j);
        // rnodes[i].D[1] = -rnodes[k].P * values[k] * values[j] * (values[i] / values[j]).pow(3);
        rnodes[i].D[1] = -rnodes[k].P * values.col(k) * values.col(i).pow(3) / values.col(j).square();
    }
};

template <>
struct Derivative<Operon::NodeType::Pow> {
    inline auto operator()(auto const& nodes, auto const& values, auto& rnodes, auto i)
    {
        auto j = i - 1;
        auto k = j - (nodes[j].Length + 1);
        rnodes[i].D[0] = rnodes[j].P * values.col(j).pow(values.col(k) - 1) * values.col(k);
        rnodes[i].D[1] = rnodes[k].P * values.col(i) * values.col(j).log();
    }
};

// unary symbols
template <>
struct Derivative<Operon::NodeType::Exp> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P * values.col(i);
    }
};

template <>
struct Derivative<Operon::NodeType::Log> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P / values.col(i-1);
    }
};

template <>
struct Derivative<Operon::NodeType::Logabs> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P * values.col(i-1).sign() / values.col(i-1).abs();
    }
};

template <>
struct Derivative<Operon::NodeType::Log1p> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P / (values.col(i-1) + 1);
    }
};

template <>
struct Derivative<Operon::NodeType::Sin> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P * values.col(i-1).cos();
    }
};

template <>
struct Derivative<Operon::NodeType::Cos> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = -rnodes[i - 1].P * values.col(i-1).sin();
    }
};

template <>
struct Derivative<Operon::NodeType::Tan> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P * (values.col(i).square() + 1);
    }
};

template <>
struct Derivative<Operon::NodeType::Tanh> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P * (1 - values.col(i).square());
    }
};

template <>
struct Derivative<Operon::NodeType::Asin> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P / (1 - values.col(i-1).square()).sqrt();
    }
};

template <>
struct Derivative<Operon::NodeType::Acos> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = -rnodes[i - 1].P / (1 - values.col(i-1).square()).sqrt();
    }
};

template <>
struct Derivative<Operon::NodeType::Atan> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P / (1 + values.col(i-1).square());
    }
};

template <>
struct Derivative<Operon::NodeType::Sqrt> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P / (2 * values.col(i));
    }
};

template <>
struct Derivative<Operon::NodeType::Sqrtabs> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P * values.col(i-1).sign() / (2 * values.col(i));
    }
};

template <>
struct Derivative<Operon::NodeType::Cbrt> {
    inline auto operator()(auto const& /*nodes*/, auto const& values, auto& rnodes, auto i)
    {
        rnodes[i].D[0] = rnodes[i - 1].P / (3 * values.col(i).square());
    }
};
} // namespace Operon::Autodiff::Reverse

#endif
