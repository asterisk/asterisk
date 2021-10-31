

CONSOLE = "Console/dsp" -- Console interface for demo
--CONSOLE = "DAHDI/1"
--CONSOLE = "Phone/phone0"

IAXINFO = "guest"       -- IAXtel username/password
--IAXINFO = "myuser:mypass"

TRUNK = "DAHDI/G2"
TRUNKMSD = 1
-- TRUNK = "IAX2/user:pass@provider"


--
-- Extensions are expected to be defined in a global table named 'extensions'.
-- The 'extensions' table should have a group of tables in it, each
-- representing a context.  Extensions are defined in each context.  See below
-- for examples.
--
-- Extension names may be numbers, letters, or combinations thereof. If
-- an extension name is prefixed by a '_' character, it is interpreted as
-- a pattern rather than a literal.  In patterns, some characters have
-- special meanings:
--
--   X - any digit from 0-9
--   Z - any digit from 1-9
--   N - any digit from 2-9
--   [1235-9] - any digit in the brackets (in this example, 1,2,3,5,6,7,8,9)
--   . - wildcard, matches anything remaining (e.g. _9011. matches
--       anything starting with 9011 excluding 9011 itself)
--   ! - wildcard, causes the matching process to complete as soon as
--       it can unambiguously determine that no other matches are possible
--
-- For example the extension _NXXXXXX would match normal 7 digit
-- dialings, while _1NXXNXXXXXX would represent an area code plus phone
-- number preceded by a one.
--
-- If your extension has special characters in it such as '.' and '!' you must
-- explicitly make it a string in the tabale definition:
--
--   ["_special."] = function;
--   ["_special!"] = function;
--
-- There are no priorities.  All extensions to asterisk appear to have a single
-- priority as if they consist of a single priority.
--
-- Each context is defined as a table in the extensions table.  The
-- context names should be strings.
--
-- One context may be included in another context using the 'includes'
-- extension.  This extension should be set to a table containing a list
-- of context names.  Do not put references to tables in the includes
-- table.
--
--   include = {"a", "b", "c"};
--
-- Channel variables can be accessed through the global 'channel' table.
--
--   v = channel.var_name
--   v = channel["var_name"]
--   v.value
--   v:get()
--
--   channel.var_name = "value"
--   channel["var_name"] = "value"
--   v:set("value")
--
--   channel.func_name(1,2,3):set("value")
--   value = channel.func_name(1,2,3):get()
--
--   channel["func_name(1,2,3)"]:set("value")
--   channel["func_name(1,2,3)"] = "value"
--   value = channel["func_name(1,2,3)"]:get()
--
-- Note the use of the ':' operator to access the get() and set()
-- methods.
--
-- Also notice the absence of the following constructs from the examples above:
--   channel.func_name(1,2,3) = "value"  -- this will NOT work
--   value = channel.func_name(1,2,3)    -- this will NOT work as expected
--
--
-- Dialplan applications can be accessed through the global 'app' table.
--
--    app.Dial("DAHDI/1")
--    app.dial("DAHDI/1")
--    app["dial"]("DAHDI/1")
--
-- More examples can be found below.
--
-- An autoservice is automatically run while lua code is executing.  The
-- autoservice can be stopped and restarted using the autoservice_stop() and
-- autoservice_start() functions.  The autservice should be running before
-- starting long running operations. The autoservice will automatically be
-- stopped before executing applications and dialplan functions and will be
-- restarted afterwards.  The autoservice_status() function can be used to
-- check the current status of the autoservice and will return true if an
-- autoservice is currently running.
--
-- Note about naming conflicts:
-- Lua allows you to refer to table entries using the '.' notation,
-- I.E. app.goto(something), only if the entry doesn't conflict with an Lua
-- reserved word.  In the 'goto' example, with Lua 5.1 or earlier, 'goto' is
-- not a reserved word so you'd be calling the Asterisk dialplan application
-- 'goto'.  Lua 5.2 however, introduced the 'goto' control statement which
-- makes 'goto' a reserved word.  This causes the interpreter to fail parsing
-- the file and pbx_lua.so will fail to load.  The same applies to any use of
-- Lua tables including extensions, channels and any tables you create.
--
-- There are two ways around this:  Since Lua is case-sensitive, you can use
-- capitalized names, I.E. app.Goto(something) to refer to the Asterisk apps,
-- functions, etc. Or you can use the full syntax, I.E. app["goto"](something).
-- Both syntaxes are backwards compatible with earlier Lua versions.  To make
-- your Lua dialplans easier to maintain and to reduce the chance of future
-- conflicts you may want to use the app["goto"](something) syntax for all
-- table accesses.
--

function outgoing_local(c, e)
	app.dial("DAHDI/1/" .. e, "", "")
end

function demo_instruct()
	app.background("demo-instruct")
	app.waitexten()
end

function demo_congrats()
	app.background("demo-congrats")
	demo_instruct()
end

-- Answer the chanel and play the demo sound files
function demo_start(context, exten)
	app.wait(1)
	app.answer()

	channel.TIMEOUT("digit"):set(5)
	channel.TIMEOUT("response"):set(10)
	-- app.set("TIMEOUT(digit)=5")
	-- app.set("TIMEOUT(response)=10")

	demo_congrats(context, exten)
end

function demo_hangup()
	app.playback("demo-thanks")
	app.hangup()
end

extensions = {
	demo = {
		s = demo_start;

		["2"] = function()
			app.background("demo-moreinfo")
			demo_instruct()
		end;
		["3"] = function ()
			channel.LANGUAGE():set("fr") -- set the language to french
			demo_congrats()
		end;

		["1000"] = function()
--	See the naming conflict note above.
			app['goto']("default", "s", 1)
		end;

		["1234"] = function()
			app.playback("transfer", "skip")
			-- do a dial here
		end;

		["1235"] = function()
			app.voicemail("1234", "u")
		end;

		["1236"] = function()
			app.dial("Console/dsp")
			app.voicemail(1234, "b")
		end;

		["#"] = demo_hangup;
		t = demo_hangup;
                i = function()
                        app.playback("invalid")
                        demo_instruct()
                end;

		["500"] = function()
			app.playback("demo-abouttotry")
			app.dial("IAX2/guest@misery.digium.com/s@default")
			app.playback("demo-nogo")
			demo_instruct()
		end;

		["600"] = function()
			app.playback("demo-echotest")
			app.echo()
			app.playback("demo-echodone")
			demo_instruct()
		end;

		["8500"] = function()
			app.voicemailmain()
			demo_instruct()
		end;

	};

	default = {
		-- by default, do the demo
		include = {"demo"};
	};

	public = {
		-- ATTENTION: If your Asterisk is connected to the internet and you do
		-- not have allowguest=no in sip.conf, everybody out there may use your
		-- public context without authentication.  In that case you want to
		-- double check which services you offer to the world.
		--
		include = {"demo"};
	};

	["local"] = {
		["_NXXXXXX"] = outgoing_local;
	};
}

hints = {
	demo = {
		[1000] = "SIP/1000";
		[1001] = "SIP/1001";
	};

	default = {
		["1234"] = "SIP/1234";
	};
}
