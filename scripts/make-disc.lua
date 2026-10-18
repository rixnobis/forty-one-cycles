-- Builds the CD image with Redux's own ISO builder. Run through
-- scripts/make-disc.sh, which passes the paths in the environment.
local ffi = require('ffi')

local exe = os.getenv('DISC_EXE')
local cnf = os.getenv('DISC_CNF')
local licence = os.getenv('DISC_LICENCE')
local out = os.getenv('DISC_OUT')
local tail = tonumber(os.getenv('DISC_TAIL') or '1024')

local function fail(msg)
    io.stderr:write('make-disc.lua: ' .. msg .. '\n')
    PCSX.quit(1)
end

local function open(path)
    local f = Support.File.open(path)
    if f:failed() then fail('cannot open ' .. path) end
    return f
end

local outFile = Support.File.open(out, 'TRUNCATE')
if outFile:failed() then fail('cannot create ' .. out) end
local builder = PCSX.isoBuilder(outFile)

-- Zero system area; the licence sectors go in after close(), which
-- lays the filesystem out from sector 16 and leaves 0-15 alone.
builder:writeLicense()
builder:setSystemIdent('PLAYSTATION')
builder:setApplicationIdent('PLAYSTATION')
builder:setVolumeIdent('FORTY_ONE_CYCLES')
builder:setVolSetIdent('FORTY_ONE_CYCLES')
builder:setPublisherIdent('RIX')
builder:setDataPreparerIdent('PCSX-REDUX')

-- XA attributes: 0x8d55 directory, 0x0d55 file.
local root = builder:createRoot(1)
root:setXA(true)
root:setXAAttribs(0x8d55)
-- The builder lays files out and lists them in insertion order, so they
-- go in sorted, as ISO 9660 wants directory records.
local files = {}
for _, f in ipairs({ { 'PSX.EXE', exe }, { 'SYSTEM.CNF', cnf } }) do
    local node = builder:createFile(root, f[1], open(f[2]))
    node:setXA(true)
    node:setXAAttribs(0x0d55)
    node:setSectorMode('M2_FORM1')
    files[#files + 1] = node
end
builder:close()
if builder:failed() then fail('builder failed') end

-- Licence: 12 sectors of 2336 bytes (8-byte subheader + 2048 data + 280),
-- as written by tools/gen_licence.py. Only the 2048 data bytes are kept;
-- the builder regenerates subheader, EDC and ECC.
local f = io.open(licence, 'rb')
if not f then fail('cannot open ' .. licence) end
local lic = f:read('*a')
f:close()
if #lic % 2336 ~= 0 or #lic > 16 * 2336 then fail('licence file is ' .. #lic .. ' bytes') end
for i = 0, #lic / 2336 - 1 do
    builder:writeSectorAt(lic:sub(i * 2336 + 9, i * 2336 + 8 + 2048), i, 'M2_FORM1')
end

-- Padding past the last file: the drive overshoots seeks near the end of
-- the written area and needs somewhere harmless to land.
local last = 0
for _, node in ipairs(files) do
    local e = node:getLBA() + math.ceil(node:getSize() / 2048)
    if e > last then last = e end
end
local zero = ffi.new('uint8_t[2048]')
for lba = last, last + tail - 1 do
    builder:writeSectorAt(zero, 2048, lba, 'M2_FORM1')
end
if builder:failed() then fail('sector writes failed') end
outFile:close()
PCSX.quit(0)
