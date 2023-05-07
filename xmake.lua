add_rules("mode.debug", "mode.release")

add_requires("asio", "nlohmann_json","spdlog","gtest")
add_requires("range-v3")
set_languages("cxxlatest")
-- set_optimize("aggressive")
set_optimize("fastest")
-- set_warnings("more", "error")

target("nogo")
    set_kind("binary")
    add_packages("asio", "nlohmann_json","spdlog")
    add_packages("range-v3")
    add_files("nogo.cpp")
    if is_plat("windows") then
        add_files("res.rc")
    end
    set_basename("nogo-server")

target("test")
    set_kind("binary")
    add_packages("asio","spdlog","gtest")
    add_packages("range-v3")
    add_files("test.cpp")
    set_basename("nogo-test")
