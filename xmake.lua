add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

add_requires("asio", "nlohmann_json")
set_languages("cxxlatest")
set_optimize("aggressive")

target("network")
    set_kind("static")
    add_packages("asio", "nlohmann_json")
    add_files("network/*.ixx")

target("nogo")
    set_kind("binary")
    add_deps("network")
    add_files("Rule.ixx", "Contest.ixx", "nogo.cpp")

target("bot")
    set_kind("binary")
    add_deps("network")
    add_files("Rule.ixx", "Contest.ixx", "Bot.ixx", "Bot.cpp")