#!/usr/bin/lua
-- Generate awful.doc documentation string from Lua code.
-- © 2009 Julien Danjou <julien@danjou.info>

require("luadoc")

luadoc.logger = luadoc.util.loadlogengine({})

taglet = require("luadoc.taglet.standard")
taglet.logger = luadoc.logger
taglet.options = { nolocals = true }
doc = taglet.start({ arg[1] })

function table.val_to_str ( v )
  if "string" == type( v ) then
    v = string.gsub( v, "\n", "\\n" )
    if string.match( string.gsub(v,"[^'\"]",""), '^"+$' ) then
      return "'" .. v .. "'"
    end
    return '"' .. string.gsub(v:gsub("\\", "\\\\"),'"', '\\"' ) .. '"'
  else
    return "table" == type( v ) and table.tostring( v ) or
      tostring( v )
  end
end

function table.key_to_str ( k )
  if "string" == type( k ) and string.match( k, "^[_%a][_%a%d]*$" ) then
    return k
  else
    return "[" .. table.val_to_str( k ) .. "]"
  end
end

function table.tostring( tbl )
  local result, done = {}, {}
  for k, v in ipairs( tbl ) do
    table.insert( result, table.val_to_str( v ) )
    done[ k ] = true
  end
  for k, v in pairs( tbl ) do
    if not done[ k ] then
      table.insert( result,
        table.key_to_str( k ) .. "=" .. table.val_to_str( v ) )
    end
  end
  return "{" .. table.concat( result, "," ) .. "}"
end

function dump_func_or_table(module_name, tbl)
    for name, doc in pairs(tbl) do
        if type(name) == "string" then
            -- 'capi' is not a real module, does not set it
            local modprefix = ""
            if module_name ~= "capi" then modprefix = module_name .. "." end
            print("set(" .. modprefix .. name .. ", " .. table.tostring(doc) .. ")")
        end
    end
end

print("-- Auto generated file at build time.")
print("-- It would be stupid to modify it.\n")
print("require(\"awful.doc\")")
print("local set = doc.set")
print("--- Self-documentation module for awesome libs")
print("module(\"awful.doc.awesome\")");

-- Build module documentation
for module_name, module_doc in pairs(doc.modules) do
    if type(module_name) == "string" then
        print("set(" .. module_name .. ", " .. table.tostring(module_doc) .. ")")
        dump_func_or_table(module_name, module_doc.functions)
        dump_func_or_table(module_name, module_doc.tables)
    end
end
