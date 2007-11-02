#!/usr/bin/env lua
--[[

This utility can be used to generate an extensions.conf file to match an
existing extensions.lua file.  As an argument it takes the patch of the
extensions.lua file to read from, otherwise it uses
/etc/asterisk/extensions.lua.

This script can also be used to automatically include extensions.lua in
extensions.conf via a #exec as well.

#exec /usr/bin/build-extensions-conf.lua -c

--]]

usage = [[

Usage:
  ]] .. arg[0] .. [[ [options] [extensions.lua path]

This utility can generate an extensions.conf file with all of the contexts in
your extensions.lua file defined as including the Lua switch.  This is useful
if you want to use your extensions.lua file exclusively.  By using this utility
you dont't have to create each extension in extensions.conf manually.

The resulting extensions.conf file is printed to standard output.

  --contexts-only, -c	Don't print the [global] or [general] sections.  This
			is useful for including the generated file into an
			existing extensions.conf via #include or #exec.

  --help, -h		Print this message.

]]

extensions_file = "/etc/asterisk/extensions.lua"

options = {}

for k, v in ipairs(arg) do
	if v:sub(1, 1) == "-" then
		if v == "-h" or v == "--help" then
			print("match")
			options["help"] = true
		elseif v == "-c" or v == "--contexts-only" then
			options["contexts-only"] = true
		end
	else
		options["extensions-file"] = v
	end
end

if options["help"] then
	io.stderr:write(usage)
	os.exit(0)
end

if options["extensions-file"] then
	extensions_file = options["extensions-file"]
end

result, error_message = pcall(dofile, extensions_file)

if not result then
	io.stderr:write(error_message .. "\n")
	os.exit(1)
end

if not extensions then
	io.stderr:write("Error: extensions table not found in '" .. extensions_file .. "'\n")
	os.exit(1)
end

if not options["contexts-only"] then
	io.stdout:write("[general]\n\n[globals]\n\n")
end

for context, extens in pairs(extensions) do
	io.stdout:write("[" .. tostring(context) ..  "]\nswitch => Lua\n\n")
end

