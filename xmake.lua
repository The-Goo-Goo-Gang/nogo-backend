add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

add_requires("asio", "nlohmann_json")
add_requires("range-v3")
set_languages("cxxlatest")
set_optimize("aggressive")

target("nogo")
    set_kind("binary")
    add_packages("asio", "nlohmann_json")
    add_packages("range-v3")
    add_files("nogo.cpp")
