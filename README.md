lua-resty-json
==============

Json lib for lua and C. The C interface is depicted by `ljson_parser.h`;
while the Lua interface is implemented by `json_decoder.lua`. The lua
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
few real-world json strings. For string-array intensive jsons, our decoder
is normally 30% - 50% ahead of cjson. While for the hash-table intensive
input, we are only 10-30% better. In an extreme example where there is
a super long string, we see 5X speedup. The performance is measured with
luajit 2.1.

So far we pay lots of attention to string handling, and did not get chance to
improve following aspects:
- Parse floating point number quickly. so far we rely on `strtod()`
  to do the dirty job. Unfortunately, the `strtod()` seems to be pretty
  slow.

- Efficiently skip white-spaces between tokens.

- More efficient memory allocation. We are currently using `mempool`
  which allocate a big chunk and the subsequent memory allocation requests
  are served by carving block out of the chunk. It works pretty well
  for small to medium-sized `JSON` input (say under `100k` in size);
  however, the memory allocation overhead is still high (primarily due to
  the cost of allocating big chunks) for big `JSON`s.

Floating Point Number
--------------------
The way we handle following situations may not be what you expect, but
the `JSON` SPEC does not seem to articulate how to handle these situations
right either.

- literal `-0` is interpreted as integer `0`, instead of floating point
  `0.0`.

-  `-0.0` is interperted as floating point `-0.0`.
- If a literal is beyond the range of double-precision, we consider it
  as overflow/underflow; we do not try to represent the literal using
  `long double` or `quadruple`.

- We rely on `strtod()` to parse literal (in strict mode), which, I guess,
  is using default rounding mode or the mode designated by the appliction
  which call the decoder.

- We try to represent literals in signed 64-bit interger whenever possible.
  But the numbers like `1E6` is still represented as floating point as we
  currently rely on `strtod()` for handling scientific notation.

TODO
----
- Continue to improve floating point parsing.
- Improve testing, and add more testing.
- Improve hashtab parsing performance (I almost have not yet got chance
  tune its performance  when I write this comment).
