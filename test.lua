#!/usr/bin/lua
if arg[ 1 ] == "C" then
  package.path = ""
end

local finally = require( "finally" )


local function create_a( raise )
  if raise then error( "error in create a" ) end
  local o = {
    close = function( self )
      print( "-", "close  a", self )
    end
  }
  print( "+", "create a", o )
  return o
end

local function create_b( raise )
  if raise then error( "error in create b" ) end
  local o = {
    clear = function( self )
      print( "-", "clear  b", self )
    end
  }
  print( "+", "create b", o )
  return o
end

local function create_c( raise )
  if raise then error( "error in create c" ) end
  local o = {
    destroy = function( self )
      print( "-", "destroy c", self )
    end
  }
  print( "+", "create c", o )
  return o
end


local function wastememory( n )
  local a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, t
  if n <= 0 then
    return 0
  else
    return 1+wastememory( n-1 )
  end
end


local function main1( r1, r2, r3, r4, r5, stack, calls, dbg )
  print( r1, r2, r3, r4, r5, stack, calls, dbg )
  local a, b, c
  return finally( function()
    a = create_a( r1 )
    b = create_b( r2 )
    c = create_c( r3 )
    print( "ok" )
    return 1, 2, 3
  end, function()
    if r5 then wastememory( 3 ) end
    if c then c:destroy() end
    if r4 then error( "error in finally cleanup function" ) end
    if b then b:clear() end
    if a then a:close() end
  end, stack, calls, dbg )
end


local x = ("="):rep( 70 )
local function ___() print( x ) end
local tb = debug.traceback
print( xpcall( main1, tb, false, false, false, false, true, 80, 4, true ) )
___()
print( xpcall( main1, tb, false, false, false, false, false ) )
___()
print( xpcall( main1, tb, false, false, true, false, false ) )
___()
print( xpcall( main1, tb, false, true, false, false, false ) )
___()
print( xpcall( main1, tb, true, false, false, false, false ) )
___()
print( xpcall( main1, tb, false, false, false, true, false ) )
___()
print( xpcall( main1, tb, false, false, true, true, false ) )
___()
print( xpcall( main1, tb, false, false, false, false, true, 20, 4, true ) )
___()
print( xpcall( main1, tb, false, false, false, false, true, 80, 3, true ) )

