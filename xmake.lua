add_rules("mode.debug", "mode.release")

add_requires("asio", "nlohmann_json","spdlog","gtest","boost","gflags","leveldb","hdf5","snappy","glog","protobuf-c","openblas")
add_requires("range-v3", "fmt")
set_languages("cxxlatest")
set_optimize("aggressive")
-- set_optimize("fastest")
-- set_warnings("more", "error")

target("nogo")
    set_kind("binary")
    add_packages("asio", "nlohmann_json","spdlog","boost","gflags","leveldb","hdf5","snappy","glog","protobuf-c","openblas")
    add_packages("range-v3")
    add_files("nogo.cpp")
    if is_plat("windows") or is_plat("mingw") then
        add_files("res.rc")
    end
    set_basename("nogo-server")

target("test")
    set_kind("binary")
    add_packages("asio","spdlog","gtest")
    add_packages("range-v3", "fmt")
    add_files("test/test.cpp")
    set_basename("nogo-test")

target("train") 
    set_kind("binary")
    add_packages("asio","spdlog", "nlohmann_json","boost","gflags","leveldb","hdf5","snappy","glog","protobuf-c","openblas")
    add_packages("range-v3", "fmt")
    add_includedirs("include")
    add_files("alphazero/train.cpp")
    -- add_links("lib/*") 
    set_basename("nogo-train")