-- SPDX-License-Identifier: GPL-3.0-or-later
-- HIRAKU MAME oracle: run a Firebeat machine from a deterministic input script
-- and record what the game does to the hardware, so the recompiled runtime
-- can be diffed against it.
--
-- Loaded by oracle/mame.sh via -autoboot_script. Configuration comes from the
-- environment, which mame.sh forwards into the flatpak:
--   HIRAKU_SCRIPT  input script (.his, see oracle/README.md)
--   HIRAKU_OUT     output directory (must exist)
--   HIRAKU_TAPS    comma list of MMIO groups to log (default: all)
--
-- Outputs in HIRAKU_OUT:
--   mmio.bin    one 32-byte record per tapped access (format in oracle/trace.py)
--   frames.txt  one line per frame: frame, emulated time, watches, hashes
--   dump-*.bin  raw RAM regions requested by `dump`
--   log.txt     what the script did, and when
--   snaps       PNG snapshots go to MAME's snapshot dir (mame.sh points it at HIRAKU_OUT)

-- MAME runs the autoboot script again after every reset, the watchdog's soft reset too (a game
-- rebooting itself, as Twinkle's after test mode): the first run's notifiers carry on through it
-- (install_taps on reset), so a second run does nothing, or it would start the script over
if _G.hiraku_subs then return end

local mach   = manager.machine
local cpu    = mach.devices[":maincpu"]
local space  = cpu.spaces["program"]
local OUT    = assert(os.getenv("HIRAKU_OUT"), "HIRAKU_OUT not set")
local SCRIPT = assert(os.getenv("HIRAKU_SCRIPT"), "HIRAKU_SCRIPT not set")
local log    = assert(io.open(OUT .. "/log.txt", "w"))
local log_open = true
local finished = false -- set by `quit` or an error; later callbacks do nothing
local function say(fmt, ...)
    local s = string.format(fmt, ...)
    if log_open then log:write(s, "\n"); log:flush() end
    print("hiraku: " .. s)
end

-- --------------------------------------------------------------------------
-- MMIO groups (main CPU view, MAME firebeat.cpp maps). Group ids are stable:
-- they are written into every trace record.
-- --------------------------------------------------------------------------
local GROUPS = {
    { id = 1, name = "gcu",   lo = 0x7e800000, hi = 0x7e8001ff },  -- K057714 x2
    { id = 2, name = "dpram", lo = 0x74000000, hi = 0x740003ff },  -- SPU mailbox
    { id = 3, name = "ata",   lo = 0x7fe00000, hi = 0x7fe8000f },  -- main ATA CS0/CS1
    { id = 4, name = "io",    lo = 0x7d000000, hi = 0x7d0008ff },  -- cabinet, lamps, ymz, control, inputs
    { id = 5, name = "uart",  lo = 0x7dc00000, hi = 0x7dc0000f },  -- PC16552 com
    { id = 6, name = "rtc",   lo = 0x7e000000, hi = 0x7e00013f },  -- RTC65271 + XRAM
    { id = 7, name = "ext",   lo = 0x70000000, hi = 0x7000ffff },  -- extend board: MIDI UART, FDC, FFT, IRQ latch
    { id = 8, name = "flash", lo = 0x7d400000, hi = 0x7dbfffff },  -- 3x 29F016A
}
-- System 573 (MAME ksys573.cpp and the PS1 CPU's internal map). The PS1 reaches its
-- I/O through three mirrors (KUSEG, KSEG0, KSEG1): each group is tapped in all of
-- them and recorded at its KUSEG address.
local MIRRORS = { 0 }
local AMASK = 0xffffffff
-- Twinkle (MAME twinkle.cpp): the same PS1 CPU as the 573, told apart by its SCSI controller
local TWINKLE = mach.devices[":ncr53cf96"] ~= nil
if TWINKLE then
    GROUPS = {
        { id = 21, name = "dpram", lo = 0x1f000000, hi = 0x1f0007ff },  -- CY7C131 mailbox to the SPU board
        { id = 22, name = "scsi",  lo = 0x1f200000, hi = 0x1f20ffff },  -- NCR 53CF96 (the program CD)
        { id = 23, name = "superio", lo = 0x1f210000, hi = 0x1f2107ff }, -- FDC37C665GT: UARTs (DVD player), FDC, LPT
        { id = 24, name = "io",    lo = 0x1f218000, hi = 0x1f28ffff },  -- watchdog, I/O mux, LEDs, keys, dongle
        { id = 25, name = "rtc",   lo = 0x1f290000, hi = 0x1f2a007f },  -- RTC65271 + XRAM (the backup RAM)
        { id = 26, name = "mixer", lo = 0x1f2b0000, hi = 0x1f2b00ff },  -- sub board: Bt812 and overlay
        { id = 15, name = "irq",   lo = 0x1f801070, hi = 0x1f80112f },  -- PS1 IRQ, DMA, root counters
        { id = 16, name = "gpu",   lo = 0x1f801810, hi = 0x1f801817 },
        { id = 17, name = "spu",   lo = 0x1f801c00, hi = 0x1f801fff },
        { id = 18, name = "sio",   lo = 0x1f801040, hi = 0x1f80105f },  -- SIO0/1 (1st style's DVD player is on SIO1)
    }
    MIRRORS = { 0x00000000, 0x80000000, 0xa0000000 }
    AMASK = 0x1fffffff
elseif cpu.shortname == "cxd8530cq" then
    GROUPS = {
        { id = 11, name = "flash", lo = 0x1f000000, hi = 0x1f3fffff },  -- onboard flash, PCMCIA (banked)
        { id = 12, name = "in",    lo = 0x1f400000, hi = 0x1f40000f },  -- IN0-IN3, OUT0
        { id = 13, name = "ata",   lo = 0x1f480000, hi = 0x1f4c000f },  -- ATA CS0/CS1 (the CD drive)
        { id = 14, name = "sys",   lo = 0x1f500000, hi = 0x1f6fffff },  -- control, ATAPI reset, lamps, M48T58, JVS, security, I/O board
        { id = 15, name = "irq",   lo = 0x1f801070, hi = 0x1f80112f },  -- PS1 IRQ, DMA, root counters
        { id = 16, name = "gpu",   lo = 0x1f801810, hi = 0x1f801817 },
        { id = 17, name = "spu",   lo = 0x1f801c00, hi = 0x1f801fff },
        { id = 18, name = "sio",   lo = 0x1f801040, hi = 0x1f80105f },  -- SIO0/1 (the cassette is on SIO1)
    }
    MIRRORS = { 0x00000000, 0x80000000, 0xa0000000 }
    AMASK = 0x1fffffff
elseif mach.devices[":k055555"] ~= nil or cpu.shortname == "m68ec020" then
    GROUPS = {}   -- DJ Main: no MMIO groups yet (the Firebeat ones are outside its 24-bit bus)
end

-- Twinkle's SPU board: the 68000's view (MAME twinkle.cpp sound_map). Wave RAM itself
-- (0x800000-) is left out: the DMA and the 68000 fill it word by word.
local AUDIO_GROUPS = {}
local audio = nil
if TWINKLE then
    audio = mach.devices[":audiocpu"]
    AUDIO_GROUPS = {
        { id = 31, name = "a_ctrl",  lo = 0x200000, hi = 0x26ffff },  -- DIP switches, LEDs, IRQ control, DMA address, wave bank
        { id = 32, name = "a_dpram", lo = 0x280000, hi = 0x2807ff },  -- the mailbox, 68000 side
        { id = 33, name = "a_ata",   lo = 0x300000, hi = 0x34000f },  -- the hard disk
        { id = 34, name = "a_rf",    lo = 0x400000, hi = 0x400fff },  -- RF5C400
    }
end

-- --------------------------------------------------------------------------
-- State shared by the taps and the frame loop
-- --------------------------------------------------------------------------
local frame = 0
local now = 0.0               -- emulated seconds, refreshed per frame and per access
local mmio_buf, mmio_n, mmio_total = {}, 0, 0
local mmio = assert(io.open(OUT .. "/mmio.bin", "wb"))
local pc_state = cpu.state["PC"] or cpu.state["pc"] or cpu.state["GENPC"]
local audio_pc = audio and (audio.state["PC"] or audio.state["GENPC"])
local taps = {}               -- passthrough handlers must stay referenced

-- Folding. Consecutive accesses with the same (kind, group, addr, mask) become
-- one record with a count. If their data differed (sector reads, VRAM
-- uploads), the record's data is the FNV-1a hash of the whole data sequence
-- and kind gets bit 1 set. The key deliberately leaves out the PC, so the
-- runtime can fold its own stream identically whether or not PCs are exact.
-- HIRAKU_FOLD=0 writes every access as-is.
local FOLD = (os.getenv("HIRAKU_FOLD") or "1") ~= "0"
-- HIRAKU_NOFOLD=gcu,dpram: groups recorded access by access (e.g. for GCU replay)
local NOFOLD = {}
for g in string.gmatch(os.getenv("HIRAKU_NOFOLD") or "", "[^,]+") do NOFOLD[g] = true end
local NOFOLD_ID = {}
local p_kind, p_group, p_addr, p_mask, p_data, p_hash, p_n, p_frame, p_time, p_pc, p_varied

local function emit()
    if p_n == nil then return end
    mmio_n = mmio_n + 1
    -- frame u32, time f64, pc u32, addr u32, data u32, mask u32, kind u8, group u8, count u16
    mmio_buf[mmio_n] = string.pack("<I4dI4I4I4I4BBI2", p_frame, p_time, p_pc, p_addr,
        p_varied and p_hash or p_data, p_mask, p_kind | (p_varied and 2 or 0), p_group, p_n)
    p_n = nil
    if mmio_n >= 4096 then
        mmio:write(table.concat(mmio_buf, "", 1, mmio_n))
        mmio_total = mmio_total + mmio_n
        mmio_buf, mmio_n = {}, 0
    end
end

local function fnv32(h, v)
    for sh = 0, 24, 8 do h = ((h ~ ((v >> sh) & 0xff)) * 0x01000193) & 0xffffffff end
    return h
end

local function record(kind, group, offset, data, mask, pcs)
    offset, data, mask = offset & 0xffffffff, data & 0xffffffff, mask & 0xffffffff
    if FOLD and not NOFOLD_ID[group] and p_n and p_n < 0xffff and kind == p_kind and offset == p_addr
       and mask == p_mask and group == p_group then
        p_n = p_n + 1
        p_hash = fnv32(p_hash, data)
        if data ~= p_data then p_varied = true end
        return
    end
    emit()
    p_kind, p_group, p_addr, p_mask, p_data, p_n = kind, group, offset, mask, data, 1
    p_hash, p_varied = fnv32(0x811c9dc5, data), false
    p_frame, p_time, p_pc = frame, mach.time:as_double(), (pcs or pc_state).value & 0xffffffff
end

local function flush_mmio()
    emit()
    if mmio_n > 0 then
        mmio:write(table.concat(mmio_buf, "", 1, mmio_n))
        mmio_total = mmio_total + mmio_n
        mmio_buf, mmio_n = {}, 0
    end
    mmio:flush()
end

local want = {}
for g in string.gmatch(os.getenv("HIRAKU_TAPS") or "all", "[^,]+") do want[g] = true end
for _, g in ipairs(GROUPS) do if NOFOLD[g.name] then NOFOLD_ID[g.id] = true end end
for _, g in ipairs(AUDIO_GROUPS) do if NOFOLD[g.name] then NOFOLD_ID[g.id] = true end end

-- System 573: the M48T58's clock as MAME set it from the host at power-on, so a
-- HIRAKU run can start from the same time (--rtc). Registers 0x1ff9-0x1fff (BCD
-- seconds .. year) on the 16-bit lanes of 0x1f620000 (umask 0x00ff00ff).
local function log_rtc()
    local function tk(o) return space:read_u8(0x1f620000 + (o >> 1) * 4 + (o & 1) * 2) end
    local function bcd(v) return (v >> 4) * 10 + (v & 15) end
    local y, mo, d = bcd(tk(0x1fff)), bcd(tk(0x1ffe) & 0x1f), bcd(tk(0x1ffd) & 0x3f)
    local h, mi, se = bcd(tk(0x1ffb) & 0x3f), bcd(tk(0x1ffa) & 0x7f), bcd(tk(0x1ff9) & 0x7f)
    local t = os.time({ year = (y < 70 and 2000 or 1900) + y, month = mo, day = d, hour = h, min = mi, sec = se })
    say("rtc %04d-%02d-%02d %02d:%02d:%02d at 1 s: power-on time %d (--rtc)", (y < 70 and 2000 or 1900) + y, mo, d, h, mi, se, t - 1)
end

-- Twinkle: the RTC65271's clock as MAME set it from the host at power-on (--rtc). Its
-- registers sit behind an index register, so they are read from MAME's save items
-- rather than through the bus, which would change the index the game left there.
local function save_item(dev, name)
    for k, idx in pairs(dev.items) do
        if k == name or k:match("/" .. name .. "$") then return emu.item(idx) end
    end
    error("no save item " .. name .. " in " .. dev.tag)
end
local function log_rtc_twinkle()
    local regs = save_item(mach.devices[":rtc"], "m_regs")
    local function r(i) return regs:read(i) end
    local bin = (r(0x0b) & 0x04) ~= 0   -- register B, DM: binary rather than BCD
    local function v(x) return bin and x or ((x >> 4) * 10 + (x & 15)) end
    local h = r(4)
    local pm = (h & 0x80) ~= 0
    h = v(h & 0x7f)
    if (r(0x0b) & 0x02) == 0 and pm then h = h + 12 end   -- 12-hour mode, as MAME stores it
    local y, mo, d, mi, se = v(r(9)), v(r(8)), v(r(7)), v(r(2)), v(r(0) & 0x7f)
    local yr = (y < 70 and 2000 or 1900) + y
    local t = os.time({ year = yr, month = mo, day = d, hour = h, min = mi, sec = se })
    say("rtc %04d-%02d-%02d %02d:%02d:%02d at 1 s (reg B %02x): power-on time %d (--rtc)",
        yr, mo, d, h, mi, se, r(0x0b), t - 1)
end

-- Taps must be (re)installed once the machine has reset: a map rebuild drops them.
local function install_taps()
    if finished then return end
    for _, t in ipairs(taps) do t:remove() end
    taps = {}
    for _, g in ipairs(GROUPS) do
        if want.all or want[g.name] then
            local id = g.id
            for _, m in ipairs(MIRRORS) do
                taps[#taps + 1] = space:install_read_tap(g.lo | m, g.hi | m, "hiraku_r_" .. g.name .. m,
                    function(offset, data, mask) record(0, id, offset & AMASK, data, mask) end)
                taps[#taps + 1] = space:install_write_tap(g.lo | m, g.hi | m, "hiraku_w_" .. g.name .. m,
                    function(offset, data, mask) record(1, id, offset & AMASK, data, mask) end)
            end
        end
    end
    if audio then
        local aspace = audio.spaces["program"]
        for _, g in ipairs(AUDIO_GROUPS) do
            if want.all or want[g.name] then
                local id = g.id
                taps[#taps + 1] = aspace:install_read_tap(g.lo, g.hi, "hiraku_r_" .. g.name,
                    function(offset, data, mask) record(0, id, offset, data, mask, audio_pc) end)
                taps[#taps + 1] = aspace:install_write_tap(g.lo, g.hi, "hiraku_w_" .. g.name,
                    function(offset, data, mask) record(1, id, offset, data, mask, audio_pc) end)
            end
        end
    end
    say("f%d taps installed (%d handlers)", frame, #taps)
end

-- --------------------------------------------------------------------------
-- Input script (.his)
-- --------------------------------------------------------------------------
local fields       -- "PORT/Field name" and "Field name" -> ioport field
local function find_field(name)
    if fields == nil then
        fields = {}
        for tag, port in pairs(mach.ioport.ports) do
            local short = tag:gsub("^:", "")
            for fname, f in pairs(port.fields) do
                fields[short .. "/" .. fname] = f
                if fields[fname] == nil then fields[fname] = f else fields[fname] = false end
            end
        end
        -- Fields of one name on one port (Dance Maniax's "D-Sensor U L" for both players) hide each
        -- other in port.fields; each, found by its bit, is also "P<player> <name>"
        for tag, port in pairs(mach.ioport.ports) do
            for b = 0, 31 do
                local f = port:field(1 << b)
                if f ~= nil and f.mask == (1 << b) and not f.name:match("^P%d") then
                    local alias = "P" .. (f.player + 1) .. " " .. f.name
                    if fields[alias] == nil then fields[alias] = f end
                end
            end
        end
    end
    local f = fields[name]
    if f == false then error("field '" .. name .. "' is ambiguous; use PORT/" .. name) end
    if f == nil then
        local port, near = name:match("^([^/]+)/"), {}
        for k in pairs(fields) do
            if port and k:sub(1, #port + 1) == port .. "/" then near[#near + 1] = "'" .. k .. "'" end
        end
        error("no input field '" .. name .. "'" .. (#near > 0 and "; " .. port .. " has " .. table.concat(near, ", ") or ""))
    end
    return f
end

local function tokens(line)
    local t, i = {}, 1
    while i <= #line do
        local c = line:sub(i, i)
        if c == "#" then break
        elseif c:match("%s") then i = i + 1
        elseif c == '"' then
            local j = line:find('"', i + 1, true) or error("unterminated quote: " .. line)
            t[#t + 1] = line:sub(i + 1, j - 1); i = j + 1
        else
            local j = line:find("[%s#]", i) or (#line + 1)
            t[#t + 1] = line:sub(i, j - 1); i = j
        end
    end
    return t
end

local function num(s) return math.tointeger(tonumber(s)) or error("not a number: " .. tostring(s)) end

local events = {}      -- frame -> list of actions
local repeats = {}     -- { every, from, action }
local watches = {}     -- { name, width, addr }
local hashes = {}      -- { name, lo, hi }

local function add(f, act) events[f] = events[f] or {}; table.insert(events[f], act) end

local function parse_action(t, k)
    local verb = t[k]
    if verb == "press" then return { "press", t[k + 1], num(t[k + 2] or "8") }
    elseif verb == "hold" then return { "set", t[k + 1], 1 }
    elseif verb == "release" then return { "set", t[k + 1], 0 }
    elseif verb == "set" then return { "set", t[k + 1], num(t[k + 2]) }
    elseif verb == "snap" then return { "snap" }
    elseif verb == "dump" then return { "dump", t[k + 1], num(t[k + 2]), num(t[k + 3]) }
    elseif verb == "poke" then
        local b = {}
        for j = k + 2, #t do b[#b + 1] = num(t[j]) end
        if #b == 0 then error("poke: no bytes") end
        return { "poke", num(t[k + 1]), b }
    elseif verb == "mark" then return { "mark", table.concat(t, " ", k + 1) }
    elseif verb == "itrace" then
        assert(t[k + 1] == "on" or t[k + 1] == "off", "itrace on|off")
        return { "itrace", t[k + 1] }
    elseif verb == "quit" then return { "quit" }
    end
    error("unknown action '" .. tostring(verb) .. "'")
end

do
    local n = 0
    for line in io.lines(SCRIPT) do
        n = n + 1
        local t = tokens(line)
        if #t > 0 then
            local ok, err = pcall(function()
                if t[1] == "game" then
                    -- informational; mame.sh picks the machine
                elseif t[1] == "watch" then
                    local w = ({ u8 = 1, u16 = 2, u32 = 4 })[t[3]] or error("watch width must be u8/u16/u32")
                    watches[#watches + 1] = { t[2], w, num(t[4]) }
                elseif t[1] == "hash" then
                    hashes[#hashes + 1] = { t[2], num(t[3]), num(t[4]) }
                elseif t[1] == "at" then
                    add(num(t[2]), parse_action(t, 3))
                elseif t[1] == "every" then
                    local a = parse_action(t, 3)
                    repeats[#repeats + 1] = { num(t[2]), 0, a }
                elseif t[1] == "from" then
                    -- from <f> every <n> <action>
                    assert(t[3] == "every", "expected: from <frame> every <n> <action>")
                    repeats[#repeats + 1] = { num(t[4]), num(t[2]), parse_action(t, 5) }
                else
                    error("unknown directive '" .. t[1] .. "'")
                end
            end)
            if not ok then error(string.format("%s:%d: %s", SCRIPT, n, err)) end
        end
    end
    say("script %s: %d watches, %d hashes", SCRIPT, #watches, #hashes)
end

-- --------------------------------------------------------------------------
-- Frame loop
-- --------------------------------------------------------------------------
local frames = assert(io.open(OUT .. "/frames.txt", "w"))
frames:write("# frame time")
for _, w in ipairs(watches) do frames:write(" ", w[1]) end
for _, h in ipairs(hashes) do frames:write(" #", h[1]) end
frames:write("\n")

local held = {}        -- field -> release frame

local function fnv(lo, hi)
    -- FNV-1a over 32-bit words of a RAM range (inclusive lo, exclusive hi), 16 words an unpack:
    -- the per-word unpack made a 4 MiB hash every frame ~96 % of a System 573 run
    local s = space:read_range(lo & 0x1fffffff, (hi - 1) & 0x1fffffff, 8)
    local h = 0x811c9dc5
    local n, i = #s, 1
    local P = 0x01000193
    while i + 63 <= n do
        local a, b, c, d, e, f, g, k, l, m, o, p, q, r, t, u = string.unpack(">I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4", s, i)
        h = ((h ~ a) * P) & 0xffffffff; h = ((h ~ b) * P) & 0xffffffff; h = ((h ~ c) * P) & 0xffffffff
        h = ((h ~ d) * P) & 0xffffffff; h = ((h ~ e) * P) & 0xffffffff; h = ((h ~ f) * P) & 0xffffffff
        h = ((h ~ g) * P) & 0xffffffff; h = ((h ~ k) * P) & 0xffffffff; h = ((h ~ l) * P) & 0xffffffff
        h = ((h ~ m) * P) & 0xffffffff; h = ((h ~ o) * P) & 0xffffffff; h = ((h ~ p) * P) & 0xffffffff
        h = ((h ~ q) * P) & 0xffffffff; h = ((h ~ r) * P) & 0xffffffff; h = ((h ~ t) * P) & 0xffffffff
        h = ((h ~ u) * P) & 0xffffffff
        i = i + 64
    end
    for j = i, n - 3, 4 do
        h = ((h ~ string.unpack(">I4", s, j)) * P) & 0xffffffff
    end
    return h
end

-- HIRAKU_HASH_EVERY=N: hash on every Nth frame only ("-" on the others; snapcmp.py skips them),
-- and on every frame from HIRAKU_HASH_FROM to HIRAKU_HASH_TO (a window to pin a difference down)
local HASH_EVERY = tonumber(os.getenv("HIRAKU_HASH_EVERY") or "1")
local HASH_FROM, HASH_TO = tonumber(os.getenv("HIRAKU_HASH_FROM") or "-1"), tonumber(os.getenv("HIRAKU_HASH_TO") or "-1")
local function hash_now(f)
    return f % HASH_EVERY == 0 or (f >= HASH_FROM and f < HASH_TO)
end

local function run(a)
    local v = a[1]
    if v == "press" then
        local f = find_field(a[2]); f:set_value(1); held[f] = frame + a[3]
        say("f%d press %s %d", frame, a[2], a[3])
    elseif v == "set" then
        find_field(a[2]):set_value(a[3]); say("f%d set %s %d", frame, a[2], a[3])
    elseif v == "snap" then
        mach.video:snapshot(); say("f%d snap", frame)
    elseif v == "dump" then
        local fh = assert(io.open(string.format("%s/dump-%s-f%d.bin", OUT, a[2], frame), "wb"))
        fh:write(space:read_range(a[3] & 0x1fffffff, (a[4] - 1) & 0x1fffffff, 8)); fh:close()
        say("f%d dump %s %08x-%08x", frame, a[2], a[3], a[4])
    elseif v == "poke" then
        for i, b in ipairs(a[3]) do space:write_u8((a[2] + i - 1) & 0x1fffffff, b) end
        say("f%d poke %08x (%d bytes)", frame, a[2], #a[3])
    elseif v == "mark" then
        say("f%d mark %s", frame, a[2])
    elseif v == "itrace" then
        -- MAME's debugger instruction trace: every executed PPC instruction,
        -- loops folded. Needs EXACT_PC=1 (-debug). Huge: keep windows short.
        local dbg = mach.debugger
        if dbg == nil then error("itrace needs EXACT_PC=1 (the debugger)") end
        if a[2] == "on" then
            local path = string.format("%s/itrace-f%d.txt", OUT, frame)
            -- HIRAKU_ITRACE_CYCLES=1: each line starts with the CPU's total cycles (DJ Main's
            -- HK_DJ_ITRACE writes the same), to find an instruction whose cost differs
            if os.getenv("HIRAKU_ITRACE_CYCLES") then
                dbg:command(string.format('trace %s,maincpu,noloop,{tracelog "%%d ",totalcycles}', path))
            else
                dbg:command(string.format("trace %s,maincpu,noloop", path))
            end
            say("f%d itrace on -> %s", frame, path)
        else
            dbg:command("trace off,maincpu")
            say("f%d itrace off", frame)
        end
    elseif v == "quit" then
        say("f%d quit; %d mmio records", frame, mmio_total + mmio_n)
        flush_mmio(); mmio:close(); frames:close()
        finished = true
        log:close(); log_open = false
        mach:exit()
    end
end

local function on_frame()
    frame = frame + 1
    -- the M48T58 shows its time from its first 1 s tick on: read after it, less that second
    if frame == 61 and TWINKLE then log_rtc_twinkle()
    elseif frame == 61 and cpu.shortname == "cxd8530cq" then log_rtc() end
    for f, until_frame in pairs(held) do
        if frame >= until_frame then f:set_value(0); held[f] = nil end
    end
    local line = { tostring(frame), string.format("%.6f", mach.time:as_double()) }
    for _, w in ipairs(watches) do
        local v = (w[2] == 1 and space:read_u8(w[3])) or (w[2] == 2 and space:read_u16(w[3])) or space:read_u32(w[3])
        line[#line + 1] = string.format("%x", v)
    end
    for _, h in ipairs(hashes) do line[#line + 1] = hash_now(frame) and string.format("%08x", fnv(h[2], h[3])) or "-" end
    frames:write(table.concat(line, " "), "\n")
    for _, r in ipairs(repeats) do
        if frame >= r[2] and (frame - r[2]) % r[1] == 0 then run(r[3]) end
    end
    for _, a in ipairs(events[frame] or {}) do
        run(a)
        if finished then return end
    end
    if frame % 60 == 0 then flush_mmio(); frames:flush() end
end

-- A failing frame callback is reported once and then stops the run: a script
-- that silently skips its quit would otherwise spin until the wall-clock cap.
local function guarded()
    if finished then return end
    local ok, err = pcall(on_frame)
    if not ok then
        say("f%d ERROR %s", frame, tostring(err))
        finished = true
        flush_mmio(); mach:exit()
    end
end

_G.hiraku_subs = {
    emu.add_machine_reset_notifier(install_taps),
    emu.add_machine_frame_notifier(guarded),
}
-- The autoboot script runs after the initial reset, so arm the taps now too.
install_taps()
-- HIRAKU_LUA: one more Lua file for a one-off probe (its own taps, prints)
local extra = os.getenv("HIRAKU_LUA") or ""
if extra ~= "" then dofile(extra) end
say("armed: %s", mach.system.name)
