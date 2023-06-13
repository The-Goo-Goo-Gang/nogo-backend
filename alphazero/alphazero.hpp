#define NOMINMAX
#ifndef CPU_ONLY
#define CPU_ONLY
#endif
#include <caffe/caffe.hpp>

#include <chrono>
#include <deque>
#include <random>
#include <vector>

#include "../log.hpp"
#include "mcts.hpp"

class AlphaZero {
    boost::shared_ptr<caffe::Solver<float>> solver;
    boost::shared_ptr<caffe::Net<float>> main_net;
    boost::shared_ptr<caffe::Net<float>> save_net;

    int batch_size = 1;
    int test_games = 0;
    int test_playouts = 0;
    int num_win = 0;
    int num_lose = 0;

    float loss_sum = 0;

    std::deque<std::tuple<State, float, std::vector<std::pair<Position, float>>>> history_data;
    int max_history = 100;

    float c_param = 0;
    int train_playouts = 0;
    int train_playout_time = 0;
    float noise_param = 0;
    float noise_weight = 0;

public:
    AlphaZero(const std::string& solver_file,
        const std::string& save_file,
        const int batch_size,
        const int test_games,
        const int test_playouts,
        const int max_history,
        const float c_param,
        const int train_playouts,
        const float noise_param,
        const float noise_weight);
    AlphaZero(const std::string& net_file,
        const std::string& save_file,
        const int playout_time_,
        const float c_param_);
    void ReshapeNet(const boost::shared_ptr<caffe::Net<float>> net, const int new_batch_size);
    auto alphazero_action_propability_mainnet(const State& state);
    auto alphazero_action_propability_savenet(const State& state);
    void train(const int game_num, bool finish_first_stage);
    void compare(bool finish_first_stage);
    void save();
    auto run(const State& state, int test_playout_time);
    void train_system(const int games, const int test_frequency, bool finish_first_stage);
};
// This is used for training
AlphaZero::AlphaZero(const std::string& solver_file,
    const std::string& save_file,
    const int batch_size,
    const int test_games,
    const int test_playouts,
    const int max_history,
    const float c_param,
    const int train_playouts,
    const float noise_param,
    const float noise_weight)
    : batch_size(batch_size)
    , test_games(test_games)
    , test_playouts(test_playouts)
    , max_history(max_history)
    , c_param(c_param)
    , train_playouts(train_playouts)
    , noise_param(noise_param)
    , noise_weight(noise_weight)
{
    caffe::Caffe::set_mode(caffe::Caffe::CPU);
    caffe::SolverParameter solver_param;
    caffe::ReadProtoFromTextFileOrDie(solver_file, &solver_param);
    solver.reset(caffe::SolverRegistry<float>::CreateSolver(solver_param));
    main_net = solver->net();
    save_net.reset(new caffe::Net<float>(solver->param().net(), caffe::Phase::TEST));
    if (!save_file.empty()) {
        main_net->CopyTrainedLayersFromBinaryProto(save_file);
    }
    train_playout_time = 0;
    alphazero_logger->info("Init AlphaZero: iteration{}", solver->iter());
}

// To caculate faster, we use a simple version for testing
AlphaZero::AlphaZero(const std::string& net_file,
    const std::string& save_file,
    const int playout_time_,
    const float c_param_)
    : c_param(c_param_)
    , train_playout_time(playout_time_)
{
    save_net.reset(new caffe::Net<float>(net_file, caffe::Phase::TEST));
    save_net->CopyTrainedLayersFromBinaryProto(save_file);
    ReshapeNet(save_net, 1);
    alphazero_logger->info("Init AlphaZero for test: iteration{}", solver->iter());
}

void AlphaZero::ReshapeNet(const boost::shared_ptr<caffe::Net<float>> net, const int new_batch_size)
{
    if (!net) {
        return;
    }
    std::vector<int> shape;
    boost::shared_ptr<caffe::Blob<float>> blob = net->blob_by_name("input_data");
    if (blob) {
        shape = blob->shape();
        shape[0] = new_batch_size;
        blob->Reshape(shape);
    }

    blob = net->blob_by_name("model_value");
    if (blob) {
        shape = blob->shape();
        shape[0] = new_batch_size;
        blob->Reshape(shape);
    }

    blob = net->blob_by_name("model_probas");
    if (blob) {
        shape = blob->shape();
        shape[0] = new_batch_size;
        blob->Reshape(shape);
    }

    boost::shared_ptr<caffe::Layer<float>> layer = net->layer_by_name("cross_entropy_scale");
    if (layer) {
        layer->blobs()[0]->mutable_cpu_data()[0] = 1.0f / new_batch_size;
    }
    net->Reshape();
}
auto AlphaZero::alphazero_action_propability_mainnet(const State& state)
{
    std::vector<float> input = state.to_net();
    boost::shared_ptr<caffe::Blob<float>> blob_state = main_net->blob_by_name("input_data");
    boost::shared_ptr<caffe::Blob<float>> blob_value = main_net->blob_by_name("output_value");
    boost::shared_ptr<caffe::Blob<float>> blob_propablity = main_net->blob_by_name("output_probas");
    caffe::caffe_copy(input.size(), input.data(), blob_state->mutable_cpu_data());
    main_net->Forward();
    float value = blob_value->cpu_data()[0];
    std::vector<std::pair<Position, float>> action_propability;
    for (int i = 0; i < 81; i++) {
        Position p { i % 9, i / 9 };
        action_propability.push_back(std::pair { p, blob_propablity->cpu_data()[i] });
        if ((*state.board)[p]) {
            action_propability.pop_back();
        }
    }
    return std::pair<float, std::vector<std::pair<Position, float>>> { value, action_propability };
}
auto AlphaZero::alphazero_action_propability_savenet(const State& state)
{
    std::vector<float> input = state.to_net();
    boost::shared_ptr<caffe::Blob<float>> blob_state = save_net->blob_by_name("input_data");
    boost::shared_ptr<caffe::Blob<float>> blob_value = save_net->blob_by_name("output_value");
    boost::shared_ptr<caffe::Blob<float>> blob_propablity = save_net->blob_by_name("output_probas");
    caffe::caffe_copy(input.size(), input.data(), blob_state->mutable_cpu_data());
    save_net->Forward();
    float value = blob_value->cpu_data()[0];
    std::vector<std::pair<Position, float>> action_propability;
    for (int i = 0; i < 81; i++) {
        Position p { i % 9, i / 9 };
        action_propability.push_back(std::pair { p, blob_propablity->cpu_data()[i] });
        if ((*state.board)[p]) {
            action_propability.pop_back();
        }
    }
    return std::pair<float, std::vector<std::pair<Position, float>>> { value, action_propability };
}
void AlphaZero::train(const int game_num, bool finish_first_stage)
{
    if (history_data.size() < 2 * batch_size) {
        std::uniform_int_distribution<int> dist_az(0, 9);
        State state;
        Position position { dist_az(rng_az), dist_az(rng_az) };
        State trainstate = state.next_state(position);
        /*
        auto* tree_policy(const State&) > = alphazero_action_propability_mainnet;
        if (!finish_first_stage)
            tree_policy = simple_action_propability;
            */
        MCTSTree_az tree(finish_first_stage ? 1 : 0, this, trainstate, train_playouts, 0, c_param, noise_param, noise_weight);
        while (1) {
            Position p = tree.treestep();
            std::pair<float, std::vector<std::pair<Position, float>>> action_propability = tree.return_action_propability();
            tree.move(p);
            if (!finish_first_stage)
                action_propability.first = tanh(action_propability.first);
            std::tuple<State, float, std::vector<std::pair<Position, float>>> data { trainstate, action_propability.first, action_propability.second };
            history_data.push_back(data);
            if (tree.get_state().is_over()) {
                break;
            }
            trainstate = tree.get_state();
        }
        while (history_data.size() > max_history) {
            history_data.pop_front();
        }
        alphazero_logger->info("Finish loading game data");
    }
    auto data = history_data.front();
    history_data.pop_front();
    boost::shared_ptr<caffe::Blob<float>> blob_state = main_net->blob_by_name("input_data");
    boost::shared_ptr<caffe::Blob<float>> blob_value = main_net->blob_by_name("model_value");
    boost::shared_ptr<caffe::Blob<float>> blob_propablity = main_net->blob_by_name("model_probas");
    boost::shared_ptr<caffe::Blob<float>> blob_loss_value = main_net->blob_by_name("value_loss");
    boost::shared_ptr<caffe::Blob<float>> blob_loss_probas = main_net->blob_by_name("probas_loss");
    auto input = std::get<0>(data).to_net();
    caffe::caffe_copy(input.size(), input.data(), blob_state->mutable_cpu_data());
    *(blob_value->mutable_cpu_data()) = std::get<1>(data);
    std::vector<float> input_propability(81, -1.0f);
    for (auto& p : std::get<2>(data)) {
        input_propability[p.first.y * 9 + p.first.x] = p.second;
    }
    caffe::caffe_copy(input_propability.size(), input_propability.data(), blob_propablity->mutable_cpu_data());
    solver->Step(1);
    loss_sum = blob_loss_value->cpu_data()[0] + blob_loss_probas->cpu_data()[0];
    if (game_num % 10 == 0)
        alphazero_logger->info("Train: Game_num{}, Batchsize{}, loss_sum{}", game_num, batch_size, loss_sum);
    return;
}
void AlphaZero::compare(bool finish_first_stage)
{
    State state;
    MCTSTree_az maintree(1, this, state, test_playouts, 0, c_param, noise_param, noise_weight);
    MCTSTree_az savetree(finish_first_stage ? 1 : 0, this,
        state, test_playouts, 0, c_param, noise_param, noise_weight);
    Role main_role;
    if (num_win + num_lose % 2 == 0) {
        main_role = Role::BLACK;
    } else {
        main_role = Role::WHITE;
    }
    while (state.available_actions().size() && !state.is_over()) {
        Position p;
        if (state.role = main_role) {
            p = maintree.treestep();
        } else {
            p = savetree.treestep();
        }
        maintree.move(p);
        savetree.move(p);
        state = maintree.get_state();
    }
    Role winner = state.is_over() ? state.role : -state.role;
    if (winner == main_role) {
        num_win++;
    } else {
        num_lose++;
    }

    return;
}
void AlphaZero::save()
{
    caffe::NetParameter net_param;
    main_net->ToProto(&net_param, false);
    std::string filename = "model/" + std::to_string(std::time(nullptr)) + ".caffemodel";
    caffe::WriteProtoToBinaryFile(net_param, filename);
    save_net.reset(new caffe::Net<float>(solver->param().net(), caffe::Phase::TEST));
    alphazero_logger->info("Save weights to {}", filename);
    solver->Snapshot();
    return;
}
auto AlphaZero::run(const State& state, int test_playout_time)
{
    Position p;
    float winrate;
    auto actions = state.available_actions();
    if (!actions.size()) {
        p = Position { -1, -1 };
        winrate = -1;
    } else {
        MCTSTree_az tree(2, this, state, 0, 1500, c_param, 0, 0);
        p = tree.treestep();
        winrate = tree.return_action_propability().first;
        State next = state.next_state(p);
        if (next.is_over()) {
            logger->error("AlphaZero error, return random available position");
            p = actions[rand() % actions.size()];
        }
    }
    return std::pair<Position, float> { p, winrate };
}
void AlphaZero::train_system(const int games, const int test_frequency, bool finish_first_stage = false)
{
    ReshapeNet(main_net, batch_size);
    for (int i = 0; i < games; i++) {
        train(i, finish_first_stage);
        if (i % test_frequency == 0) {
            ReshapeNet(main_net, 1);
            ReshapeNet(save_net, 1);
            for (int i = 0; i < test_games; i++) {
                compare(finish_first_stage);
            }
            ReshapeNet(main_net, batch_size);
            alphazero_logger->info("Test {} games, win {}, lose {}", test_games, num_win, num_lose);
            if (num_win > num_lose || !finish_first_stage) {
                alphazero_logger->info("Save weights");
                save();
            }
            if (num_win > num_lose * 0.5 && !finish_first_stage) {
                alphazero_logger->warn("Finish first stage");
                finish_first_stage = true;
            }
            num_win = 0;
            num_lose = 0;
        }
    }
    return;
}

Position alphazero_bot_player(const State& state)
{
    if (state.board->get_rank() != 9) {
        logger->error("AlphaZero error, board size is not 9, return random available position");
        auto actions = state.available_actions();
        auto p = actions[rand() % actions.size()];
        return p;
    }
    static AlphaZero bot("model/net.prototxt", "model/using.caffemodel", 1500, 0.1);
    return bot.run(state, 1500).first;
}

void MCTSTree_az::playout()
{
    State tempstate = state;
    auto node = root;
    float reward = 0.0f;
    while (node->children.size()) {
        node = node->select(c_param);
        tempstate = tempstate.next_state(node->action);
    }
    if (!tempstate.is_over() && tempstate.available_actions().size()) {
        std::pair<float, std::vector<std::pair<Position, float>>> action_propability;
        if (get_action_propability_type == 0)
            action_propability = simple_action_propability(tempstate);
        else if (get_action_propability_type == 1)
            action_propability = az->alphazero_action_propability_mainnet(tempstate);
        else if (get_action_propability_type == 2)
            action_propability = az->alphazero_action_propability_savenet(tempstate);
        reward = action_propability.first;
        node->expand(action_propability.second);
    } else {
        if (tempstate.is_over())
            reward = 1.0f;
        else
            reward = -1.0f;
    }
    node->backup(-reward);
    return;
};