#include "alphazero.hpp"
int main()
{
    // 检查model/using.caffemodel文件是否存在
    std::string save_file = "model/using.caffemodel";
    std::ifstream f("model/using.caffemodel");
    if (!f.good()) {
        save_file = "";
    }
    AlphaZero az("model/solver.prototxt", save_file, 10, 20, 2000, 110, 0.1, 2000, 0.25, 0.05);
    az.train_system(100000000, 200);
    return 0;
}