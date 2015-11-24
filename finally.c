/* Lua module providing a `finally` function for deterministic
 * resource cleanup.
 *
 * The value returned when `require`ing this module is a function
 *
 *     local finally = require( "finally" )
 *
 * with the following behavior:
 *
 *     finally( f, f, ni, ni, b ) ==> ...
 *
 * `finally` calls the function given as the first argument and then
 * calls the function given as the second argument. Even if the first
 * function raises an error, the second function is called anyway. If
 * the second function executes without error, the return values from
 * the first function call are returned (or the error is re-raised).
 * However, if the second function raises an error, that error is
 * propagated and previous return values/errors are lost. To prevent
 * that from happening, `finally` preallocates memory for the second
 * function call. You can specify how much Lua stack slots for local
 * variables (3rd argument) and call frames (4th argument) should be
 * available. The 5th argument, when set to `true`, allows you to find
 * suitable parameters during development by causing an out of memory
 * error as soon as the second function call allocates any additional
 * memory. The last three arguments are optional and default to 100
 * stack slots, 10 call frames, and `nil` (meaning no forced memory
 * errors).
 *
 * Example:
 *
 *     local f
 *     local a, b, c = finally( function()
 *       f = io.open( "file.txt", "w" )
 *       -- do something with f that may raise an error
 *       return 1, 2, 3
 *     end, function()
 *       if f then f:close() end
 *     end )
 */

#include <stddef.h>
#include <lua.h>
#include <lauxlib.h>


/* compatibility Lua 5.2/5.3 */
#if LUA_VERSION_NUM == 502

typedef int lua_KContext;

#define LUA_KFUNCTION( _name ) \
  static int (_name)( lua_State* L, int status, lua_KContext ctx ); \
  static int (_name ## _52)( lua_State* L ) { \
    lua_KContext ctx; \
    int status = lua_getctx( L, &ctx ); \
    return (_name)( L, status, ctx ); \
  } \
  static int (_name)( lua_State* L, int status, lua_KContext ctx )

#define lua_callk( L, na, nr, ctx, cont ) \
  lua_callk( L, na, nr, ctx, cont ## _52 )

#ifdef lua_call
#undef lua_call
#define lua_call( L, na, nr ) \
  (lua_callk)( L, na, nr, 0, NULL )
#endif

#else /* Lua 5.3 */

#define LUA_KFUNCTION( _name ) \
  static int (_name)( lua_State* L, int status, lua_KContext ctx )

#endif


/* struct to save Lua allocator */
typedef struct {
  lua_Alloc alloc;
  void*     ud;
} alloc_state;


/* simulate out of memory for choosing the proper preallocation
 * settings for the cleanup function */
static void* alloc_fail( void* ud, void* ptr, size_t osize,
                         size_t nsize ) {
  alloc_state* as = ud;
  if( nsize > 0 && (ptr == NULL || osize < nsize) ) {
#if 0
    fprintf( stderr, "[alloc] ptr: %p, osize: %zu, nsize: %zu\n",
             ptr, osize, nsize );
#endif
    return NULL;
  }
  return as->alloc( as->ud, ptr, osize, nsize );
}


/* preallocate stack frames, preallocate stack slots, change Lua
 * allocator (if in debug mode), and call the cleanup function */
LUA_KFUNCTION( preallocatek ) {
  lua_Integer calls = 0, stack = 0;
  (void)status;
  switch( ctx ) {
    case 0:
      calls = lua_tointeger( L, 2 );
      stack = lua_tointeger( L, 3 );
      if( stack )
        luaL_checkstack( L, (int)stack, "preallocate" );
      if( calls > 0 ) {
        lua_pushvalue( L, 1 );
        lua_pushvalue( L, 1 );
        lua_pushinteger( L, calls-1 );
        lua_callk( L, 2, 0, 1, preallocatek );
    case 1:
        if( lua_islightuserdata( L, 4 ) ) {
          alloc_state* as = lua_touserdata( L, 4 );
          lua_setallocf( L, alloc_fail, as );
        }
        if( lua_isfunction( L, 5 ) ) {
          lua_settop( L, 5 );
          lua_call( L, 0, 0 );
        }
      } else
        return lua_yield( L, 0 );
  }
  return 0;
}

static int preallocate( lua_State* L ) {
  return preallocatek( L, 0, 0 );
}


static int lfinally( lua_State* L ) {
  lua_Integer minstack = 0, mincalls = 0;
  int debug = 0, status = 0, status2 = 0;
  alloc_state as = { 0, 0 };
  lua_State* L2 = NULL;
  luaL_checktype( L, 1, LUA_TFUNCTION );
  luaL_checktype( L, 2, LUA_TFUNCTION );
  minstack = luaL_optinteger( L, 3, 100 );
  luaL_argcheck( L, minstack > 0, 3,
                 "invalid number of reserved stack slots" );
  mincalls = luaL_optinteger( L, 4, 10 );
  luaL_argcheck( L, mincalls > 0, 4,
                 "invalid minimum number of call frames" );
  mincalls += 1; /* stack frame(s) used internally */
  debug = lua_toboolean( L, 5 );
  lua_settop( L, 2 );
  /* prepare thread to run the cleanup function */
  L2 = lua_newthread( L );
  lua_pushcfunction( L2, preallocate );
  lua_pushvalue( L2, -1 );
  lua_pushinteger( L2, mincalls );
  lua_pushinteger( L2, minstack );
  if( debug ) {
    as.alloc = lua_getallocf( L, &as.ud );
    lua_pushlightuserdata( L2, &as );
  } else
    lua_pushnil( L2 );
  lua_pushvalue( L, 2 );
  lua_xmove( L, L2, 1 );
  lua_replace( L, 2 ); /* L: [ function | thread ] */
  /* preallocate stack frames and stack slots for cleanup function */
  status = lua_resume( L2, L, 5 );
  if( status != LUA_YIELD ) { /* must be an error */
    lua_xmove( L2, L, 1 );
    lua_error( L );
  }
  lua_settop( L, 3 ); /* reserve a stack slot for later */
  /* run main function */
  lua_pushvalue( L, 1 );
  status = lua_pcall( L, 0, LUA_MULTRET, 0 );
  /* run cleanup function in the other thread */
  lua_settop( L2, 0 );
  status2 = lua_resume( L2, L, 0 );
  if( debug ) /* reset memory allocation function */
    lua_setallocf( L, as.alloc, as.ud );
  if( status2 != 0 && status2 != LUA_YIELD ) {
    lua_remove( L, 3 ); /* make room */
    lua_xmove( L2, L, 1 ); /* error message from other thread */
    lua_error( L );
  }
  if( status != 0 )
    lua_error( L ); /* re-raise error from main function */
  return lua_gettop( L )-3; /* return values from main function */
}


#ifndef EXPORT
#  define EXPORT extern
#endif

EXPORT int luaopen_finally( lua_State* L ) {
  lua_pushcfunction( L, lfinally );
  return 1;
}

