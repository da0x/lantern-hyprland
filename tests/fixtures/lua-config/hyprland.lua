-- SPDX-License-Identifier: GPL-3.0-or-later
-- A config written for the tests, in the shape of a real one: locals, a loop, a function
-- binding, a submap in a required file, and a file read at load time.

local mainMod  = "SUPER"
local terminal = "kitty"

hl.on("hyprland.start", function ()
    hl.exec_cmd("waybar")
end)

hl.config({
    general = { gaps_in = 5, layout = "dwindle" },
})

hl.bind(mainMod .. " + Q", hl.dsp.exec_cmd(terminal))
hl.bind("SUPER + mouse:272", hl.dsp.window.drag(), { mouse = true })

for i = 1, 3 do
    hl.bind(mainMod .. " + " .. i, hl.dsp.focus({ workspace = i }))
end

hl.bind("SUPER + F", function ()
    hl.dispatch(hl.dsp.window.float({ action = "toggle" }))
end)

hl.bind("XF86AudioMute", hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"), { locked = true, description = "Mute" })

local file   = io.open("keyboard.txt")
local layout = file:read("l")
file:close()
hl.bind(mainMod .. " + " .. (layout == "azerty" and "A" or "W"), hl.dsp.window.close())

require("binds.resize")
