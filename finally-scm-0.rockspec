package="finally"
version="scm-0"

source = {
  url = "git://github.com/siffiejoe/lua-finally.git",
}

description = {
  summary = "Provides a 'finally' function.",
  detailed = [[
    'finally' in other languages is used to ensure execution
    of a certain piece of code even if an error/exception is
    thrown (usually for deterministic cleanup of resources).
    This module provides a function that emulates the
    'finally' language feature those other languages in Lua.
  ]],
  homepage = "https://github.com/siffiejoe/lua-finally/",
  license = "MIT",
}

dependencies = {
  "lua >= 5.1, < 5.4"
}

build = {
  type = "builtin",
  modules = {
    finally = "finally.c",
  }
}

