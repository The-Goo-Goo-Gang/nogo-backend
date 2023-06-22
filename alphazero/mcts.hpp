#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

#include "../rule.hpp"

namespace chrono = std::chrono;
using namespace std::chrono_literals;

std::mt19937 rng_az(std::random_device {}());

std::pair<float, std::vector<std::pair<Position, float>>> simple_action_propability(const State& state)
{
    State state_temp = state;
    state_temp.role = -state_temp.role;
    auto available_actions = state.available_actions();
    std::vector<std::pair<Position, float>> action_propability;
    for (auto action : available_actions) {
        action_propability.push_back({ action, 0.5f });
    }
    action_propability.resize(available_actions.size());
    float value = (float)available_actions.size() - (float)state_temp.available_actions().size();
    return std::pair(value, action_propability);
}

class MCTSNode_az : public std::enable_shared_from_this<MCTSNode_az> {
    Position action;
    std::weak_ptr<MCTSNode_az> parent;
    std::vector<std::shared_ptr<MCTSNode_az>> children;
    int visit { 0 };
    float propability { 0 };
    float total_quality { 0 };

    auto getquality() const
    {
        if (visit == 0)
            return 0.0f;
        return total_quality / visit;
    }

public:
    MCTSNode_az(std::weak_ptr<MCTSNode_az> parent = {}, Position action = {}, float propability = 1)
        : parent(parent)
        , action(action)
        , propability(propability)
    {
    }

    auto select(float c_param) const
    {
        auto ucb = [&](std::shared_ptr<MCTSNode_az> child) {
            return child->getquality() + c_param * propability * sqrt(log(1 + 2 * visit) / (1 + child->visit));
        };
        return *ranges::max_element(children, std::less {}, ucb);
    };

    void expand(const std::vector<std::pair<Position, float>>& action_propability)
    {
        for (auto i : action_propability) {
            auto child = std::make_shared<MCTSNode_az>(weak_from_this(), i.first, i.second);
            children.push_back(child);
        }
        return;
    }

    void backup(float reward)
    {
        auto node = shared_from_this();
        while (node) {
            node->visit++;
            node->total_quality += reward;
            reward = -reward;
            node = node->parent.lock();
        }
        return;
    };
    friend class MCTSTree_az;
};
class AlphaZero;
class MCTSTree_az {
    std::shared_ptr<MCTSNode_az> root;
    AlphaZero* az;
    int get_action_propability_type;
    int playout_times;
    int playout_millisecond;
    State state;
    float c_param;
    float noise_param;
    float noise_weight;

    void playout();

    void normalize_propability()
    {
        float sum = 0;
        for (auto child : root->children) {
            sum += child->visit;
        }
        for (auto child : root->children) {
            child->propability = child->visit / (sum + 0.0001f);
        }
        return;
    };

    void add_noise()
    {
        std::vector<float> noises;
        float noise_sum = 0;
        std::gamma_distribution<float> dist_az(noise_param, 1.0f);
        for (auto child : root->children) {
            noises.push_back(dist_az(rng_az));
            noise_sum += noises.back();
        }
        for (auto child : root->children) {
            child->propability = (1 - noise_weight) * child->propability + noise_weight * noises.back() / (noise_sum + 0.0001f);
            noises.pop_back();
        }
        return;
    }

public:
    MCTSTree_az(int get_action_propability_type,
        AlphaZero* az,
        const State& state,
        int times,
        int millisecond,
        float c_param,
        float noise_param,
        float noise_weight)
        : get_action_propability_type(get_action_propability_type)
        , az(az)
        , state(state)
        , playout_times(times)
        , playout_millisecond(millisecond)
        , c_param(c_param)
        , noise_param(noise_param)
        , noise_weight(noise_weight)
        , root(std::make_shared<MCTSNode_az>())
    {
    }

    State get_state() const
    {
        return state;
    }

    void move(Position action)
    {
        state = state.next_state(action);
        auto oldroot = root;
        bool find = false;
        for (auto child : root->children) {
            if (child->action == action) {
                find = true;
                root = child;
                root->parent.reset();
                break;
            }
        }
        if (!find) {
            root = std::make_shared<MCTSNode_az>();
        }
        return;
    }

    auto return_action_propability()
    {
        normalize_propability();
        add_noise();
        std::vector<std::pair<Position, float>> action_propability;
        for (auto child : root->children) {
            action_propability.push_back({ child->action, child->propability });
        }
        float quality = root->total_quality / root->visit;
        return std::pair<float, std::vector<std::pair<Position, float>>>(quality, action_propability);
    }

    auto treestep()
    {
        if (playout_times == 0) {
            auto start = chrono::steady_clock::now();
            while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count() < playout_millisecond) {
                playout();
            }
        } else {
            for (int i = 0; i < playout_times; i++) {
                playout();
            }
        }
        std::cout << root->total_quality << std::endl;
        return root->select(0)->action;
    }
};
