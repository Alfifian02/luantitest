-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later
--
-- Gamepad layout presets.
--
-- A preset only describes the *gamepad* bindings of `keymap_*` settings.
-- Keyboard and mouse bindings are never touched when a preset is applied.
-- Actions a preset doesn't mention get their default gamepad bindings, so the
-- presets can never go out of sync with defaultsettings.cpp.
--
-- Gamepad keycodes: GAMEPAD_BUTTON_<n> (SDL gamepad button numbering, e.g.
-- 0 = south/A, 9 = left shoulder, 13 = D-pad left), GAMEPAD_AXIS_PLUS_<n> and
-- GAMEPAD_AXIS_MINUS_<n> (0/1 = left stick, 2/3 = right stick, 4/5 = triggers).

local M = {}

-- Order matters: it is the order of the buttons in the UI.
M.layouts = {
	{
		id = "default",
		overrides = {},
	},
	{
		-- Move with the right stick, look with the left stick
		id = "southpaw",
		overrides = {
			keymap_forward = { "GAMEPAD_AXIS_MINUS_3" },
			keymap_backward = { "GAMEPAD_AXIS_PLUS_3" },
			keymap_left = { "GAMEPAD_AXIS_MINUS_2" },
			keymap_right = { "GAMEPAD_AXIS_PLUS_2" },
			keymap_camera_yaw_left = { "GAMEPAD_AXIS_MINUS_0" },
			keymap_camera_yaw_right = { "GAMEPAD_AXIS_PLUS_0" },
			keymap_camera_pitch_up = { "GAMEPAD_AXIS_MINUS_1" },
			keymap_camera_pitch_down = { "GAMEPAD_AXIS_PLUS_1" },
			-- stick clicks follow their sticks
			keymap_aux1 = { "GAMEPAD_BUTTON_2", "GAMEPAD_BUTTON_8" },
			keymap_camera_mode = { "GAMEPAD_BUTTON_7" },
		},
	},
	{
		-- Jump on the left bumper so the thumb never leaves the right stick
		id = "bumper_jumper",
		overrides = {
			keymap_jump = { "GAMEPAD_BUTTON_9" },
			keymap_aux1 = { "GAMEPAD_BUTTON_0", "GAMEPAD_BUTTON_7" },
			keymap_chat = { "GAMEPAD_BUTTON_10" },
			keymap_freemove = { "GAMEPAD_BUTTON_2" },
			keymap_hotbar_previous = { "GAMEPAD_BUTTON_13" },
			keymap_hotbar_next = { "GAMEPAD_BUTTON_14" },
		},
	},
}

function M.is_gamepad_keycode(str)
	return str:sub(1, #"GAMEPAD_") == "GAMEPAD_"
end

local function split(value)
	local pad, others = {}, {}
	for _, v in ipairs(value:split("|")) do
		table.insert(M.is_gamepad_keycode(v) and pad or others, v)
	end
	return pad, others
end

local function find_layout(id)
	for _, layout in ipairs(M.layouts) do
		if layout.id == id then
			return layout
		end
	end
end

-- Gamepad bindings `layout` wants for the key setting `info`
-- (an entry of core.full_settingtypes), as a list.
local function wanted_bindings(layout, info)
	local override = layout.overrides[info.name]
	if override then
		return override
	end
	return (split(info.default or ""))
end

local function is_key_setting(info)
	return info.type == "key" and info.name:sub(1, #"keymap_") == "keymap_"
end

-- Applies a layout to `settings` (core.settings or anything with
-- get/set/remove). `settingtypes` is core.full_settingtypes.
-- Returns false for an unknown layout.
function M.apply(layout_id, settingtypes, settings)
	local layout = find_layout(layout_id)
	if not layout then
		return false
	end
	for _, info in ipairs(settingtypes) do
		if is_key_setting(info) then
			local _, others = split(settings:get(info.name) or info.default or "")
			local new = table.copy(others)
			for _, v in ipairs(wanted_bindings(layout, info)) do
				new[#new + 1] = v
			end
			local str = table.concat(new, "|")
			if str == (info.default or "") then
				settings:remove(info.name)
			else
				settings:set(info.name, str)
			end
		end
	end
	return true
end

-- Returns the id of the layout whose gamepad bindings match the current
-- settings exactly (ignoring order), or nil if the bindings are custom.
function M.detect(settingtypes, settings)
	for _, layout in ipairs(M.layouts) do
		local matches = true
		for _, info in ipairs(settingtypes) do
			if is_key_setting(info) then
				local have = split(settings:get(info.name) or info.default or "")
				local want = wanted_bindings(layout, info)
				local set = {}
				for _, v in ipairs(have) do set[v] = true end
				local n = 0
				for _, v in ipairs(want) do
					if not set[v] then matches = false break end
					n = n + 1
				end
				if matches and n ~= #have then matches = false end
			end
			if not matches then break end
		end
		if matches then
			return layout.id
		end
	end
	return nil
end

return M
