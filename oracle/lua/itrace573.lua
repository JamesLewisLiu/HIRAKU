-- HIRAKU_LUA probe (System 573): MAME's debugger instruction trace with the total
-- cycle count, from frame HK_ITRACE_FROM to HK_ITRACE_TO (HIRAKU frame numbers).
-- Needs EXACT_PC=1 (-debug -debugger none). Compare with HK_573_ITRACE in HIRAKU.
local from = tonumber(os.getenv("HK_ITRACE_FROM") or "139")
local to = tonumber(os.getenv("HK_ITRACE_TO") or "140")
local out = assert(os.getenv("HIRAKU_OUT"))
local f = 0
itrace573_sub = emu.add_machine_frame_notifier(function()
    f = f + 1
    local dbg = manager.machine.debugger
    if f == from then
        dbg:command(string.format('trace %s/itrace.txt,maincpu,noloop,{tracelog "%%d ",totalcycles}', out))
    elseif f == to then
        dbg:command("trace off,maincpu")
    end
end)
