#        Finally -- Deterministic Cleanup of Resources in Lua        #

##                           Introduction                           ##

Lua's garbage collector and `__gc` metamethods can handle arbitrary
resources in a reliable, yet undeterministic way. Some resources
however need to be reclaimed as soon as possible, even if an error is
raised while using such a resource. Other languages provide dedicated
language features for this situation (`finally`, `using`, or
scope-based destruction of objects -- `RAII`). On Lua you can use this
**finally** module.


##                          Getting Started                         ##

The interface was proposed in a [lua-l mailing list thread][1]: The
`finally` function takes two Lua functions as arguments, calls the
first, and then the second function even if the first function call
raises an error (the error is passed as an argument to the second
function call in this case):

    local f1, f2
    local same = finally( function()
      f1 = assert( io.open( "filename1.txt", "r" ) )
      f2 = assert( io.open( "filename2.txt", "r" ) )
      return f1:read( "*a" ) == f2:read( "*a" )
    end, function( e )
      if e then print( "there was an error!" ) end
      if f2 then f2:close() end
      if f1 then f1:close() end
    end )

The `finally` function call returns the results of the first function
(or re-raises its error) unless an error happens during execution of
the second function, in which case previous results/errors are lost.
This (and the fact that an interrupted cleanup function could leak
important resources) is the reason why any code that may raise errors
should be avoided in the cleanup function. Unfortunately Lua allocates
some memory implicitly when running Lua code (e.g. for function call
frames or the Lua stack), which can cause memory allocation errors or
errors in unrelated `__gc` metamethods to be raised. The `finally`
function implementation in this module gives you the chance of writing
cleanup code that can never be interrupted by calling the cleanup
function in a coroutine (preallocated before the first function call)
with reserved call frames and Lua stack slots. Unless you allocate new
Lua values or raise errors explicitly in your cleanup function (or
write faulty Lua code), you are fine.

The defaults should be good enough for most cleanup code, but you can
pass the number of reserved stack slots (default 100) as the third and
the number of preallocated stack frames (default 10) as the fourth
argument to `finally`. To ensure that the parameters are high enough
for your cleanup code, you can pass a `true`ish value as the fifth
argument to `finally` during development/testing. This will cause
*any* memory allocation by Lua during the execution of the cleanup
function to raise an error.

And that's all.

  [1]:  http://lua-users.org/lists/lua-l/2015-11/msg00270.html
  [2]:  http://lua-users.org/lists/lua-l/2015-04/msg00423.html


##                          Quirks/Gotchas                          ##

There are many ways to allocate memory in Lua code inadvertently, and
thus to risk memory allocation errors or errors in `__gc` metamethods
while running the cleanup function. What you should definitely avoid
is table literals, writes to non-existing table fields, new strings
(e.g. using string concatenation, by implicit coercions or `tostring`
calls, or some C API functions, e.g. `luaL_error` -- string literals
in Lua code are fine because they are allocated when the chunk is
compiled), new Lua functions, coroutines, or userdata.

This module works for Lua 5.1 (including LuaJIT) up to Lua 5.3, but
the code for Lua 5.1 uses recursive Lua function calls instead of C
function calls to preallocate call frames and stack slots. There is a
separate limit for C function calls that could cause an error later in
the cleanup function, but you should easily be able to rule this out
during testing. You also cannot explicitly set the number of stack
slots to preallocate. For each call frame approximately 15 extra stack
slots are available. However, the number of stack slots or call frames
needed by a JIT-compiled Lua function might differ from the uncompiled
version of the same function, and JIT-compilation itself may happen at
any time and cause memory allocations. Since the LuaJIT code is
written in assembler, it is hard to figure out where exactly memory
might be allocated. So when using LuaJIT you are basically on your
own!


##                              Contact                             ##

Philipp Janda, siffiejoe(a)gmx.net

Comments and feedback are always welcome.


##                              License                             ##

**finally** is *copyrighted free software* distributed under the MIT
license (the same license as Lua 5.1). The full license text follows:

    finally (c) 2015 Philipp Janda

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to
    the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

