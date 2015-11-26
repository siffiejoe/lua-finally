#!/usr/bin/lua
if arg[ 1 ] ~= "L" then
  package.path = ""
end

local finally = require( "finally" )


local function create_a( raise )
  if raise then error( "error in create a" ) end
  local name
  local o = {
    close = function()
      print( "-", "close  a", name )
    end
  }
  name = tostring( o )
  print( "+", "create a", name )
  return o
end

local function create_b( raise )
  if raise then error( "error in create b" ) end
  local name
  local o = {
    clear = function()
      print( "-", "clear  b", name )
    end
  }
  name = tostring( o )
  print( "+", "create b", name )
  return o
end

local function create_c( raise )
  if raise then error( "error in create c" ) end
  local name
  local o = {
    destroy = function()
      print( "-", "destroy c", name )
    end
  }
  name = tostring( o )
  print( "+", "create c", name )
  return o
end


local function wastememory( n )
  local a, b, c, d, e, f, g, h, i, j, k, l, m
  if n <= 0 then
    return 0
  else
    return 1+wastememory( n-1 )
  end
end


local function main1( r1, r2, r3, r4, stack, calls, dbg )
  print( r1, r2, r3, r4, stack, calls, dbg )
  local a, b, c
  return finally( function()
    a = create_a( r1 )
    b = create_b( r2 )
    c = create_c( r3 )
    print( "ok" )
    return 1, 2, 3
  end, function( ... )
    print( "error?", ... )
    wastememory( 5 )
    if c then c:destroy() end
    if r4 then error( "error in finally cleanup function" ) end
    if b then b:clear() end
    if a then a:close() end
  end, stack, calls, dbg )
end


if _VERSION == "Lua 5.1" then
  local xpcall, unpack, select = xpcall, unpack, select
  function _G.xpcall( f, msgh, ... )
    local args, n = { ... }, select( '#', ... )
    return xpcall( function() return f( unpack( args, 1, n ) ) end, msgh )
  end
end


local x = ("="):rep( 70 )
local function ___() print( x ) end
local tb = debug.traceback
print( xpcall( main1, tb, false, false, false, false, 80, 6, true ) )
___()
print( xpcall( main1, tb, false, false, false, false, nil, nil, true ) )
___()
print( xpcall( main1, tb, false, false, true, false, nil, nil, true ) )
___()
print( xpcall( main1, tb, false, true, false, false, nil, nil, true ) )
___()
print( xpcall( main1, tb, true, false, false, false, nil, nil, true ) )
___()
print( xpcall( main1, tb, false, false, false, true ) )
___()
print( xpcall( main1, tb, false, false, true, true ) )
___()
print( xpcall( main1, tb, false, false, false, false, 30, 6, true ) )
___()
print( xpcall( main1, tb, false, false, false, false, 80, 3, true ) )
___()
print( xpcall( main1, tb, false, false, false, false, 1000001, 6, true ) )

