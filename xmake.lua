add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
set_warnings("everything")

add_requires("asio")
set_languages("cxxlatest")
set_optimize("aggressive")

target("nogo")
    set_kind("binary")
    add_files("*.ixx", "nogo.cpp")
    add_defines("ASIO_STANDALONE")
    add_packages("asio")

target("bot")
    set_kind("binary")
    add_files("*.ixx", "bot.cpp")
    add_defines("ASIO_STANDALONE")
    add_packages("asio")