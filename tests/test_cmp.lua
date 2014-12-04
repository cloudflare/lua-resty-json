package.cpath = package.cpath .. ";../?.so"
package.path = package.path .. ";../?.lua"

local cjson = require "cjson"
local ljson_decoder = require 'json_decoder'

local f, err = io.open("test_cmp.json", "r")

local function cmp_lua_var(obj1, obj2)
    if type(obj1) ~= type(obj2) then
        return
    end

    if type(obj1) == "string" or
       type(obj1) == "number" or
       type(obj1) == "nil" or
       type(obj1) == "boolean" then
        if obj1 == obj2 then
            return true
        end

        print(obj1, "of tyep", type(obj1), "vs", obj2, "of type", obj2)
        return
    end

    if (type(obj1) ~= "table") then
        print("unknown type", type(obj1));
        return
    end

    -- compare table
    for k, v in pairs(obj1) do
        if not cmp_lua_var(v, obj2[k]) then
            print("key =", k, "value:", v," vs ", obj2[k])
            return
        end
    end

    for k, v in pairs(obj2) do
        if not cmp_lua_var(v, obj1[k]) then
            print("key =", k, "value:", v," vs ", obj1[k])
            return
        end
    end

    return true
end

local instance, err = ljson_decoder.new()
if not instance then
   print("fail to create decoder instance")
end

local linenum = 0
local fail_num = 0;
for line in f:lines() do
    local result1 = instance:decode(line)
    local result2 = cjson.decode(line)

    linenum = linenum + 1

    if not cmp_lua_var(result1, result2) then
        print("Fail with JSON at line", linenum)
        fail_num = fail_num + 1
    end
end

if fail_num == 0 then
    print("pass!")
    os.exit(0)
else
    print("Fail!")
    os.exit(1)
end
