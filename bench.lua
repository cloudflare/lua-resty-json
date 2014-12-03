local cjson = require "cjson"
local ljson_decoder = require 'json_decoder'
local f, err = io.open("bench.json", "r")

local iter = 100000
--local iter = 3
local instance = ljson_decoder.new()

for line in f:lines() do
    local begin = os.clock()
    for i = 1, iter do
        local result, err = instance:decode(line)
    end

    local t1 = os.clock() - begin
    begin = os.clock()
    for i = 1, iter do
        local t = cjson.decode(line)
    end
    local t2 = os.clock() - begin

    print(t1, t2, (t2-t1)/t2 * 100)
end
