#ifndef INITIALIZATION_HPP
#define INITIALIZATION_HPP

#include <algorithm>
#include <execution>

#include "operator.hpp"
#include "grammar.hpp"
#include "dataset.hpp"

namespace Operon 
{
    using namespace std;

    Node SampleProportional(RandomDevice& random, const vector<pair<NodeType, double>>& partials) 
    {
        uniform_real_distribution<double> uniformReal(0, partials.back().second - numeric_limits<double>::epsilon());
        auto r    = uniformReal(random);
        auto it   = find_if(partials.begin(), partials.end(), [=](auto& p) { return p.second > r; });
        auto node = Node(it->first);
        return node;
    }

    void Grow(RandomDevice& random, const Grammar& grammar, const vector<Variable>& variables, vector<Node>& nodes, const vector<pair<NodeType, double>>& partials, size_t maxLength, size_t maxDepth) 
    {
        if (maxDepth == 0)
        {
            // only allowed to grow leaf nodes
            auto pc   = grammar.GetFrequency(NodeType::Constant);
            auto pv   = grammar.GetFrequency(NodeType::Variable);
            uniform_real_distribution<double> uniformReal(0, pc + pv);
            auto node = uniformReal(random) < pc ? Node(NodeType::Constant) : Node(NodeType::Variable);
            nodes.push_back(node);
        }
        else 
        {
            auto node = SampleProportional(random, partials);
            nodes.push_back(node);
            for (size_t i = 0; i < node.Arity; ++i)
            {
                Grow(random, grammar, variables, nodes, partials, maxLength, maxDepth - 1);
            }
        }
    }


    class GrowTreeCreator : public CreatorBase 
    {
        public:
            GrowTreeCreator(size_t depth, size_t length) : maxDepth(depth), maxLength(length) { }

            Tree operator()(RandomDevice& random, const Grammar& grammar, const vector<Variable>& variables) const
            {
                auto allowed = grammar.AllowedSymbols();
                vector<pair<NodeType, double>> partials(allowed.size());
                std::inclusive_scan(std::execution::seq, allowed.begin(), allowed.end(), partials.begin(), [](const auto& lhs, const auto& rhs) { return std::make_pair(rhs.first, lhs.second + rhs.second); });
                vector<Node> nodes;
                auto root = SampleProportional(random, partials);
                nodes.push_back(root);

                for (int i = 0; i < root.Arity; ++i)
                {
                    Grow(random, grammar, variables, nodes, partials, maxLength, maxDepth - 1);
                }
                uniform_int_distribution<size_t> uniformInt(0, variables.size() - 1); 
                for(auto& node : nodes)
                {
                    if (node.IsVariable())
                    {
                        node.HashValue = node.CalculatedHashValue = variables[uniformInt(random)].Hash;
                    }
                }
                reverse(nodes.begin(), nodes.end());
                auto tree = Tree(nodes);
                return tree.UpdateNodes();
            }

        private:
            size_t maxDepth;
            size_t maxLength;
    };
}
#endif
