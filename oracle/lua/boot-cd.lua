-- DIP switch 4 ("Start Up Device") on the CD-ROM drive, as HIRAKU's --boot-cd: for an install over an
-- installed game (DrumMania 2nd's Session Power Up Kit). HIRAKU_LUA=oracle/lua/boot-cd.lua
local field = manager.machine.ioport.ports[":IN1"].fields["Start Up Device"]
field.user_value = 0x00000008
print("hiraku: DIP 4 Start Up Device = CD-ROM Drive")
