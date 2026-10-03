// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <deque>
#include <vector>
#include "irrlichttypes_bloated.h"
#include "irr_v2d.h"
#include "rect.h"
#include "IEventReceiver.h"
#include "Keycodes.h"

namespace gui
{
class IGUIElement;
}

/**
 * Lets a gamepad drive every menu and formspec (main menu, pause menu,
 * inventory, mod formspecs, ...).
 *
 * Controls while a menu is open:
 *  - D-Pad / left stick:  move focus to the nearest widget (or inventory slot)
 *                         in that direction. Lists, dropdowns, text fields and
 *                         scrollbars get the arrow keys first.
 *  - A (south):           left click at the cursor (hold to keep pressed)
 *  - X (west):            right click
 *  - R3 (right stick):    middle click
 *  - B (east):            back / close (Escape)
 *  - LB / RB:             previous / next tab
 *  - Right stick:         move the cursor freely
 *  - LT + right stick:    scroll the element under the cursor
 *  - RT (held):           Shift modifier for clicks (quick-move in inventories)
 *  - Y / Start:           not consumed, so the existing keymap_inventory and
 *                         keymap_pause bindings still close menus.
 *
 * Everything works by moving the real cursor and posting regular mouse/key
 * events, so it behaves exactly like mouse and keyboard input.
 */
class GamepadMenuNavigator
{
public:
	/// Returns true if the event was consumed.
	/// Must only be called for gamepad events.
	bool handleEvent(const SEvent &event);

	/// Called once per frame (from RenderingEngine::run()).
	void step();

	/// Forget held buttons, queued actions and directions.
	void reset();

private:
	struct Action {
		enum Type {
			MOUSE_DOWN,
			MOUSE_UP,
			KEY,
			TAB,
			NAVIGATE,
		} type;
		u32 button = 0;           // SDL_BUTTON_* for mouse actions
		EKEY_CODE key = KEY_UNKNOWN;
		bool flag = false;        // TAB: go backwards
		v2s32 dir;                // NAVIGATE: direction
	};

	struct Candidate {
		core::rect<s32> area;
		gui::IGUIElement *element; // nullptr for e.g. inventory slots
	};

	static bool isEnabled();
	static bool isKeyCaptureActive();

	void updateNavDirection();
	v2s32 leftStickDirection();

	void runAction(const Action &action);
	void navigate(v2s32 dir);
	bool sendArrowKeyToFocus(v2s32 dir);
	void collectCandidates(gui::IGUIElement *root, gui::IGUIElement *menu,
			std::vector<Candidate> &out) const;
	void focusCandidate(const Candidate &candidate);

	void postKey(EKEY_CODE key, bool pressed, bool shift = false, bool ctrl = false) const;
	bool postKeyPress(EKEY_CODE key, bool shift = false, bool ctrl = false) const;
	void postMouseButton(u32 button, bool down);
	void postMouseMove(v2s32 pos) const;
	void postWheel(float wheel) const;

	v2f currentCursor() const;
	v2s32 getCursorPos() const;
	void setCursorPos(v2s32 pos);
	void moveCursorBy(v2f delta);

	u32 heldButtonMask() const;
	bool shiftHeld() const { return m_axes[5] > 0.5f; }

	std::deque<Action> m_actions;

	float m_axes[6] = {};
	bool m_dpad[4] = {}; // up, down, left, right
	bool m_stick_active = false;

	v2s32 m_nav_dir;
	float m_nav_timer = 0.0f;
	bool m_nav_first = true;

	bool m_mouse_held[3] = {}; // left, middle, right
	bool m_swallow_left_up = false;

	v2f m_cursor;
	bool m_cursor_valid = false;
	u64 m_cursor_stamp_us = 0;
	float m_scroll_accum = 0.0f;

	u64 m_last_step_us = 0;
	u64 m_last_left_click_ms = 0;
	v2s32 m_last_left_click_pos;

	bool m_dirty = false; // whether there is any state worth resetting
};

extern GamepadMenuNavigator g_gamepad_nav;
