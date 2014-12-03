package.cpath = package.cpath..";../?.so"
package.path = package.cpath..";../?.lua"

local ljson_decoder = require 'json_decoder'
local decoder = ljson_decoder.new()

local function cmp_lua_var(obj1, obj2)
    if type(obj1) ~= type(obj2) then
        return
    end

    if type(obj1) == "string" or
       type(obj1) == "number" or
       type(obj1) == "nil" or
       type(obj1) == "boolean" then
        return obj1 == obj2 and true or nil
    end

    if (type(obj1) ~= "table") then
        print("unknown type", type(obj1));
        return
    end

    -- compare table
    for k, v in pairs(obj1) do
        if not cmp_lua_var(v, obj2[k]) then
            -- print(v," vs ", obj2[k])
            return
        end
    end

    for k, v in pairs(obj2) do
        if not cmp_lua_var(v, obj1[k]) then
            -- print(v," vs ", obj1[k])
            return
        end
    end

    return true
end

local test_fail_num = 0;
local test_total = 0;

local function ljson_test(test_id, parser, input, expect)
    test_total = test_total + 1
    io.write(string.format("Testing %s ...", test_id))
    local result = decoder:decode(input)
    if cmp_lua_var(result, expect) then
        print("succ!")
    else
        test_fail_num = test_fail_num + 1
        print("failed!")
        --ljson_decoder.debug(result)
    end
end

local json_parser = ljson_decoder.create()

-- Test 1
local input = [=[[1, 2, 3, {"key1":"value1", "key2":"value2"}, "lol"]]=]
local output = {1, 2, 3, {["key1"] = "value1", ["key2"] = "value2" }, "lol"}
ljson_test("test1", json_parser, input, output);

-- Test 2
input = [=[[]]=]
output = {}
ljson_test("test2", json_parser, input, output);

-- Test 3
input = [=[[{}]]=]
output = {{}}
ljson_test("test3", json_parser, input, output);

input = [=[[null]]=]
output = {nil}
ljson_test("test4", json_parser, input, output);

input = [=[[true, false]]=]
output = {true, false}
ljson_test("test5", json_parser, input, output);

io.write(string.format(
        "\n============================\nTotal test count %d, fail %d\n",
        test_total, test_fail_num))

--TODO: How to let luajit exit with non-zero if not all testing case pass.
