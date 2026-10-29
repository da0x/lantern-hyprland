-- SPDX-License-Identifier: GPL-3.0-or-later
hl.define_submap("resize", function ()
    hl.bind("right", hl.dsp.window.resize({ x = 10, y = 0 }), { repeating = true })
    hl.bind("escape", hl.dsp.submap("reset"))
end)

os.execute("touch should-never-exist")
