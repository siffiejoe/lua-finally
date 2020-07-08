/* Lua module providing a `finally` function for deterministic
 * resource cleanup.
 */

#include <stddef.h>
#include <lua.h>
#include <lauxlib.h>


/* Lua version compatibility */
#if LUA_VERSION_NUM == 501 /* Lua 5.1 / LuaJIT */

#define lua_resume( L2, L, na, nr ) \
  ((void)(L), (void)(nr), lua_resume( L2, na ))

#elif LUA_VERSION_NUM == 502 /* Lua 5.2 */

#define lua_resume( L2, L, na, nr ) \
  ((void)(nr), lua_resume( L2, L, na ))

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

#elif LUA_VERSION_NUM == 503

#define lua_resume( L2, L, na, nr ) \
  ((void)(nr), lua_resume( L2, L, na ))

#define LUA_KFUNCTION( _name ) \
  static int (_name)( lua_State* L, int status, lua_KContext ctx )

#elif LUA_VERSION_NUM == 504 /* Lua 5.3/5.4 */

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


#if LUA_VERSION_NUM > 501 /* Lua 5.2+ */

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
  "local setalloc, yield = ...\n"
  "local function postprocess( as, cleanup, ... )\n"
  "  if cleanup then\n"
  "    if as then setalloc( as ) end\n"
  "    cleanup( ... )\n"
  "  else\n"
  "    return ...\n"
  "  end\n"
  "end\n"
  "return function( prealloc, calls, slots, as, f )\n"
  "  local _1,_2,_3,_4,_5,_6,_7,_8,_9,_10\n"
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
    lua_pushcfunction( L, lsetalloc );
    lua_pushcfunction( L, lyield );
    lua_call( L, 2, 1 );
    lua_pushlightuserdata( L, (void*)preallocate_code );
    lua_pushvalue( L, -2 );
    lua_rawset( L, LUA_REGISTRYINDEX );
  }
}

#endif


static int lfinally( lua_State* L ) {
  lua_Integer minstack = 0, mincalls = 0;
  int debug = 0, status = 0, status2 = 0, nret = 0;
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
   * Also we can't preallocate a variable amount of stack slots so
   * that they won't cause a panic on memory allocation failure *and*
   * survive a garbage collection cycle during the main function.
   * The above mentioned Lua closure allocates some extra locals, and
   * if you need more you can increase the number of reserved calls
   * (each extra call will give you about 15 slots). */
  push_lua_prealloc( L2 );
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
  status = lua_resume( L2, L, 5, &nret );
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
  status2 = lua_resume( L2, L, !!status, &nret );
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

