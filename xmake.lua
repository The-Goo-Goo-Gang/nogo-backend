add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
set_warnings("everything")

set_languages("cxxlatest")
set_optimize("aggressive")

target("nogo")
    set_kind("binary")
    add_files("*.ixx", "nogo.cpp")

target("bot")
    set_kind("binary")
    add_files("bot.cpp")