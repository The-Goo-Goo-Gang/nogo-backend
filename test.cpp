#include <iostream>

#include <gtest/gtest.h>

#include "test.h"

string host, port;
TEST(nogo, server)
{
    array<string, 7> sendmsg = {
        "{\"op\":200000,\"data1\":\"player1\",\"data2\":\"b\"}\n",
        "{\"op\":200000,\"data1\":\"player2\",\"data2\":\"w\"}\n",
        "{\"op\":200002,\"data1\":\"A1\",\"data2\":\"1683446065123\"}\n",
        "{\"op\":200002,\"data1\":\"A2\",\"data2\":\"1683446066123\"}\n",
        "{\"op\":200002,\"data1\":\"B2\",\"data2\":\"1683446067123\"}\n",
        "{\"op\":200002,\"data1\":\"B2\",\"data2\":\"1683446068123\"}\n",
        "{\"op\":200007,\"data1\":\"\",\"data2\":\"\"}\n"
    };
    array<string, 3> recvmsg = {
        "{\"data1\":\"A1\",\"data2\":\"1683446065123\",\"op\":200002}\n",
        "{\"data1\":\"B2\",\"data2\":\"1683446067123\",\"op\":200002}\n",
        "{\"data1\":\"\",\"data2\":\"\",\"op\":200005}\n"
    };
    char path[260];
    getcwd(path, sizeof(path));
    string server_cmd = "cmd /c start " + (string)path + "\\nogo-server " + port;
    system(server_cmd.c_str());
    this_thread::sleep_for(chrono::seconds(3));
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
    this_thread::sleep_for(chrono::seconds(1));
    c2.write(sendmsg[1]);
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[1];
    c1.write(sendmsg[2]);
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[2];
    this_thread::sleep_for(chrono::seconds(1));
    if (c2.read_msgs_.size() == 0)
        EXPECT_TRUE(c2.read_msgs_.size()) << "p2 recv nothing after p1 send" << sendmsg[2];
    else
        EXPECT_EQ(c2.read_msgs_.back(), recvmsg[0]) << "p2 last recv is wrong after p1 send" << sendmsg[2];
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[3];
    this_thread::sleep_for(chrono::seconds(1));
    c1.write(sendmsg[4]);
    ASSERT_TRUE(all_connected) << "Not connected after p1 send" << sendmsg[4];
    this_thread::sleep_for(chrono::seconds(1));
    if (c2.read_msgs_.size() == 0)
        EXPECT_TRUE(c2.read_msgs_.size()) << "p2 recv nothing after p1 send" << sendmsg[4];
    else
        EXPECT_EQ(c2.read_msgs_.back(), recvmsg[1]) << "p2 last recv is wrong after p1 send" << sendmsg[4];
    c2.write(sendmsg[5]);
    ASSERT_TRUE(all_connected) << "Not connected after p2 send" << sendmsg[5];
    this_thread::sleep_for(chrono::seconds(5));
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