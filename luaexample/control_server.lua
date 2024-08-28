package.path = "lualib/?.lua;"

local pack = require "pack"
local util = require "util"

local socket_list = { }

function Start()
	print("lua start!")
	local port = 8989
	local rc, sock = xnet.tcp_listen("0.0.0.0", port, 5)
	print("lua tcp_listen", rc, sock, port)

	xnet.register({
		listen = function(sid, new_sid, addr)
			print("----lua: new connection", sid, new_sid, xnet.addrtoa(addr))
			socket_list[new_sid] = new_sid
			xnet.register_packer(new_sid, xnet.PACKER_TYPE_LINE)
		end,
		error = function(sid, what)
			print("----lua: error", sid, what)
			socket_list[sid] = nil
		end,
		recv = function(sid, pkg_type, pkg, sz, addr)
			print("----lua: recv", sid, pkg_type, pkg, sz, xnet.addrtoa(addr))
		end,
	})
end

function Init()
	print "lua init!"
end

function Stop()
	print "lua stop!"
end
