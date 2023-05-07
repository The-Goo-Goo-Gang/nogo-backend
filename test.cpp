#include <iostream>

#include <gtest/gtest.h>

#include "test.h"

string host, port;
vector<string> timelist;
string get_time()
{
    std::chrono::milliseconds ms = std::chrono::duration_cast< std::chrono::milliseconds >(
        std::chrono::system_clock::now().time_since_epoch()
    );
    string temp_time = to_string(ms.count());
    timelist.push_back(temp_time);
    return '"'+temp_time+'"';
}
TEST(nogo,server){
    char path[260];
    getcwd(path,sizeof(path));
    string server_cmd = "cmd /c start "+(string)path+"\\nogo-server "+port;
    system(server_cmd.c_str());
    Sleep(3000);
    all_connected = true;
    cout << path << endl;
    asio::io_context io_context1;
    tcp::resolver resolver(io_context1);
    auto endpoints1 = resolver.resolve(host, port);
    chat_client c1(io_context1, endpoints1);
    std::thread t1([&io_context1](){ io_context1.run(); });
    asio::io_context io_context2;
    tcp::resolver resolver2(io_context2);
    auto endpoints2 = resolver2.resolve(host, port);
    chat_client c2(io_context2, endpoints2);
    std::thread t2([&io_context2](){ io_context2.run(); });
    c1.write("{\"op\":200000,\"data1\":\"player1\",\"data2\":\"b\"}\n");
    ASSERT_TRUE(all_connected) << "Not connected after {\"op\":200000,\"data1\":\"player1\",\"data2\":\"b\"}";
    Sleep(1000);
    c2.write("{\"op\":200000,\"data1\":\"player2\",\"data2\":\"w\"}\n");
    ASSERT_TRUE(all_connected) << "Not connected after {\"op\":200000,\"data1\":\"player2\",\"data2\":\"w\"}";
    Sleep(1000);
    c1.write("{\"op\":200002,\"data1\":\"A1\",\"data2\":"+get_time()+"}\n");
    ASSERT_TRUE(all_connected) << "Not connected after {\"op\":200002,\"data1\":\"A1\",\"data2\":"+timelist[0]+"}";
    Sleep(1000);
    if(c2.read_msgs_.size() == 0)
      EXPECT_TRUE(c2.read_msgs_.size())<< "player2 received nothing after {\"op\":200002,\"data1\":\"A1\",\"data2\":"+timelist[0]+"}";
    else EXPECT_EQ(c2.read_msgs_.back(),"{\"data1\":\"A1\",\"data2\":"+timelist[0]+",\"op\":200002}\n")
         << "player1 last message is not {\"data1\":\"A1\",\"data2\":"+timelist[0]+",\"op\":200002}"
         << " but " << c2.read_msgs_.back();
    c2.write("{\"op\":200002,\"data1\":\"A2\",\"data2\":"+get_time()+"}\n");
    ASSERT_TRUE(all_connected) << "Not connected after {\"op\":200002,\"data1\":\"A2\",\"data2\":"+timelist[1]+"}";
    Sleep(1000);
    c1.write("{\"op\":200002,\"data1\":\"B2\",\"data2\":"+get_time()+"}\n");
    ASSERT_TRUE(all_connected) << "Not connected after {\"op\":200002,\"data1\":\"B2\",\"data2\":"+timelist[2]+"}";
    Sleep(1000); 
    if(c2.read_msgs_.size() == 0)
      EXPECT_TRUE(c2.read_msgs_.size())<< "player2 received nothing after {\"op\":200002,\"data1\":\"B2\",\"data2\":"+timelist[2]+"}";
    else EXPECT_EQ(c2.read_msgs_.back(),"{\"data1\":\"B2\",\"data2\":"+timelist[2]+",\"op\":200002}\n")
         << "player1 last message is not {\"data1\":\"B2\",\"data2\":"+timelist[2]+",\"op\":200002}"
         << " but " << c2.read_msgs_.back();
    c2.write("{\"op\":200002,\"data1\":\"B1\",\"data2\":"+get_time()+"}\n");
    ASSERT_TRUE(all_connected) << "Not connected after {\"op\":200002,\"data1\":\"B1\",\"data2\":"+timelist[3]+"}";
    Sleep(5000);
    if(c2.read_msgs_.size() == 0)
      EXPECT_TRUE(c1.read_msgs_.size())<< "playerw received nothing after {\"op\":200002,\"data1\":\"B1\",\"data2\":"+timelist[3]+"}";
    else EXPECT_EQ(c1.read_msgs_.back(),"{\"data1\":\"\",\"data2\":\"\",\"op\":200005}\n")
         << "player1 last message is not {\"data1\":\"\",\"data2\":\"\",\"op\":200005}"
         << " but " << c1.read_msgs_.back();
    c2.write("{\"op\":200007,\"data1\":\"\",\"data2\":\"\"}\n");
    c2.close();
    c1.write("{\"op\":200007,\"data1\":\"\",\"data2\":\"\"}\n");
    c1.close();
}
int main(int argc, char* argv[]){
  if(argc<3){
    cout<<"Usage: "<<argv[0]<<" <host> <port>\n";
    return 1;
  }
  host = argv[1];
  port = argv[2];
  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}