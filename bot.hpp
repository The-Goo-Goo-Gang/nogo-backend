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
#include <optional>

#include "rule.hpp"

namespace chrono = std::chrono;
using namespace std::chrono_literals;

std::mt19937 rng(std::random_device {}());
std::uniform_real_distribution<double> dist(0, 1);
// static -> CE

// struct to represent a node in the Monte Carlo Tree
struct MCTSNode : std::enable_shared_from_this<MCTSNode> {
    using MCTSNode_ptr = std::shared_ptr<MCTSNode>;

    State state {};
    std::vector<Position> available_actions {};
    MCTSNode_ptr parent;
    std::vector<MCTSNode_ptr> children {};
    int visit { 0 };
    double quality { 0 };

    MCTSNode(const State& state, MCTSNode_ptr parent = nullptr)
        : state(state)
        , parent(parent)
    {
    }

    auto add_child(const State& state)
    {
        auto child = std::make_shared<MCTSNode>(state, shared_from_this());
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
            node->available_actions = node->state.available_actions();
        }

        if (!node->available_actions.size()) {
            return node;
        }

        auto state { node->state };
        if (node->children.size() < node->state.available_actions().size()) {
            auto actions { state.available_actions() };
            auto action { actions[node->children.size()] };
            return node->add_child(state.next_state(action));
        }

        return node->best_child(C)->tree_policy(C);
    }

    // simulate the game from the expanded node
    double default_policy()
    {
        auto node { shared_from_this() };

        State state = node->state;
        while (!state.is_over()) {
            auto actions = state.available_actions();
            int index = (int)actions.size() * dist(rng);
            state = state.next_state(actions[index]);
        }
        return state.is_over() == -node->state.role;
    }

    double default_policy2()
    {
        auto node { shared_from_this() };

        auto state { node->state };
        int n3 = state.available_actions().size();
        state.role = -state.role;
        int n4 = state.available_actions().size();
        state.role = -state.role;
        return n4 - n3;
    }

    // backpropagate the result of the simulation
    void backup(double reward)
    {
        auto node { shared_from_this() };

        while (node) {
            node->visit++;
            node->quality += reward;
            node = node->parent;
            reward = -reward;
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
    return [=](const State& state) -> std::optional<Position> {
        auto start = chrono::high_resolution_clock::now();
        auto root = std::make_shared<MCTSNode>(state);
        while (chrono::high_resolution_clock::now() - start < 1500ms) {
            auto expand_node = root->tree_policy(C);
            double reward = expand_node->default_policy2();
            expand_node->backup(reward);
        }
        if (!root->children.size()) {
            return std::nullopt;
        }
        return root->best_child(C)->state.last_move;
    };
}

_EXPORT constexpr auto mcts_bot_player = mcts_bot_player_generator(0.1);
