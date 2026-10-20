-- Builds the CD image with Redux's own ISO builder. Run through
-- scripts/make-disc.sh, which passes the paths in the environment.
local ffi = require('ffi')

local exe = os.getenv('DISC_EXE')
local cnf = os.getenv('DISC_CNF')
local out = os.getenv('DISC_OUT')
local tail = tonumber(os.getenv('DISC_TAIL') or '1024')

local function fail(msg)
    io.stderr:write('make-disc.lua: ' .. msg .. '\n')
    PCSX.quit(1)
    error(msg, 0)
end

local function open(path)
    local f = Support.File.open(path)
    if f:failed() then fail('cannot open ' .. path) end
    return f
end

-- Licence sectors 0-11, 2048 data bytes each. Nothing here comes from a
-- Sony disc: sector 4 carries our own text and sectors 5-11 our own logo,
-- an extruded "41" as a TMD of flat triangles. The BIOS shows that logo at
-- boot. NTSC US BIOSes accept any logo; PAL v4.0E+ and Japanese BIOSes
-- compare it against their own copy and refuse.
local GLYPH = { -- "41", one character per 1x1 cell
    '#..#....#',
    '#..#...##',
    '#..#....#',
    '####....#',
    '...#....#',
    '...#....#',
    '...#...###',
}
local CELL = 150
local DEPTH = 300
local LIFT = 200 -- clears the BIOS's own "PlayStation" caption
local FRONT = { 0x40, 0xff, 0x80 } -- phosphor green
local SIDE = { 0xd0, 0x90, 0x20 } -- amber

local function filled(x, y)
    local row = GLYPH[y + 1]
    return row ~= nil and x >= 0 and row:sub(x + 1, x + 1) == '#'
end

-- Writes the logo TMD at the file's write position, returns its size.
local function writeLogo(file)
    local verts, vidx, prims = {}, {}, {}
    local normals = { { 0, 0, -4096 }, { 0, 0, 4096 }, { -4096, 0, 0 }, { 4096, 0, 0 }, { 0, -4096, 0 }, { 0, 4096, 0 } }
    local w, h = 0, #GLYPH
    for _, row in ipairs(GLYPH) do w = math.max(w, #row) end

    local function v(x, y, z)
        local key = x .. ',' .. y .. ',' .. z
        if vidx[key] == nil then
            vidx[key] = #verts
            verts[#verts + 1] = { (x - w / 2) * CELL, (y - h / 2) * CELL - LIFT, (z - 0.5) * DEPTH }
        end
        return vidx[key]
    end

    local function quad(a, b, c, d, n, col)
        prims[#prims + 1] = { col, n, a, b, c }
        prims[#prims + 1] = { col, n, a, c, d }
    end

    for y = 0, h - 1 do
        for x = 0, w - 1 do
            if filled(x, y) then
                quad(v(x, y, 0), v(x + 1, y, 0), v(x + 1, y + 1, 0), v(x, y + 1, 0), 0, FRONT)
                quad(v(x, y, 1), v(x, y + 1, 1), v(x + 1, y + 1, 1), v(x + 1, y, 1), 1, FRONT)
                if not filled(x - 1, y) then
                    quad(v(x, y, 0), v(x, y + 1, 0), v(x, y + 1, 1), v(x, y, 1), 2, SIDE)
                end
                if not filled(x + 1, y) then
                    quad(v(x + 1, y, 0), v(x + 1, y, 1), v(x + 1, y + 1, 1), v(x + 1, y + 1, 0), 3, SIDE)
                end
                if not filled(x, y - 1) then
                    quad(v(x, y, 0), v(x, y, 1), v(x + 1, y, 1), v(x + 1, y, 0), 4, SIDE)
                end
                if not filled(x, y + 1) then
                    quad(v(x, y + 1, 0), v(x + 1, y + 1, 0), v(x + 1, y + 1, 1), v(x, y + 1, 1), 5, SIDE)
                end
            end
        end
    end

    -- Offsets are relative to the object table (flags = 0).
    local primTop = 28
    local vertTop = primTop + 16 * #prims
    local normTop = vertTop + 8 * #verts
    for _, n in ipairs({ 0x41, 0, 1, vertTop, #verts, normTop, #normals, primTop, #prims }) do file:writeU32(n) end
    file:writeI32(0)
    for _, p in ipairs(prims) do
        -- F3, single-sided, light source calculation on
        for _, b in ipairs({ 4, 3, 0x20, 0x20, p[1][1], p[1][2], p[1][3], 0x20 }) do file:writeU8(b) end
        for i = 2, 5 do file:writeU16(p[i]) end
    end
    for _, list in ipairs({ verts, normals }) do
        for _, p in ipairs(list) do
            for i = 1, 3 do file:writeI16(p[i] < 0 and math.ceil(p[i]) or math.floor(p[i])) end
            file:writeI16(0)
        end
    end
    return 12 + 28 + 16 * #prims + 8 * (#verts + #normals)
end

local function textSector()
    -- The BIOS draws each 32-byte line as two 16-byte halves side by side,
    -- so the title splits at its hyphen. Spacing set by eye in Redux with
    -- a retail US BIOS.
    local s = '     FORTY-     ' .. 'ONE CYCLES      ' .. '   a PlayStation' .. ' demo by Rix    \n'
    local fill = string.rep('0', 62) .. '\n0'
    while #s < 2048 do s = s .. fill end
    return s:sub(1, 2048)
end

local licence = Support.File.buffer()
for _ = 1, 4 do licence:write(string.rep(string.rep('0', 63) .. '\n', 32)) end
licence:write(textSector())
local tmdSize = writeLogo(licence)
if tmdSize > 7 * 2048 then fail('logo TMD is ' .. tmdSize .. ' bytes, more than 7 sectors') end
licence:write(string.rep('\xff', 7 * 2048 - tmdSize))
if licence:size() ~= 12 * 2048 then fail('licence is ' .. licence:size() .. ' bytes') end

local outFile = Support.File.open(out, 'TRUNCATE')
if outFile:failed() then fail('cannot create ' .. out) end
local builder = PCSX.isoBuilder(outFile)

-- Zero system area; the licence sectors go in after close(), which
-- lays the filesystem out from sector 16 and leaves 0-15 alone.
-- writeLicense() would only take a Sony-format licence.
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

-- The builder fills in subheader, EDC and ECC.
local sector = ffi.new('uint8_t[2048]')
for i = 0, 11 do
    licence:readAt(sector, 2048, i * 2048)
    builder:writeSectorAt(sector, 2048, i, 'M2_FORM1')
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
