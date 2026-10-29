-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- The recording interpreter. It runs a Hyprland Lua config with a stand-in for `hl` that
-- records every call, with its arguments and the file and line it came from, and runs
-- nothing: no dispatcher, no program, no write.
--
-- This chunk is trusted and has the full standard library. The config it loads does not:
-- it gets `sandbox`, built below from an allow-list, and nothing that leads back here.
--
-- The host passes:
--   host.main()           the main config file's path
--   host.folder()         the config folder
--   host.resolve(path)    an absolute path inside the config folder, following links, to a
--                         regular file; or an error. Relative paths start at the folder.
--
-- It returns plain, tagged data (see `tag`) for the C++ side to copy out.

local host = ...

local getinfo      = debug.getinfo
local realLoad     = load
local realOpen     = io.open
local mathType     = math.type
local sort         = table.sort
local pack         = table.pack

local recorderSource = getinfo(1, "S").source

-- A string's methods are the real string library, which the config reaches through any
-- string value. Nothing here needs bytecode, so it goes, and the config never sees the
-- library itself (see sandbox.getmetatable).
string.dump = nil

local calls    = {}
local blocked  = {}
local files    = {}
local submap   = ""

---------------------------------------------------------------------------------------
-- Where a call came from: every frame of the user's code on the stack, innermost first.

local function stack()
    local frames = {}
    local level  = 2
    while true do
        local info = getinfo(level, "Sl")
        if not info then
            break
        end
        if info.source ~= recorderSource and info.source:sub(1, 1) == "@" and info.currentline >= 0 then
            frames[#frames + 1] = { file = info.source:sub(2), line = info.currentline }
        end
        level = level + 1
    end
    return frames
end

---------------------------------------------------------------------------------------
-- Tagged copies of values. Every value becomes { t = kind, ... }, so a table the user
-- wrote can never be mistaken for something the recorder made.

local actions = setmetatable({}, { __mode = "k" })

local tag

local function tagList(list)
    local tagged = {}
    for index = 1, list.n do
        tagged[index] = tag(list[index], {}, 0)
    end
    return tagged
end

local function keyOrder(a, b)
    local ta, tb = type(a), type(b)
    if ta ~= tb then
        return ta < tb
    end
    if ta == "number" or ta == "string" then
        return a < b
    end
    return tostring(a) < tostring(b)
end

tag = function(value, seen, depth)
    local kind = type(value)
    if kind == "nil" then
        return { t = "nil" }
    elseif kind == "boolean" then
        return { t = "boolean", v = value }
    elseif kind == "number" then
        return { t = mathType(value) == "integer" and "integer" or "number", v = value }
    elseif kind == "string" then
        return { t = "string", v = value }
    elseif kind == "function" then
        local info = getinfo(value, "S")
        local file = info.source:sub(1, 1) == "@" and info.source:sub(2) or ""
        return { t = "function", file = file, line = info.linedefined }
    elseif kind == "table" then
        local action = actions[value]
        if action then
            return { t = "action", name = action.name, args = tagList(action.args) }
        end
        if seen[value] or depth > 32 then
            return { t = "unreadable", v = "a table that contains itself" }
        end
        seen[value] = true

        local entries = {}
        local count   = #value
        for index = 1, count do
            entries[#entries + 1] = { k = { t = "integer", v = index }, v = tag(rawget(value, index), seen, depth + 1) }
        end
        local keys = {}
        for key in next, value do
            if not (mathType(key) == "integer" and key >= 1 and key <= count) then
                keys[#keys + 1] = key
            end
        end
        sort(keys, keyOrder)
        for _, key in ipairs(keys) do
            entries[#entries + 1] = { k = tag(key, seen, depth + 1), v = tag(rawget(value, key), seen, depth + 1) }
        end

        seen[value] = nil
        return { t = "table", entries = entries }
    end
    return { t = "unreadable", v = kind }
end

local function record(name, ...)
    calls[#calls + 1] = { name = name, args = tagList(pack(...)), submap = submap, stack = stack() }
end

local function refuse(what)
    blocked[#blocked + 1] = { what = what, stack = stack() }
end

---------------------------------------------------------------------------------------
-- The `hl` stand-in, written out by hand from Hyprland's own type stubs
-- (/usr/share/hypr/stubs/hl.meta.lua). A name that is not here is an error, so a call Lantern
-- does not know about is reported instead of silently recorded as something else.

local function strict(name, contents)
    return setmetatable(contents, {
        __index = function(_, key)
            error(("%s.%s is not part of Hyprland's Lua API as Lantern knows it"):format(name, tostring(key)), 2)
        end,
        __metatable = false,
    })
end

-- What a call that creates something returns: an object whose methods do nothing.
local function inert()
    return setmetatable({}, {
        __index = function()
            return function()
                return nil
            end
        end,
        __metatable = false,
    })
end

local function recorder(name, result)
    return function(...)
        record(name, ...)
        if result then
            return result()
        end
    end
end

local function dispatchers(prefix, names, namespaces)
    local namespace = {}
    for _, name in ipairs(names) do
        local full = prefix .. name
        namespace[name] = function(...)
            local action = {}
            actions[action] = { name = full, args = pack(...) }
            return action
        end
    end
    for name, inner in pairs(namespaces or {}) do
        namespace[name] = inner
    end
    return strict("hl.dsp" .. (prefix ~= "" and "." .. prefix:sub(1, -2) or ""), namespace)
end

local dsp = dispatchers("", {
    "dpms", "event", "exec_cmd", "exec_raw", "exit", "focus", "force_idle", "force_renderer_reload", "global",
    "layout", "no_op", "pass", "release_input_capture", "send_key_state", "send_shortcut", "submap",
}, {
    cursor    = dispatchers("cursor.", { "move", "move_to_corner" }),
    group     = dispatchers("group.", { "active", "lock", "lock_active", "move_window", "next", "prev", "toggle" }),
    window    = dispatchers("window.", {
        "alter_zorder", "bring_to_top", "center", "clear_tags", "close", "cycle_next", "deny_from_group", "drag",
        "float", "fullscreen", "fullscreen_state", "kill", "move", "pin", "pseudo", "resize", "set_prop", "signal",
        "swap", "tag", "toggle_swallow",
    }),
    workspace = dispatchers("workspace.", { "change_id", "move", "rename", "swap_monitors", "toggle_special" }),
})

local function none()
    return nil
end

local function empty()
    return {}
end

local hl = strict("hl", {
    -- Calls that configure: recorded, and that is all.
    animation  = recorder("animation"),
    config     = recorder("config"),
    curve      = recorder("curve"),
    device     = recorder("device"),
    env        = recorder("env"),
    gesture    = recorder("gesture"),
    monitor    = recorder("monitor"),
    permission = recorder("permission"),
    unbind     = recorder("unbind"),

    -- Calls that create something the config may hold on to.
    bind           = recorder("bind", inert),
    window_rule    = recorder("window_rule", inert),
    layer_rule     = recorder("layer_rule", inert),
    workspace_rule = recorder("workspace_rule", inert),
    on             = recorder("on", inert),
    timer          = recorder("timer", inert),

    -- Calls that would act on the running desktop: recorded, never done.
    dispatch                                 = recorder("dispatch"),
    exec_cmd                                 = recorder("exec_cmd"),
    clear_crashed_lockscreen                 = recorder("clear_crashed_lockscreen"),
    exec_scheduled_prop_refresh_immediately = recorder("exec_scheduled_prop_refresh_immediately"),

    -- A submap's body runs now, the way Hyprland runs it, so its bindings are recorded
    -- with the submap's name.
    define_submap = function(name, resetOrBody, body)
        record("define_submap", name, resetOrBody, body)
        local run = type(resetOrBody) == "function" and resetOrBody or body
        if type(run) == "function" then
            local outer = submap
            submap      = tostring(name)
            run()
            submap = outer
        end
    end,

    -- Questions about the running desktop. A config being read has no desktop, so they
    -- answer as if nothing were there.
    get_active_monitor            = none,
    get_active_special_workspace  = none,
    get_active_window             = none,
    get_active_workspace          = none,
    get_config                    = none,
    get_cursor_pos                = none,
    get_last_window               = none,
    get_last_workspace            = none,
    get_monitor                   = none,
    get_monitor_at                = none,
    get_monitor_at_cursor         = none,
    get_urgent_window             = none,
    get_window                    = none,
    get_workspace                 = none,
    get_layers                    = empty,
    get_loaded_plugins            = empty,
    get_monitors                  = empty,
    get_windows                   = empty,
    get_workspace_windows         = empty,
    get_workspaces                = empty,
    get_current_submap            = function() return submap end,
    is_key_down                   = function() return false end,
    version                       = function() return "" end,

    dsp          = dsp,
    layout       = strict("hl.layout", { register = recorder("layout.register") }),
    notification = strict("hl.notification", { create = recorder("notification.create", inert), get = empty }),

    -- Plugins add their own functions here, so any name is accepted and recorded.
    plugin = setmetatable({ load = recorder("plugin.load") }, {
        __index = function(_, key)
            return recorder("plugin." .. tostring(key))
        end,
        __metatable = false,
    }),
})

---------------------------------------------------------------------------------------
-- The sandbox the config runs in.

local sandbox = {}

local function copy(library, names)
    local copied = {}
    for _, name in ipairs(names) do
        copied[name] = library[name]
    end
    return copied
end

-- Only text chunks, and never with the recorder's own environment by default.
local function sandboxLoad(chunk, name, _, environment)
    return realLoad(chunk, name, "t", environment or sandbox)
end

local function openForReading(path, mode)
    if mode ~= nil and mode ~= "r" and mode ~= "rb" then
        refuse(("io.open(%q, %q)"):format(tostring(path), tostring(mode)))
        return nil, "Lantern does not let a config write files while reading it"
    end
    local ok, resolved = pcall(host.resolve, tostring(path))
    if not ok then
        return nil, resolved
    end
    return realOpen(resolved, "r")
end

local function blockedFunction(name)
    return function()
        refuse(name)
        return nil, "Lantern does not run this while reading a config"
    end
end

local loaded = {}

local function loadFile(path, ...)
    local file, openError = realOpen(path, "r")
    if not file then
        error(openError, 0)
    end
    local text = file:read("a")
    file:close()
    files[#files + 1] = path

    local chunk, syntaxError = realLoad(text, "@" .. path, "t", sandbox)
    if not chunk then
        error(syntaxError, 0)
    end
    return chunk(...)
end

-- Resolves modules the way Hyprland does: names relative to the config folder, or paths
-- starting with "/", "./", "../" or "~/". Globs are not followed yet.
local function sandboxRequire(name)
    name = tostring(name)
    if loaded[name] ~= nil then
        return loaded[name]
    end
    if name:find("*", 1, true) then
        error(("require(%q): Lantern does not follow globs yet"):format(name), 2)
    end

    local candidates
    if name:match("^/") or name:match("^%./") or name:match("^%.%./") or name:match("^~/") then
        candidates = { name, name .. ".lua" }
    else
        local relative = name:gsub("%.", "/")
        candidates     = { relative .. ".lua", relative .. "/init.lua" }
    end

    local failures = {}
    for _, candidate in ipairs(candidates) do
        local ok, resolved = pcall(host.resolve, candidate)
        if ok then
            local result = loadFile(resolved, name)
            loaded[name] = result == nil and true or result
            return loaded[name]
        end
        failures[#failures + 1] = resolved
    end
    error(("module %q not found: %s"):format(name, table.concat(failures, "; ")), 2)
end

sandbox.hl       = hl
sandbox._VERSION = _VERSION
for _, name in ipairs({
    "assert", "error", "ipairs", "next", "pairs", "pcall", "rawequal", "rawget", "rawlen", "rawset", "select",
    "setmetatable", "tonumber", "tostring", "type", "xpcall",
}) do
    sandbox[name] = _G[name]
end
sandbox.getmetatable = function(value)
    if type(value) == "string" then
        return nil
    end
    return getmetatable(value)
end
sandbox.load           = sandboxLoad
sandbox.require        = sandboxRequire
sandbox.print          = function() end
sandbox.collectgarbage = function() return 0 end
sandbox.dofile         = blockedFunction("dofile")
sandbox.loadfile       = blockedFunction("loadfile")

sandbox.string    = copy(string, { "byte", "char", "find", "format", "gmatch", "gsub", "len", "lower", "match", "pack", "packsize", "rep", "reverse", "sub", "unpack", "upper" })
sandbox.table     = copy(table, { "concat", "insert", "move", "pack", "remove", "sort", "unpack" })
sandbox.math      = copy(math, { "abs", "ceil", "cos", "deg", "exp", "floor", "fmod", "huge", "log", "max", "maxinteger", "min", "mininteger", "modf", "pi", "rad", "random", "randomseed", "sin", "sqrt", "tan", "tointeger", "type", "ult" })
sandbox.utf8      = copy(utf8, { "char", "charpattern", "codepoint", "codes", "len", "offset" })
sandbox.coroutine = copy(coroutine, { "close", "create", "isyieldable", "resume", "running", "status", "wrap", "yield" })

sandbox.os = copy(os, { "clock", "date", "difftime", "getenv", "time" })
for _, name in ipairs({ "execute", "exit", "remove", "rename", "setlocale", "tmpname" }) do
    sandbox.os[name] = blockedFunction("os." .. name)
end

sandbox.io = {
    open  = openForReading,
    lines = function(path, ...)
        local file, openError = openForReading(path, "r")
        if not file then
            error(openError, 2)
        end
        return file:lines(...)
    end,
}
for _, name in ipairs({ "close", "input", "output", "popen", "read", "tmpfile", "type", "write" }) do
    sandbox.io[name] = blockedFunction("io." .. name)
end

sandbox._G = sandbox

---------------------------------------------------------------------------------------

local ok, failure = xpcall(loadFile, function(message)
    return { message = tostring(message), stack = stack() }
end, host.main())

return {
    calls   = calls,
    blocked = blocked,
    files   = files,
    error   = (not ok) and { message = failure.message, stack = failure.stack } or nil,
}
