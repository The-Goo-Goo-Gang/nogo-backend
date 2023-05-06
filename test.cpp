#include <iostream>

#include <gtest/gtest.h>

#include "contest.hpp"
#include "network.hpp"
#include "log.hpp"
#include "test.h"

#include <windows.h>

string host, port;
TEST(nogo,testall){
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
    c1.write("{\"op\":200000,\"data1\":\"andylizf\",\"data2\":\"b\"}\n");
    Sleep(100);
    c2.write("{\"op\":200000,\"data1\":\"huangchengfly\",\"data2\":\"w\"}\n");
    Sleep(100);
    c1.write("{\"op\":200002,\"data1\":\"A1\",\"data2\":\"b\"}\n");
    Sleep(100);
    c2.write("{\"op\":200002,\"data1\":\"A2\",\"data2\":\"w\"}\n");
    Sleep(100);
    c1.write("{\"op\":200002,\"data1\":\"B2\",\"data2\":\"b\"}\n");
    Sleep(100);
    c2.write("{\"op\":200002,\"data1\":\"B1\",\"data2\":\"w\"}\n");
}
int main(int argc, char* argv[]){
  if(argc<3){
    std::cout<<"Usage: "<<argv[0]<<" <host> <port>\n";
    return 1;
  }
  host = argv[1];
  port = argv[2];
  testing::InitGoogleTest();
  return RUN_ALL_TESTS();
}