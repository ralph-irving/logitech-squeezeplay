
--[[
=head1 NAME

jive.net.NetworkThread - thread for network IO

=head1 DESCRIPTION

Implements a separate thread (using luathread) for network functions. The base class for network protocols is Socket. Messaging from/to the thread uses mutexed queues. The queues contain functions executed once they leave the queue in the respective thread.
The network thread queue is polled repeatedly. The main thread queue shall serviced by the main code; currently, an event EVENT_SERVICE_JNT exists for this purpose.

FIXME: Subscribe description

=head1 SYNOPSIS

 -- Create a NetworkThread (done for you in JiveMain stored in global jnt)
 jnt = NetworkThread()

 -- Create an HTTP socket that uses this jnt
 local http = SocketHttp(jnt, '192.168.1.1', 80)

=head1 FUNCTIONS

=cut
--]]
-----------------------------------------------------------------------------
-- Convention: functions/methods starting with t_ are executed in the thread
-----------------------------------------------------------------------------


-- stuff we use
local _assert, tostring, table, ipairs, pairs, pcall, type  = _assert, tostring, table, ipairs, pairs, pcall, type

local thread            = require("thread")
local socket            = require("socket")
local debug             = require("debug")
local oo                = require("loop.base")

local queue             = require("jive.utils.queue")
local Event             = require("jive.ui.Event")
local Framework         = require("jive.ui.Framework")

local log               = require("jive.utils.log").logger("net.thread")

local perfhook          = jive.perfhook

local EVENT_SERVICE_JNT = jive.ui.EVENT_SERVICE_JNT
local EVENT_CONSUME     = jive.ui.EVENT_CONSUME

-- jive.net.NetworkThread is a base class
module(..., oo.class)

-- constants
local QUEUE_SIZE        = 100  -- size of the queue from/to the jnt
local TIMEOUT           = 0.05 -- select timeout (seconds)


-- _add
-- adds a socket to the read or write list
-- timeout == 0 => no time out!
local function _add(sock, pump, sockList, timeout)
--	log:debug("_add(", sock, ", ", pump, ")")

	_assert(pump, debug.traceback())
	
	if not sock then 
		return
	end
	
	-- add us if we're not already in there
	if not sockList[sock] then
	
		-- add the socket to the sockList
		table.insert(sockList, sock)
	end	
	
	-- remember the pump, the time and the desired timeout
	sockList[sock] = {
		pumpIt = pump,
		lastSeen = socket.gettime(),
		timeout = timeout or 60
	}
end


-- _remove
-- removes a socket from the read or write list
local function _remove(sock, sockList)
--	log:debug("_remove(", sock, ")")
	
	if not sock then 
		return 
	end

	-- remove the socket from the sockList
	if sockList[sock] then
		
		sockList[sock] = nil

		for i,v in ipairs(sockList) do
			if v == sock then
				table.remove(sockList, i)
				return
			end
		end
	end
end


-- t_add/remove/read/write
-- add/remove sockets api
function t_addRead(self, sock, pump, timeout)
--	log:debug("NetworkThread:t_addRead()")
	
	_add(sock, pump, self.t_readSocks, timeout)
end

function t_removeRead(self, sock)
--	log:debug("NetworkThread:t_removeRead()")
	
	_remove(sock, self.t_readSocks)
end

function t_addWrite(self, sock, pump, timeout)
--	log:debug("NetworkThread:t_addWrite()")
	
	_add(sock, pump, self.t_writeSocks, timeout)
end

function t_removeWrite(self, sock)
--	log:debug("NetworkThread:t_removeWrite()")
	
	_remove(sock, self.t_writeSocks)
end


-- _timeout
-- manages the timeout of our sockets
local function _timeout(now, sockList)
--	log:debug("NetworkThread:_timeout()")

	for v, t in pairs(sockList) do
		-- the sockList contains both sockList[i] = sock and sockList[sock] = {pumpIt=,lastSeem=}
		-- we want the second case, the sock is a userdata (implemented by LuaSocket)
		-- we also want the timeout to exist and have expired
		if type(v) == "userdata" and t.timeout > 0 and now - t.lastSeen > t.timeout then
			log:error("network thread timeout for ", v)
			t.pumpIt("inactivity timeout")
		end
	end
end


-- _t_select
-- runs our sockets through select
local function _t_select(self)
--	log:debug("_t_select(r", #self.t_readSocks, " w", #self.t_writeSocks, ")")
	
	local r,w,e = socket.select(self.t_readSocks, self.t_writeSocks, TIMEOUT)
	
	local now = socket.gettime()
		
	if e and self.running then
		-- timeout is a normal error for select if there's nothing to do!
		if e ~= 'timeout' then
			log:error(e)
		end
	else
		-- call the write pumps
		for i,v in ipairs(w) do
			self.t_writeSocks[v].lastSeen = now
			self.t_writeSocks[v].pumpIt()
		end
		
		-- call the read pumps
		for i,v in ipairs(r) do
			-- debug to track pump error
			if self.t_readSocks[v] == nil then
				log:error("readSocks pump is nil for ", v)
				log:error("readSocks is:")
				for a,b in pairs(self.t_readSocks) do
				log:error("\t", a, " = ", b)
				end
			else
				self.t_readSocks[v].lastSeen = now
				self.t_readSocks[v].pumpIt()
			end
		end
	end
	
	-- manage timeouts
	_timeout(now, self.t_readSocks)
	_timeout(now, self.t_writeSocks)
end


-- _t_dequeue
-- fetches messages from the main thread
local function _t_dequeue(self)
--	log:debug("_t_dequeue()")
	
	local msg = true
	while msg do
		msg = self.in_queue:remove(false)
		if msg then
			msg()
		end
	end
end


-- _thread
-- the thread function with the endless loop
local function _t_thread(self)
	local ok, err

--	perfhook(50)

--	log:info("NetworkThread starting...")

	while self.running do
		local t0 = Framework:getTicks()

		ok, err = pcall(_t_select, self)
		if not ok then
			log:warn("error in _t_select: " .. err)
		end

		local t1 = Framework:getTicks()

		local len = self.in_queue:len()
		ok, err = pcall(_t_dequeue, self)
		if not ok then
			log:warn("error in _t_dequeue: " .. err)
		end

		local t2 = Framework:getTicks()

		if t1 - t0 > 105 or t2 - t1 > 10 then
			log:warn("NetworkThread select=", (t1-t0), "ms dequeue=", (t2-t1), " in_queue=", len)
		end
	end
	
	-- clean up here
--	log:info("NetworkThread exiting...")
	self.t_readSocks = nil
	self.t_writeSocks = nil
end


-- t_perform
-- queues a function for execution main thread side
function t_perform(self, func, priority)

	if priority then
		-- log:debug("enqueue high")
		self.out_queueH:insert(func)
	else
		-- log:debug("enqueue low")
		self.out_queueL:insert(func)
	end
end


--[[

=head2 NetworkThread:stop()

Sends a message to stop the thread.

=cut
--]]
function stop(self)
--	log:info("NetworkThread:stop()")
	
	self:perform(function() self.running = false end)
end


--[[

=head2 NetworkThread:perform(func)

Queues a function for execution thread side.

=cut
--]]
function perform(self, func)
--	log:debug("NetworkThread:perform()")
	
	if func then
		self.in_queue:insert(func)	
	end
end


--[[

=head2 NetworkThread:pump()

Processes the queue on the main thread side. Messages are closures (functions), and simply called.

=cut
--]]
function pump(self)
--	log:debug("NetworkThread:idle()")
	
	local t0 = Framework:getTicks()
	local lenH = self.out_queueH:len()
	local lenL = self.out_queueL:len()
	local priority

	local msg = true

	-- process one or more messages in the high priority queue
	while msg do
		msg = self.out_queueH:remove(false)
		if msg then
			priority = true
			msg()
			--log:debug("dequeue high")
		end
	end

	if not priority then
		msg = self.out_queueL:remove(false)
		if msg then
			msg()
			--log:debug("dequeue low")
		end
	end

	local t1 = Framework:getTicks()

	if t1 - t0 > 5 then
		log:warn("NetworkThread pump=", (t1 - t0), "ms out_queueH=", lenH, " out_queueL=", lenL)
	end
end


-- add/remove subscriber
function subscribe(self, object)
--	log:debug("NetworkThread:subscribe()")
	
	if not self.subscribers[object] then
		self.subscribers[object] = 1
	end
end
function unsubscribe(self, object)
--	log:debug("NetworkThread:unsubscribe()")
	
	if self.subscribers[object] then
		self.subscribers[object] = nil
	end
end


-- notify
function notify(self, event, ...)
--	log:info("NetworkThread:notify(", event, ")")
	
	local method = "notify_" .. event
	
	for k,v in pairs(self.subscribers) do
		if k[method] and type(k[method]) == 'function' then
			k[method](k, ...)
		end
	end
end


--[[

=head2 getUUID()

Returns the UUID and Mac address of this device.

=cut
--]]
function getUUID(self)
	return self.uuid, self.mac
end


--[[

=head2 setUUID(uuid, mac)

Sets the UUID and Mac address of this device.

=cut
--]]
function setUUID(self, uuid, mac)
	self.uuid = uuid
	self.mac = mac
end


--[[

=head2 __init()

Creates a new NetworkThread. The thread starts immediately.

=cut
--]]
function __init(self)
--	log:debug("NetworkThread:__init()")

	local obj = oo.rawnew(self, {
		-- create our in and out queues (mutexed)
		in_queue = queue.newqueue(QUEUE_SIZE),
		out_queueH = queue.newqueue(QUEUE_SIZE),
		out_queueL = queue.newqueue(QUEUE_SIZE),

		-- list of sockets for select
		t_readSocks = {},
		t_writeSocks = {},

		-- list of objects for notify
		subscribers = {},
		
		running = true,
	})

	-- we're running
	thread.newthread(_t_thread, {obj})
	
	return obj
end


--[[

=head1 LICENSE

Copyright 2007 Logitech. All Rights Reserved.

This file is subject to the Logitech Public Source License Version 1.0. Please see the LICENCE file for details.

=cut
--]]

