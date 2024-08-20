#define GET_XNET_CTX xnet_context_t *ctx;            \
lua_getfield((L), LUA_REGISTRYINDEX, "xnet_ctx");    \
ctx = lua_touserdata((L), -1);                       \
if (ctx == NULL) {                                   \
	return luaL_error(L, "init xnet context first"); \
}                                                    \
lua_pop(L, 1);                                       \

static int
_xnet_tcp_connect(lua_State *L) {
	//param:ip/addr
	//returns:sock id
	GET_XNET_CTX

	const char *addr = luaL_checkstring(L, 1);
	int port = (int)luaL_checkinteger(L, 2);
	xnet_socket_t *sock;
	int rc = xnet_tcp_connect(ctx, addr, port, &sock);
	if (rc == -1) {
		lua_pushinteger(L, rc);
		return 1;
	}

	lua_pushinteger(L, rc);
	lua_pushlightuserdata(L, sock);
	return 2;
}

static int
_xnet_tcp_listen(lua_State *L) {
	GET_XNET_CTX
	const char *host = luaL_checkstring(L, 1);
	int port = luaL_checkinteger(L, 2);
	int backlog = luaL_optinteger(L, 3, 5);
	lua_getfield(L, LUA_REGISTRYINDEX, "xnet_ctx");
	xnet_socket_t *s = NULL;
	int rc = xnet_tcp_listen(ctx, host, port, backlog, &s);
	if (rc == -1) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, 1);
	lua_pushlightuserdata(L, s);
	return 2;
}

static int
_xnet_tcp_send_buffer(lua_State *L) {
	GET_XNET_CTX
	if (!lua_islightuserdata(L, 1)) {
		luaL_error(L, "error sock");
	}
	xnet_socket_t *s = lua_touserdata(L, 1);
	int type = lua_type(L, 2);
	if (type == LUA_TSTRING) {
		const char *buffer = NULL;
		size_t sz = 0;
		buffer = lua_tolstring(L, 2, &sz);
		xnet_tcp_send_buffer(ctx, s, buffer, (int)sz, false);
	} else if (type == LUA_TLIGHTUSERDATA) {
		//light user data is don't need free.
		char *buffer = lua_touserdata(L, 2);
		int sz = luaL_checkinteger(L, 3);
		xnet_tcp_send_buffer(ctx, s, buffer, sz, true);
	} else {
		luaL_error(L, "error buffer type");
	}
	return 0;
}

static int
_xnet_close_socket(lua_State *L) {
	GET_XNET_CTX
	if (!lua_islightuserdata(L, 1)) {
		luaL_error(L, "error sock");
	}
	xnet_socket_t *s = lua_touserdata(L, 1);
	xnet_close_socket(ctx, s);
	return 0;
}

static int
_xnet_add_timer(lua_State *L) {
	GET_XNET_CTX
	int id = luaL_checkinteger(L, 1);
	int timeout = luaL_checkinteger(L, 2);

	int rc = xnet_add_timer(ctx, id, timeout);
	lua_pushinteger(L, rc);
	return 1;
}

static int
_xnet_exit(lua_State *L) {
	GET_XNET_CTX
	xnet_exit(ctx);
	return 0;
}

static int
_xnet_udp_listen(lua_State *L) {
	GET_XNET_CTX
	const char *host = luaL_checkstring(L, 1);
	int port = luaL_checkinteger(L, 2);

	xnet_socket_t *s = NULL;
	int rc = xnet_udp_listen(ctx, host, port, &s);
	if (rc == -1) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, 1);
	lua_pushlightuserdata(L, s);
	return 2;
}

static int
_xnet_udp_sendto(lua_State *L) {
	GET_XNET_CTX
	if (!lua_islightuserdata(L, 1)) {
		luaL_error(L, "error sock");
	}
	if (!lua_islightuserdata(L, 2)) {
		luaL_error(L, "error addr");
	}
	xnet_socket_t *s = lua_touserdata(L, 1);
	xnet_addr_t *addr = lua_touserdata(L, 2);
	int type = lua_type(L, 3);
	if (type == LUA_TSTRING) {
		size_t size = 0;
		const char *buffer = lua_tolstring(L, 3, &size);
		xnet_udp_sendto(ctx, s, addr, buffer, (int)size, false);
	} else if (type == LUA_TLIGHTUSERDATA) {
		char *buffer = lua_touserdata(L, 3);
		int size = luaL_checkinteger(L, 4);
		xnet_udp_sendto(ctx, s, addr, buffer, size, true);
	} else {
		luaL_error(L, "error buffer type");
	}
	return 0;
}

static int
_xnet_udp_create(lua_State *L) {
	GET_XNET_CTX
	int protocol = luaL_checkinteger(L, 1);
	if (protocol != SOCKET_PROTOCOL_UDP && protocol != SOCKET_PROTOCOL_UDP_IPV6) {
		luaL_error(L, "unknow protocol %d", protocol);
	}

	xnet_socket_t *s = NULL;
	int rc = xnet_udp_create(ctx, protocol, &s);
	if (rc == -1) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, 1);
	lua_pushlightuserdata(L, s);
	return 2;
}

static int
_xnet_udp_set_addr(lua_State *L) {
	GET_XNET_CTX
	if (!lua_islightuserdata(L, 1)) {
		luaL_error(L, "error sock");
	}
	xnet_socket_t *s = lua_touserdata(L, 1);
	const char *host = luaL_checkstring(L, 2);
	int port = luaL_checkinteger(L, 3);
	int rc = xnet_udp_set_addr(ctx, s, host, port);
	if (rc == -1) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, 1);
	return 1;
}

static int
_xnet_udp_send_buffer(lua_State *L) {
	GET_XNET_CTX
	if (!lua_islightuserdata(L, 1)) {
		luaL_error(L, "error sock");
	}
	xnet_socket_t *s = lua_touserdata(L, 1);
	int type = lua_type(L, 2);
	if (type == LUA_TSTRING) {
		size_t size = 0;
		const char *buffer = lua_tolstring(L, 2, &size);
		xnet_udp_send_buffer(ctx, s, buffer, (int)size, false);
	} else if (type == LUA_TLIGHTUSERDATA) {
		char *buffer = lua_touserdata(L, 2);
		int size = luaL_checkinteger(L, 3);
		xnet_udp_send_buffer(ctx, s, buffer, size, true);
	} else {
		luaL_error(L, "error buffer type");
	}
	return 0;
}

static int
_register(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_setfield(L, LUA_REGISTRYINDEX, "reg_funcs");
	return 0;
}

static int
_xnet_addrtoa(lua_State *L) {
	if (!lua_islightuserdata(L, 1)) {
		luaL_error(L, "error addr type");
	}
	char str[64] = {0};
	xnet_addr_t *addr = lua_touserdata(L, 1); 
	xnet_addrtoa(addr, str);
	lua_pushstring(L, str);
	return 1;
}

static void
xnet_bind_lua(lua_State *L, xnet_context_t *ctx) {
	ctx->user_ptr = L;
	lua_pushlightuserdata(L, ctx);
	lua_setfield(L, LUA_REGISTRYINDEX, "xnet_ctx");
	lua_newtable(L);

	//xnet_tcp_connect
	lua_pushcfunction(L, _xnet_tcp_connect);
	lua_setfield(L, -2, "tcp_connect");

	//xnet_tcp_listen
	lua_pushcfunction(L, _xnet_tcp_listen);
	lua_setfield(L, -2, "tcp_listen");

	//xnet_tcp_send_buffer
	lua_pushcfunction(L, _xnet_tcp_send_buffer);
	lua_setfield(L, -2, "tcp_send_buffer");

	//xnet_udp_listen
	lua_pushcfunction(L, _xnet_udp_listen);
	lua_setfield(L, -2, "udp_listen");

	//xnet_udp_sendto
	lua_pushcfunction(L, _xnet_udp_sendto);
	lua_setfield(L, -2, "udp_sendto");

	//xnet_udp_create
	lua_pushcfunction(L, _xnet_udp_create);
	lua_setfield(L, -2, "udp_create");

	//xnet_udp_set_addr
	lua_pushcfunction(L, _xnet_udp_set_addr);
	lua_setfield(L, -2, "udp_set_addr");

	//xnet_udp_send_buffer
	lua_pushcfunction(L, _xnet_udp_send_buffer);
	lua_setfield(L, -2, "udp_send_buffer");

	//xnet_close_socket
	lua_pushcfunction(L, _xnet_close_socket);
	lua_setfield(L, -2, "close_socket");
	//xnet_add_timer
	lua_pushcfunction(L, _xnet_add_timer);
	lua_setfield(L, -2, "add_timer");
	//xnet_exit
	lua_pushcfunction(L, _xnet_exit);
	lua_setfield(L, -2, "exit");

	//register
	lua_pushcfunction(L, _register);
	lua_setfield(L, -2, "register");

	//addrtoa
	lua_pushcfunction(L, _xnet_addrtoa);
	lua_setfield(L, -2, "addrtoa");

	//protocol type
	lua_pushinteger(L, SOCKET_PROTOCOL_TCP);
	lua_setfield(L, -2, "PROTOCOL_TCP");
	lua_pushinteger(L, SOCKET_PROTOCOL_UDP);
	lua_setfield(L, -2, "PROTOCOL_UDP");
	lua_pushinteger(L, SOCKET_PROTOCOL_UDP_IPV6);
	lua_setfield(L, -2, "PROTOCOL_UDP_IPV6");

	lua_setglobal(L, "xnet");
}