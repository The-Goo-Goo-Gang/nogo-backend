#pragma once
#ifndef _EXPORT
#define _EXPORT
#endif

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

#include "rule.hpp"

namespace chrono = std::chrono;
using namespace std::chrono_literals;

std::mt19937 rng(std::random_device {}());
std::uniform_real_distribution<double> dist(0, 1);
// static -> CE

// struct to represent a node in the Monte Carlo Tree
struct MCTSNode : std::enable_shared_from_this<MCTSNode> {
    using MCTSNode_ptr = std::shared_ptr<MCTSNode>;

    State state;
    std::vector<Position> available_actions;
    std::weak_ptr<MCTSNode> parent;
    std::vector<MCTSNode_ptr> children;
    int visit { 0 };
    double quality { 0 };

    int reward { 0 };

    MCTSNode(const State& state, std::weak_ptr<MCTSNode> parent = {})
        : state(state)
        , parent(parent)
    {
        available_actions = state.available_actions();
        reward = default_policy2();
    }

    auto add_child(const State& state)
    {
        auto child = std::make_shared<MCTSNode>(state, weak_from_this());
        children.push_back(child);
        return child;
    }

    auto best_child(double C)
    {
        auto ucb1 = [&](MCTSNode_ptr child) {
            return child->quality / child->visit + 2 * C * sqrt(log(2 * visit) / child->visit);
        };
        return *ranges::max_element(children, std::less {}, ucb1);
    }

    // select the node to expand
    auto tree_policy(double C)
    {
        auto node { shared_from_this() };

        if (!node->available_actions.size()) {
            return node;
        }

        if (node->children.size() < node->available_actions.size()) {
            auto action { node->available_actions[node->children.size()] };
            return node->add_child(node->state.next_state(action));
        }

        return node->best_child(C)->tree_policy(C);
    }

    double default_policy2()
    {
        int n3 = available_actions.size();
        state.role = -state.role;
        int n4 = state.available_actions().size();
        state.role = -state.role;
        return n4 - n3;
    }

    // backpropagate the result of the simulation
    void backup()
    {
        auto weak_node { weak_from_this() };
        int temp_reward { reward };
        while (auto node { weak_node.lock() }) {
            node->visit++;
            node->quality += temp_reward;
            weak_node = node->parent;
            temp_reward = -temp_reward;
        }
    }
};

_EXPORT Position random_bot_player(const State& state)
{
    auto actions = state.available_actions();
    return actions[rand() % actions.size()];
}

_EXPORT constexpr auto mcts_bot_player_generator(double C)
{
    return [=](const State& state) {
        auto start = chrono::high_resolution_clock::now();
        auto root = std::make_shared<MCTSNode>(state);
        while (chrono::high_resolution_clock::now() - start < 1500ms) {
            auto expand_node = root->tree_policy(C);
            expand_node->backup();
        }
        if (!root->children.size()) {
            return Position {};
        }
        return root->best_child(0)->state.last_move;
    };
}

_EXPORT constexpr auto mcts_bot_player = mcts_bot_player_generator(0.1);
