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
 *     local f1, f2
 *     local same = finally( function()
 *       f1 = assert( io.open( "filename1.txt", "r" ) )
 *       f2 = assert( io.open( "filename2.txt", "r" ) )
 *       return f1:read( "*a" ) == f2:read( "*a" )
 *     end, function( e )
 *       if e then print( "there was an error!" ) end
 *       if f2 then f2:close() end
 *       if f1 then f1:close() end
 *     end )
 */

#include <stddef.h>
#include <lua.h>
#include <lauxlib.h>


/* Lua version compatibility */
#if LUA_VERSION_NUM == 501 /* Lua 5.1 / LuaJIT */

#define lua_resume( L2, L, na ) \
  ((void)(L), lua_resume( L2, na ))

#elif LUA_VERSION_NUM == 502 /* Lua 5.2 */

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

#elif LUA_VERSION_NUM == 503 /* Lua older than 5.1 or new than 5.3 */

#define LUA_KFUNCTION( _name ) \
  static int (_name)( lua_State* L, int status, lua_KContext ctx )

#else

#error unsupported Lua version

#endif /* LUA_VERSION_NUM */


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


#if LUA_VERSION_NUM > 501 /* Lua 5.2/5.3 */

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
        lua_callk( L, 2, LUA_MULTRET, 1, preallocatek );
    case 1:
        if( lua_isfunction( L, 5 ) ) {
          alloc_state* as = lua_touserdata( L, 4 );
          if( as )
            lua_setallocf( L, alloc_fail, as );
          lua_call( L, lua_gettop( L )-5, 0 );
          return 0;
        }
      } else
        return lua_yield( L, 0 );
  }
  return lua_gettop( L ) > 2;
}

static int preallocate( lua_State* L ) {
  return preallocatek( L, 0, 0 );
}

#else /* Lua 5.1 */

static int lcheckstack( lua_State* L ) {
  int slots = (int)luaL_checkinteger( L, 1 );
  luaL_checkstack( L, slots, "preallocate" );
  return 0;
}

static int lsetalloc( lua_State* L ) {
  alloc_state* as = lua_touserdata( L, 1 );
  if( as )
    lua_setallocf( L, alloc_fail, as );
  return 0;
}

static int lyield( lua_State* L ) {
  return lua_yield( L, lua_gettop( L ) );
}

static char const preallocate_code[] =
  "local checkstack, setalloc, yield = ...\n"
  "local function postprocess( as, cleanup, ... )\n"
  "  if cleanup then\n"
  "    if as then setalloc( as ) end\n"
  "    cleanup( ... )\n"
  "  else\n"
  "    return ...\n"
  "  end\n"
  "end\n"
  "return function( prealloc, calls, slots, as, f )\n"
  "  if slots then checkstack( slots ) end\n"
  "  if calls > 0 then\n"
  "    return postprocess( as, f, prealloc( prealloc, calls-1 ) )\n"
  "  else\n"
  "    return yield()\n"
  "  end\n"
  "end\n";

static void push_lua_prealloc( lua_State* L ) {
  lua_pushlightuserdata( L, (void*)preallocate_code );
  lua_rawget( L, LUA_REGISTRYINDEX );
  if( lua_type( L, -1 ) != LUA_TFUNCTION ) {
    lua_pop( L, 1 );
    if( luaL_loadbuffer( L, preallocate_code,
                         sizeof( preallocate_code )-1,
                         "=(embedded)" ) )
      lua_error( L );
    lua_pushcfunction( L, lcheckstack );
    lua_pushcfunction( L, lsetalloc );
    lua_pushcfunction( L, lyield );
    lua_call( L, 3, 1 );
    lua_pushlightuserdata( L, (void*)preallocate_code );
    lua_pushvalue( L, -2 );
    lua_rawset( L, LUA_REGISTRYINDEX );
  }
}

#endif


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
  debug = lua_toboolean( L, 5 );
  lua_settop( L, 2 );
  /* prepare thread to run the cleanup function */
  L2 = lua_newthread( L );
#if LUA_VERSION_NUM > 501
  mincalls += 1; /* stack frame(s) used internally */
  lua_pushcfunction( L2, preallocate );
#else
  /* Lua 5.1 doesn't support yieldable C functions, so we use a Lua
   * closure to preallocate stack and call frames. This only allocates
   * *Lua* call frames though, not C call frames, so you could hit a
   * limit there while executing the cleanup function later!
   * Also, we have to allocate the extra stack slots here (because
   * allocating them within a lua_CFunction would make them
   * collectable as soon as the function returns). But we can't raise
   * an error here (because it would be unprotected), so we do an
   * additional fake `luaL_checkstack()` in the Lua closure somewhere
   * to raise the potential error there!
   */
  push_lua_prealloc( L2 );
  lua_checkstack( L2, (int)minstack );
#endif
  lua_pushvalue( L2, -1 );
  lua_pushinteger( L2, mincalls );
  lua_pushinteger( L2, minstack );
  if( debug ) {
    as.alloc = lua_getallocf( L, &as.ud );
    lua_pushlightuserdata( L2, &as );
  } else
    lua_pushnil( L2 );
  lua_pushvalue( L, 2 ); /* clean up function */
  lua_xmove( L, L2, 1 );
  lua_replace( L, 2 ); /* L: [ function | thread ] */
  /* preallocate stack frames and stack slots for cleanup function,
   * and then yield ... */
  status = lua_resume( L2, L, 5 );
  if( status != LUA_YIELD ) { /* must be an error */
    lua_xmove( L2, L, 1 );
    lua_error( L );
  }
  /* run main function */
  lua_pushvalue( L, 1 );
  status = lua_pcall( L, 0, LUA_MULTRET, 0 );
  /* run cleanup function in the other thread by resuming yielded
   * coroutine */
  lua_settop( L2, 0 );
  if( status != 0 ) { /* pass error to cleanup function */
    lua_pushvalue( L, -1 ); /* duplicate error message */
    lua_xmove( L, L2, 1 ); /* move to thread */
  }
  status2 = lua_resume( L2, L, !!status );
  if( debug ) /* reset memory allocation function */
    lua_setallocf( L, as.alloc, as.ud );
  if( status2 == LUA_YIELD ) {
    /* cleanup function shouldn't yield; can only happen in Lua 5.1 */
    lua_settop( L, 0 ); /* make room */
    lua_pushvalue( L, lua_upvalueindex( 1 ) );
    lua_error( L );
  } else if( status2 != 0 ) { /* error in cleanup function */
    lua_settop( L, 0 ); /* make room */
    lua_xmove( L2, L, 1 ); /* error message from other thread */
    lua_error( L );
  }
  if( status != 0 )
    lua_error( L ); /* re-raise error from main function */
  return lua_gettop( L )-2; /* return results from main function */
}


#ifndef EXPORT
#  define EXPORT extern
#endif

EXPORT int luaopen_finally( lua_State* L ) {
  lua_pushliteral( L, "'finally' cleanup function shouldn't yield" );
  lua_pushcclosure( L, lfinally, 1 );
  return 1;
}

