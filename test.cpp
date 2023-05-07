#include <iostream>

#include <gtest/gtest.h>

#include "test.h"

string host, port;
vector<string> timelist;
string get_time()
{
    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    string temp_time = to_string(ms.count());
    timelist.push_back(temp_time);
    return '"' + temp_time + '"';
}
TEST(nogo, server)
{
    array<string, 7> sendmsg = {
        "{\"op\":200000,\"data1\":\"player1\",\"data2\":\"b\"}\n",
        "{\"op\":200000,\"data1\":\"player2\",\"data2\":\"w\"}\n",
        "{\"op\":200002,\"data1\":\"A1\",\"data2\":" + get_time() + "}\n",
        "{\"op\":200002,\"data1\":\"A2\",\"data2\":" + get_time() + "}\n",
        "{\"op\":200002,\"data1\":\"B2\",\"data2\":" + get_time() + "}\n",
        "{\"op\":200002,\"data1\":\"B2\",\"data2\":" + get_time() + "}\n",
        "{\"op\":200007,\"data1\":\"\",\"data2\":\"\"}\n"
    };
    array<string, 3> recvmsg = {
        "{\"data1\":\"A1\",\"data2\":" + timelist[0] + ",\"op\":200002}\n",
        "{\"data1\":\"B2\",\"data2\":" + timelist[2] + ",\"op\":200002}\n",
        "{\"data1\":\"\",\"data2\":\"\",\"op\":200005}\n"
    };
    vector<string> shouldrecvmsg;
    char path[260];
    getcwd(path, sizeof(path));
    string server_cmd = "cmd /c start " + (string)path + "\\nogo-server " + port;
    system(server_cmd.c_str());
    Sleep(3000);
    all_connected = true;
    cout << path << endl;
    asio::io_context io_context1;
    tcp::resolver resolver(io_context1);
    auto endpoints1 = resolver.resolve(host, port);
    chat_client c1(io_context1, endpoints1);
    std::thread t1([&io_context1]() { io_context1.run(); });
    asio::io_context io_context2;
    tcp::resolver resolver2(io_context2);
    auto endpoints2 = resolver2.resolve(host, port);
    chat_client c2(io_context2, endpoints2);
    std::thread t2([&io_context2]() { io_context2.run(); });
    c1.write(sendmsg[0]);
    ASSERT_TRUE(all_connected) << "Not connected after p1 send" << sendmsg[0];
    Sleep(1000);
    c2.write(sendmsg[1]);
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[1];
    c1.write(sendmsg[2]);
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[2];
    Sleep(1000);
    if (c2.read_msgs_.size() == 0)
        EXPECT_TRUE(c2.read_msgs_.size()) << "p2 recv nothing after p1 send" << sendmsg[2];
    else
        EXPECT_EQ(c2.read_msgs_.back(), recvmsg[0]) << "p2 last recv is wrong after p1 send" << sendmsg[2];
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[3];
    Sleep(1000);
    c1.write(sendmsg[4]);
    ASSERT_TRUE(all_connected) << "Not connected after p1 send" << sendmsg[4];
    Sleep(1000);
    if (c2.read_msgs_.size() == 0)
        EXPECT_TRUE(c2.read_msgs_.size()) << "p2 recv nothing after p1 send" << sendmsg[4];
    else
        EXPECT_EQ(c2.read_msgs_.back(), recvmsg[1]) << "p2 last recv is wrong after p1 send" << sendmsg[4];
    c2.write(sendmsg[5]);
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[5];
    Sleep(5000);
    if (c2.read_msgs_.size() == 0)
        EXPECT_TRUE(c1.read_msgs_.size()) << "p2 recv nothing after p2 send" << sendmsg[5];
    else
        EXPECT_EQ(c1.read_msgs_.back(), recvmsg[2]) << "p2 last recv is wrong after p2 send" << sendmsg[5];
    c2.write(sendmsg[6]);
    c2.close();
    c1.write(sendmsg[6]);
    c1.close();
}
int main(int argc, char* argv[])
{
    if (argc < 3) {
        cout << "Usage: " << argv[0] << " <host> <port>\n";
        return 1;
    }
    host = argv[1];
    port = argv[2];
    testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}