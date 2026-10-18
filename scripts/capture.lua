-- Redux frame grabber, driven by environment variables (see capture-bars.sh / capture-video.sh).
--   CAP_ADDR   guest address of demo_dbg (nm forty-one.elf | grep demo_dbg)
--   CAP_OUT    output directory
--   CAP_BARS   comma list of bar:frac pairs ("9:8,17:100"): grab a PNG source frame when the
--              song reaches that bar and fractional position (0..255) first
--   CAP_VIDEO  if set, append every CAP_EVERY'th rendered frame to this raw file (bgr555le)
--   CAP_QUIT_BAR  quit once the demo reaches this bar
-- demo_dbg: [0]=magic [1]=bar [2]=frac [3]=section [4]=frames [5]=ticks
local ffi = require('ffi')
local addr = tonumber(os.getenv('CAP_ADDR'), 16) % 0x200000
local out = os.getenv('CAP_OUT') or '/tmp/cap'
local want = {}
for item in string.gmatch(os.getenv('CAP_BARS') or '', '[^,]+') do
  local b, f = string.match(item, '^(%d+):?(%d*)$')
  want[#want + 1] = {bar = tonumber(b), frac = tonumber(f) or 0, done = false}
end
local video = os.getenv('CAP_VIDEO')
local every = tonumber(os.getenv('CAP_EVERY') or '2')
local quitBar = tonumber(os.getenv('CAP_QUIT_BAR') or '0')
local vf = video and Support.File.open(video, 'TRUNCATE') or nil
local seen, last, vframes = false, -1, 0
local mem = PCSX.getMemPtr()
local dbg = ffi.cast('uint32_t*', mem + addr)

local function shot(name)
  local ss = PCSX.GPU.takeScreenShot()
  local f = Support.File.open(out .. '/' .. name .. '.raw', 'TRUNCATE')
  f:writeMoveSlice(ss.data); f:close()
  print(string.format('SHOT %s %d %d %s bar=%d frac=%d sec=%d frame=%d ticks=%d', name, ss.width, ss.height,
    tostring(ss.bpp), dbg[1], dbg[2], dbg[3], dbg[4], dbg[5]))
end

-- kept in a global: a collected listener takes Redux down
captureListener = PCSX.Events.createEventListener('GPU::Vsync', function()
  vsyncs = (vsyncs or 0) + 1
  if dbg[0] ~= 0x31474244 then return end
  if not seen then seen = true; print('DBG live') end
  local bar, frac, frames = dbg[1], dbg[2], dbg[4]
  if bar ~= lastBar then lastBar = bar; print(string.format('CLK bar=%d ticks=%d vsyncs=%d wall=%d', bar, dbg[5], vsyncs, os.time())) end
  if vf and bar > 0 then
    vStart = vStart or vsyncs
  end
  if vf and vStart and (vsyncs - vStart) % every == 0 then
    local ss = PCSX.GPU.takeScreenShot()
    if vframes == 0 then print(string.format('VIDEO %d %d %s', ss.width, ss.height, tostring(ss.bpp))) end
    vf:writeMoveSlice(ss.data)
    vframes = vframes + 1
  end
  if frames == last then return end  -- the demo has not rendered a new frame since the last vsync
  last = frames
  for _, w in ipairs(want) do
    if not w.done and (bar > w.bar or (bar == w.bar and frac >= w.frac)) and bar < w.bar + 2 then
      w.done = true
      shot(string.format('bar%03d_f%03d', w.bar, w.frac))
    end
  end
  if quitBar > 0 and bar >= quitBar then
    if vf then vf:close() end
    print('VFRAMES ' .. vframes)
    os.exit(0)
  end
end)
