lua-resty-json
==============

Json lib for lua and C. The C interface is depicted by ljson_parser.h;
while the Lua interface is implemented by json_decoder.lua. The lua
interface is built on top of C implementation, and it's implemented
using FFI instead of Lua C-API.

Following is an example of Lua usage:
```lua
local ljson_decoder = require 'json_decoder'
local instance = ljson_decoder.create()
local result, err = ljson_decoder.parse(instance, line)
```
Performance
-----------

As of I write this README.md, I compare this work against cjson using
few real-world json strings. For string-array intensive jsons. Our decoder
is normally 30% - 50% ahead of cjson. While for the hash-table intensive
input, we are only 10-30% better. The performance is measured with luajit
2.1.

TODO
----
  o. Improve testing, and add more testing.
  o. Improve hashtab parsing performance (I almost have not yet got chance
     tune its performance  when I write this comment).
  o. Unicode support.
