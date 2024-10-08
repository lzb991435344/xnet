#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "xnet.h"
#include "xnet_config.h"
#include "lua_binding.h"


typedef struct {
    xnet_init_config_t init;
    char *luabooter;
} server_config_t;

static void
call_lua_start(lua_State *L, xnet_context_t *ctx) {
	lua_getglobal(L, "Start");
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		xnet_error(ctx, "Start error:%s", lua_tostring(L, -1));
		return;
	}
}

static void
call_lua_init(lua_State *L, xnet_context_t *ctx) {
	lua_getglobal(L, "Init");
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		xnet_error(ctx, "Init error:%s", lua_tostring(L, -1));
		return;
	}
}

static void
call_lua_stop(lua_State *L, xnet_context_t *ctx) {
	lua_getglobal(L, "Stop");
	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		xnet_error(ctx, "Stop error:%s", lua_tostring(L, -1));
		return;
	}
}


static void
listen_func(xnet_context_t *ctx, int sock_id, int acc_sock_id) {
	xnet_socket_t *ns = xnet_get_socket(ctx, acc_sock_id);
    lua_State *L = ctx->user_ptr;

	int ftype = lua_getfield(L, LUA_REGISTRYINDEX, "reg_funcs");
	if (ftype != LUA_TTABLE) {
		xnet_error(ctx, "reg_funcs is not a table %d", ftype);
		return;
	}

	if (lua_getfield(L, -1, "listen") == LUA_TFUNCTION) {
		lua_pushinteger(L, sock_id);
		lua_pushinteger(L, acc_sock_id);
		lua_pushlstring(L, (char*)&ns->addr_info, sizeof(xnet_addr_t));
		if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
			xnet_error(ctx, "call listen fail:%s", lua_tostring(L, -1));
			return;
		}
	}
}

static void
error_func(xnet_context_t *ctx, int sock_id, short what) {
	lua_State *L = ctx->user_ptr;
	xnet_socket_t *s = xnet_get_socket(ctx, sock_id);
	if (s->unpacker) {
		xnet_unpacker_free(s->unpacker);
		s->unpacker = NULL;
	}

	int ftype = lua_getfield(L, LUA_REGISTRYINDEX, "reg_funcs");
	if (ftype != LUA_TTABLE) {
		xnet_error(ctx, "reg_funcs is not a table %d", ftype);
		return;
	}

	if (lua_getfield(L, -1, "error") == LUA_TFUNCTION) {
		lua_pushinteger(L, sock_id);
		lua_pushinteger(L, what);
		if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
			xnet_error(ctx, "call error fail:%s", lua_tostring(L, -1));
			return;
		}		
	}
}

static int
recv_func(xnet_context_t *ctx, int sock_id, char *buffer, int size, xnet_addr_t *addr_info) {
	xnet_socket_t *s = xnet_get_socket(ctx, sock_id);
	if (s->unpacker != NULL) {
		if (xnet_unpacker_recv(s->unpacker, buffer, size) != 0) {
			xnet_error(ctx, "unpacker recv error");
		}
		return 0;
	}

	//normal forward
	lua_State *L = ctx->user_ptr;
	int ftype = lua_getfield(L, LUA_REGISTRYINDEX, "reg_funcs");
	if (ftype != LUA_TTABLE) {
		xnet_error(ctx, "reg_funcs is not a table %d", ftype);
		return 0;
	}

	if (lua_getfield(L, -1, "recv") == LUA_TFUNCTION) {
		lua_pushinteger(L, sock_id);
		lua_pushinteger(L, 0);
		lua_pushlstring(L, buffer, size);
		lua_pushinteger(L, size);
		lua_pushlstring(L, (char*)addr_info, sizeof(xnet_addr_t));
		if (lua_pcall(L, 5, 0, 0) != LUA_OK) {
			xnet_error(ctx, "call recv error:%s", lua_tostring(L, -1));
			return 0;
		}
	}
    return 0;//return 0, buffer free by xnet
}

static void
timeout_func(xnet_context_t *ctx, int id) {
	lua_State *L = ctx->user_ptr;
    int ftype = lua_getfield(L, LUA_REGISTRYINDEX, "reg_funcs");
    if (ftype != LUA_TTABLE) {
    	xnet_error(ctx, "reg_funcs is not a table %d", ftype);
    	return;
    }
    if (lua_getfield(L, -1, "timeout") == LUA_TFUNCTION) {
    	lua_pushinteger(L, id);
    	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
    		xnet_error(ctx, "call timeout error:%s", lua_tostring(L, -1));
    		return;
    	}
    }
}


static int
command_func(xnet_context_t *ctx, xnet_context_t *source, int command, void *data, int sz) {
	lua_State *L = ctx->user_ptr;
	int ftype = lua_getfield(L, LUA_REGISTRYINDEX, "reg_funcs");
	if (ftype != LUA_TTABLE) {
		xnet_error(ctx, "reg_funcs is not a table %d", ftype);
		return 0;
	}
	if (lua_getfield(L, -1, "comand") == LUA_TFUNCTION) {
		lua_pushlightuserdata(L, source);
		lua_pushinteger(L, command);
		lua_pushlstring(L, data, sz);
		lua_pushinteger(L, sz);
		if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
			xnet_error(ctx, "call command error:%s", lua_tostring(L, -1));
			return 0;
		}
	}
    return 0;//返回0，由xnet释放data
}

static void
connect_func(struct xnet_context_t *ctx, int sock_id, int error) {
	lua_State *L = ctx->user_ptr;
	int ftype = lua_getfield(L, LUA_REGISTRYINDEX, "reg_funcs");
	if (ftype != LUA_TTABLE) {
		xnet_error(ctx, "reg_funcs is not a table %d", ftype);
		return;
	}    
	if (lua_getfield(L, -1, "connected") == LUA_TFUNCTION) {
		lua_pushinteger(L, sock_id);
		lua_pushinteger(L, error);
		if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
			xnet_error(ctx, "call connected error:%s", lua_tostring(L, -1));
			return;
		}
	}
}

static void
bind_event(xnet_context_t *ctx) {
	xnet_register_event(ctx, listen_func, error_func, recv_func,            \
    					connect_func, timeout_func, command_func);
}

static void
start_init_config(server_config_t *server_config, xnet_config_t *config, const char *config_name) {
	char *value = NULL;
	//default value
	server_config->init.log_path = NULL;
	server_config->init.disable_thread = false;
	server_config->luabooter = NULL;
	xnet_config_init(config);

	if (!config_name) return;

	if (xnet_parse_config(config, config_name) != 0) {
		xnet_release_config(config);
		printf("parse server config %s error\n", config_name);
		exit(1);
	}
	
	if (xnet_get_field2s(config, "log_path", &value))
		server_config->init.log_path = strdup(value);

	xnet_get_field2b(config, "disable_log_thread", &server_config->init.disable_thread);

	if (xnet_get_field2s(config, "luabooter", &value))
		server_config->luabooter = strdup(value);

}

static void
release_config(server_config_t *server_config, xnet_config_t *config) {
	if (server_config->init.log_path)
		free(server_config->init.log_path);
	if (server_config->luabooter)
		free(server_config->luabooter);
	if (config)
		xnet_release_config(config);
}

int
main(int argc, char **argv) {
	lua_State *L = NULL;
	xnet_context_t *ctx = NULL;
	server_config_t server_config;
	xnet_config_t config;
	const char *config_name = NULL;
	int ret;

	if (argc < 2) {
		printf("must supply a config file name\n");
		exit(1);
	}

	config_name = argv[1];
	start_init_config(&server_config, &config, config_name);

	ret = xnet_init((xnet_init_config_t*)&server_config);
	if (ret != 0) {
		printf("xnet init error!\n");
		goto error;
	}
	ctx = xnet_create_context();
	if (!ctx) {
		printf("create context error\n");
		goto error;
	}
	bind_event(ctx);
	L = luaL_newstate();
	if (!L) {
		printf("init lua state error\n");
		goto error;
	}
	luaL_openlibs(L);

	//lua state must have "Start", "Init" and "Stop" function
	ret = luaL_dofile(L, server_config.luabooter ? server_config.luabooter : "main.lua");
	if (ret != LUA_OK) {
		xnet_error(ctx, "lua error:%s", lua_tostring(L, -1));
		goto error;
	}

	xnet_bind_lua(L, ctx, &config);
	printf("start lua function\n");
	call_lua_start(L, ctx);
	call_lua_init(L, ctx);

	xnet_dispatch_loop(ctx);

	call_lua_stop(L, ctx);

	lua_close(L);
	xnet_destroy_context(ctx);
	xnet_deinit();
	release_config(&server_config, &config);
	return 0;
error:
	if (L) lua_close(L);
	if (ctx) xnet_destroy_context(ctx);
	xnet_deinit();
	release_config(&server_config, &config);
	return 1;
}
