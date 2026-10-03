// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gamepadnav.h"

#include <cfloat>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#include <ICursorControl.h>
#include <IGUIElement.h>
#include <IGUIEnvironment.h>
#include <IGUIListBox.h>
#include <IVideoDriver.h>
#include <IrrlichtDevice.h>

#include "client/keycode.h"
#include "client/renderingengine.h"
#include "guiButtonKey.h"
#include "guiInventoryList.h"
#include "guiTable.h"
#include "mainmenumanager.h"
#include "modalMenu.h"
#include "porting.h"
#include "settings.h"
#include "util/numeric.h"

GamepadMenuNavigator g_gamepad_nav;

namespace
{

constexpr float STICK_NAV_ON = 0.6f;   // deflection needed to start navigating
constexpr float STICK_NAV_OFF = 0.35f; // deflection below which navigation stops
constexpr float CURSOR_STICK_DEADZONE = 0.18f;
constexpr float CURSOR_STICK_CURVE = 2.0f; // fine control near the centre
constexpr float SCROLL_NOTCHES_PER_SECOND = 12.0f;
constexpr u64 DOUBLE_CLICK_MS = 400;
constexpr s32 DOUBLE_CLICK_DISTANCE = 8;
constexpr u64 CURSOR_TRUST_US = 300 * 1000;

enum ButtonIndex { LEFT_BUTTON = 0, MIDDLE_BUTTON = 1, RIGHT_BUTTON = 2 };

int buttonIndex(u32 sdl_button)
{
	switch (sdl_button) {
	case SDL_BUTTON_LEFT:
		return LEFT_BUTTON;
	case SDL_BUTTON_MIDDLE:
		return MIDDLE_BUTTON;
	default:
		return RIGHT_BUTTON;
	}
}

// Circular dead zone with a response curve; keeps the direction of the input.
v2f applyRadialDeadzone(v2f in, float inner, float curve)
{
	const float mag = in.getLength();
	if (mag <= inner || mag <= 0.0f)
		return v2f(0.0f, 0.0f);
	const float scaled = std::min(1.0f, (mag - inner) / (1.0f - inner));
	return in * (std::pow(scaled, curve) / mag);
}

s32 distanceToRange(s32 v, s32 lo, s32 hi)
{
	if (v < lo)
		return lo - v;
	if (v > hi)
		return v - hi;
	return 0;
}

} // namespace

bool GamepadMenuNavigator::isEnabled()
{
	return g_settings->getBool("gamepad_menu_navigation");
}

bool GamepadMenuNavigator::isKeyCaptureActive()
{
	auto *device = RenderingEngine::get_raw_device();
	if (!device)
		return false;
	auto *focus = device->getGUIEnvironment()->getFocus();
	auto *key_button = dynamic_cast<GUIButtonKey *>(focus);
	return key_button && key_button->isCapturing();
}

void GamepadMenuNavigator::reset()
{
	m_actions.clear();
	std::fill(std::begin(m_axes), std::end(m_axes), 0.0f);
	std::fill(std::begin(m_dpad), std::end(m_dpad), false);
	std::fill(std::begin(m_mouse_held), std::end(m_mouse_held), false);
	m_stick_active = false;
	m_swallow_left_up = false;
	m_nav_dir = v2s32(0, 0);
	m_nav_timer = 0.0f;
	m_nav_first = true;
	m_scroll_accum = 0.0f;
	m_cursor_valid = false;
	m_dirty = false;
}

/*
 * Event input
 */

bool GamepadMenuNavigator::handleEvent(const SEvent &event)
{
	if (!isEnabled() || !isMenuActive())
		return false;

	// While rebinding a key the button wants to see every gamepad input.
	if (isKeyCaptureActive())
		return false;

	if (event.EventType == EET_GAMEPAD_BUTTON_EVENT) {
		const auto &e = event.GamepadButtonEvent;
		const bool down = e.PressedDown;

		Action action;
		switch (e.Button) {
		case GamepadButton::SOUTH:
			action.type = down ? Action::MOUSE_DOWN : Action::MOUSE_UP;
			action.button = SDL_BUTTON_LEFT;
			break;
		case GamepadButton::WEST:
			action.type = down ? Action::MOUSE_DOWN : Action::MOUSE_UP;
			action.button = SDL_BUTTON_RIGHT;
			break;
		case GamepadButton::RIGHT_STICK:
			action.type = down ? Action::MOUSE_DOWN : Action::MOUSE_UP;
			action.button = SDL_BUTTON_MIDDLE;
			break;
		case GamepadButton::EAST:
			m_dirty = true;
			if (down) {
				action.type = Action::KEY;
				action.key = KEY_ESCAPE;
				m_actions.push_back(action);
			}
			return true;
		case GamepadButton::LEFT_SHOULDER:
		case GamepadButton::RIGHT_SHOULDER:
			m_dirty = true;
			if (down) {
				action.type = Action::TAB;
				action.flag = e.Button == GamepadButton::LEFT_SHOULDER;
				m_actions.push_back(action);
			}
			return true;
		case GamepadButton::DPAD_UP:
			m_dpad[0] = down;
			m_dirty = true;
			updateNavDirection();
			return true;
		case GamepadButton::DPAD_DOWN:
			m_dpad[1] = down;
			m_dirty = true;
			updateNavDirection();
			return true;
		case GamepadButton::DPAD_LEFT:
			m_dpad[2] = down;
			m_dirty = true;
			updateNavDirection();
			return true;
		case GamepadButton::DPAD_RIGHT:
			m_dpad[3] = down;
			m_dirty = true;
			updateNavDirection();
			return true;
		default:
			// Y, Start, Back, ... keep their normal meaning (e.g. closing the
			// inventory or the pause menu).
			return false;
		}

		m_dirty = true;
		m_actions.push_back(action);
		return true;
	}

	if (event.EventType == EET_GAMEPAD_AXIS_EVENT) {
		const auto &e = event.GamepadAxisEvent;
		const size_t axis = static_cast<size_t>(e.Axis);
		if (axis >= 6)
			return false;
		m_axes[axis] = rangelim(e.Value / 32767.0f, -1.0f, 1.0f);
		m_dirty = true;
		if (axis < 2)
			updateNavDirection();
		return true;
	}

	return false;
}

v2s32 GamepadMenuNavigator::leftStickDirection()
{
	const float x = m_axes[0];
	const float y = m_axes[1];
	const float mag = std::hypot(x, y);

	if (mag < (m_stick_active ? STICK_NAV_OFF : STICK_NAV_ON)) {
		m_stick_active = false;
		return v2s32(0, 0);
	}
	m_stick_active = true;

	// Snap to the dominant axis so diagonals do not jump around.
	if (std::fabs(x) > std::fabs(y))
		return v2s32(x > 0 ? 1 : -1, 0);
	return v2s32(0, y > 0 ? 1 : -1);
}

void GamepadMenuNavigator::updateNavDirection()
{
	v2s32 dir(m_dpad[3] - m_dpad[2], m_dpad[1] - m_dpad[0]);
	if (dir.X != 0 && dir.Y != 0)
		dir.Y = 0;
	if (dir == v2s32(0, 0))
		dir = leftStickDirection();
	else
		m_stick_active = false;

	if (dir == m_nav_dir)
		return;

	m_nav_dir = dir;
	m_nav_first = true;
	if (dir != v2s32(0, 0)) {
		Action action;
		action.type = Action::NAVIGATE;
		action.dir = dir;
		m_actions.push_back(action);
		// Wait a little before the movement starts repeating
		m_nav_timer = g_settings->getFloat("gamepad_menu_repeat_delay", 0.05f, 2.0f);
		m_nav_first = false;
	}
}

/*
 * Per frame work
 */

void GamepadMenuNavigator::step()
{
	const u64 now_us = porting::getTimeUs();
	const float dt = m_last_step_us == 0 ? 0.0f :
			std::min((now_us - m_last_step_us) / 1.0e6f, 0.1f);
	m_last_step_us = now_us;

	if (!isEnabled() || !isMenuActive()) {
		if (m_dirty)
			reset();
		return;
	}
	if (isKeyCaptureActive())
		return;

	// Repeat navigation while a direction is held
	if (m_nav_dir != v2s32(0, 0)) {
		m_nav_timer -= dt;
		if (m_nav_timer <= 0.0f) {
			Action action;
			action.type = Action::NAVIGATE;
			action.dir = m_nav_dir;
			m_actions.push_back(action);
			m_nav_timer = g_settings->getFloat("gamepad_menu_repeat_interval", 0.02f, 1.0f);
		}
	}

	while (!m_actions.empty()) {
		const Action action = m_actions.front();
		m_actions.pop_front();
		runAction(action);
		if (!isMenuActive()) {
			// The action closed the last menu
			reset();
			return;
		}
	}

	// Right stick: free cursor / scrolling
	const v2f stick = applyRadialDeadzone(v2f(m_axes[2], m_axes[3]),
			CURSOR_STICK_DEADZONE, CURSOR_STICK_CURVE);

	if (m_axes[4] > 0.5f) {
		// Left trigger held: scroll instead of moving the cursor
		m_scroll_accum += -stick.Y * dt * SCROLL_NOTCHES_PER_SECOND;
		while (m_scroll_accum >= 1.0f) {
			postWheel(1.0f);
			m_scroll_accum -= 1.0f;
		}
		while (m_scroll_accum <= -1.0f) {
			postWheel(-1.0f);
			m_scroll_accum += 1.0f;
		}
	} else {
		m_scroll_accum = 0.0f;
		if (stick.getLengthSQ() > 0.0f) {
			auto *driver = RenderingEngine::get_video_driver();
			const float scale = driver ? driver->getScreenSize().Height / 1080.0f : 1.0f;
			const float speed = g_settings->getFloat("gamepad_cursor_speed", 50.0f, 10000.0f);
			moveCursorBy(stick * (speed * scale * dt));
		}
	}
}

void GamepadMenuNavigator::runAction(const Action &action)
{
	switch (action.type) {
	case Action::MOUSE_DOWN:
		if (action.button == SDL_BUTTON_LEFT) {
			// An opened dropdown list is operated with the keyboard path since
			// its entries are not necessarily under the cursor.
			auto *focus = RenderingEngine::get_raw_device()->getGUIEnvironment()->getFocus();
			if (focus && focus->getType() == gui::EGUIET_LIST_BOX && focus->getParent() &&
					focus->getParent()->getType() == gui::EGUIET_COMBO_BOX) {
				postKeyPress(KEY_RETURN);
				m_swallow_left_up = true;
				return;
			}
		}
		postMouseButton(action.button, true);
		break;
	case Action::MOUSE_UP:
		if (action.button == SDL_BUTTON_LEFT && m_swallow_left_up) {
			m_swallow_left_up = false;
			return;
		}
		postMouseButton(action.button, false);
		break;
	case Action::KEY:
		postKeyPress(action.key);
		break;
	case Action::TAB:
		// GUIFormSpecMenu switches tabs on Ctrl+Tab / Ctrl+Shift+Tab
		postKeyPress(KEY_TAB, action.flag, true);
		break;
	case Action::NAVIGATE:
		navigate(action.dir);
		break;
	}
}

/*
 * Spatial navigation
 */

bool GamepadMenuNavigator::sendArrowKeyToFocus(v2s32 dir)
{
	auto *device = RenderingEngine::get_raw_device();
	auto *menu = g_menumgr.tryGetTopMenu();
	auto *focus = device->getGUIEnvironment()->getFocus();
	if (!focus || !menu || focus == menu)
		return false;

	const bool horizontal = dir.X != 0;
	EKEY_CODE key;
	if (dir.X < 0)
		key = KEY_LEFT;
	else if (dir.X > 0)
		key = KEY_RIGHT;
	else if (dir.Y < 0)
		key = KEY_UP;
	else
		key = KEY_DOWN;

	auto *list_box = dynamic_cast<gui::IGUIListBox *>(focus);
	auto *table = dynamic_cast<GUITable *>(focus);
	const bool list_like = list_box || table;
	const bool edit = focus->getType() == gui::EGUIET_EDIT_BOX;
	const bool scroll = focus->getType() == gui::EGUIET_SCROLL_BAR;

	if (!list_like && !edit && !scroll)
		return false;
	// Lists only react to vertical arrows; sideways input moves the focus away.
	if (list_like && horizontal)
		return false;

	auto selection = [&]() -> s32 {
		if (list_box)
			return list_box->getSelected();
		if (table)
			return table->getSelected();
		return -2;
	};

	const s32 before = selection();
	if (!postKeyPress(key))
		return false;
	// At the first/last row nothing changes: let the focus leave the list.
	if (list_like && selection() == before)
		return false;
	return true;
}

void GamepadMenuNavigator::collectCandidates(gui::IGUIElement *root,
		gui::IGUIElement *menu, std::vector<Candidate> &out) const
{
	for (gui::IGUIElement *child : root->getChildren()) {
		if (!child->isVisible() || !child->isEnabled())
			continue;

		if (auto *inv_list = dynamic_cast<GUIInventoryList *>(child)) {
			for (const auto &rect : inv_list->getSlotRects())
				out.push_back({rect, nullptr});
			continue;
		}

		const auto type = child->getType();
		const bool container = type == gui::EGUIET_TAB_CONTROL || type == gui::EGUIET_TAB;
		const bool selectable = !container && (child->isTabStop() ||
				(type == gui::EGUIET_SCROLL_BAR && child->getParent() == menu));
		if (selectable) {
			const core::rect<s32> rect = child->getAbsoluteClippingRect();
			if (rect.getArea() > 0)
				out.push_back({rect, child});
		}

		collectCandidates(child, menu, out);
	}
}

void GamepadMenuNavigator::focusCandidate(const Candidate &candidate)
{
	if (candidate.element)
		RenderingEngine::get_raw_device()->getGUIEnvironment()->setFocus(candidate.element);

	// Put the cursor on the target so hovering, tooltips and clicks all agree
	// with the focus.
	setCursorPos(candidate.area.getCenter());
}

void GamepadMenuNavigator::navigate(v2s32 dir)
{
	auto *menu = g_menumgr.tryGetTopMenu();
	if (!menu)
		return;

	if (sendArrowKeyToFocus(dir))
		return;

	std::vector<Candidate> candidates;
	collectCandidates(menu, menu, candidates);
	if (candidates.empty())
		return;

	const v2s32 cur = getCursorPos();

	const Candidate *best = nullptr;
	float best_score = FLT_MAX;
	bool inside_any = false;

	for (const auto &c : candidates) {
		if (c.area.isPointInside(cur)) {
			// This is the widget we are on right now.
			inside_any = true;
			continue;
		}
		const v2s32 center = c.area.getCenter();
		const v2s32 delta = center - cur;
		const s32 along = delta.X * dir.X + delta.Y * dir.Y;
		if (along <= 0)
			continue;

		const s32 perp_center = std::abs(dir.X != 0 ? delta.Y : delta.X);
		const s32 perp_gap = dir.X != 0 ?
				distanceToRange(cur.Y, c.area.UpperLeftCorner.Y, c.area.LowerRightCorner.Y) :
				distanceToRange(cur.X, c.area.UpperLeftCorner.X, c.area.LowerRightCorner.X);

		// Prefer widgets straight ahead over ones that are merely closer
		const float score = along + 4.0f * perp_gap + 0.5f * perp_center;
		if (score < best_score) {
			best_score = score;
			best = &c;
		}
	}

	if (!best && !inside_any) {
		// Nothing is selected yet: start with the closest widget in any direction.
		for (const auto &c : candidates) {
			const float dist = (c.area.getCenter() - cur).getLengthSQ();
			if (dist < best_score) {
				best_score = dist;
				best = &c;
			}
		}
	}

	if (best)
		focusCandidate(*best);
}

/*
 * Posting events
 */

void GamepadMenuNavigator::postKey(EKEY_CODE key, bool pressed, bool shift, bool ctrl) const
{
	SEvent ev{};
	ev.EventType = EET_KEY_INPUT_EVENT;
	ev.KeyInput.Key = key;
	ev.KeyInput.Char = 0;
	ev.KeyInput.SystemKeyCode = key == KEY_ESCAPE ? EscapeKey.getScancode() : 0;
	ev.KeyInput.PressedDown = pressed;
	ev.KeyInput.Shift = shift;
	ev.KeyInput.Control = ctrl;
	RenderingEngine::get_raw_device()->postEventFromUser(ev);
}

bool GamepadMenuNavigator::postKeyPress(EKEY_CODE key, bool shift, bool ctrl) const
{
	SEvent ev{};
	ev.EventType = EET_KEY_INPUT_EVENT;
	ev.KeyInput.Key = key;
	ev.KeyInput.Char = 0;
	ev.KeyInput.SystemKeyCode = key == KEY_ESCAPE ? EscapeKey.getScancode() : 0;
	ev.KeyInput.PressedDown = true;
	ev.KeyInput.Shift = shift;
	ev.KeyInput.Control = ctrl;
	const bool handled = RenderingEngine::get_raw_device()->postEventFromUser(ev);

	// The menu may have been closed by the key press
	if (isMenuActive())
		postKey(key, false, shift, ctrl);
	return handled;
}

u32 GamepadMenuNavigator::heldButtonMask() const
{
	u32 mask = 0;
	if (m_mouse_held[LEFT_BUTTON])
		mask |= SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
	if (m_mouse_held[MIDDLE_BUTTON])
		mask |= SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE);
	if (m_mouse_held[RIGHT_BUTTON])
		mask |= SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
	return mask;
}

void GamepadMenuNavigator::postMouseButton(u32 button, bool down)
{
	const v2s32 pos = getCursorPos();
	m_mouse_held[buttonIndex(button)] = down;

	SEvent ev{};
	ev.EventType = EET_MOUSE_INPUT_EVENT;
	ev.MouseInput.X = pos.X;
	ev.MouseInput.Y = pos.Y;
	ev.MouseInput.Button = button;
	ev.MouseInput.ButtonStates = heldButtonMask();
	ev.MouseInput.Shift = shiftHeld();
	ev.MouseInput.Control = false;

	switch (button) {
	case SDL_BUTTON_LEFT:
		ev.MouseInput.Event = down ? EMIE_LMOUSE_PRESSED_DOWN : EMIE_LMOUSE_LEFT_UP;
		break;
	case SDL_BUTTON_MIDDLE:
		ev.MouseInput.Event = down ? EMIE_MMOUSE_PRESSED_DOWN : EMIE_MMOUSE_LEFT_UP;
		break;
	default:
		ev.MouseInput.Event = down ? EMIE_RMOUSE_PRESSED_DOWN : EMIE_RMOUSE_LEFT_UP;
		break;
	}

	auto *device = RenderingEngine::get_raw_device();
	device->postEventFromUser(ev);

	// The SDL backend produces double click events itself; do the same here.
	if (down && button == SDL_BUTTON_LEFT && isMenuActive()) {
		const u64 now = porting::getTimeMs();
		const v2s32 moved = pos - m_last_left_click_pos;
		if (m_last_left_click_ms != 0 && now - m_last_left_click_ms <= DOUBLE_CLICK_MS &&
				std::abs(moved.X) <= DOUBLE_CLICK_DISTANCE &&
				std::abs(moved.Y) <= DOUBLE_CLICK_DISTANCE) {
			ev.MouseInput.Event = EMIE_LMOUSE_DOUBLE_CLICK;
			device->postEventFromUser(ev);
			m_last_left_click_ms = 0;
		} else {
			m_last_left_click_ms = now;
			m_last_left_click_pos = pos;
		}
	}
}

void GamepadMenuNavigator::postMouseMove(v2s32 pos) const
{
	SEvent ev{};
	ev.EventType = EET_MOUSE_INPUT_EVENT;
	ev.MouseInput.Event = EMIE_MOUSE_MOVED;
	ev.MouseInput.X = pos.X;
	ev.MouseInput.Y = pos.Y;
	ev.MouseInput.ButtonStates = heldButtonMask();
	ev.MouseInput.Shift = shiftHeld();
	RenderingEngine::get_raw_device()->postEventFromUser(ev);
}

void GamepadMenuNavigator::postWheel(float wheel) const
{
	const v2s32 pos = getCursorPos();

	SEvent ev{};
	ev.EventType = EET_MOUSE_INPUT_EVENT;
	ev.MouseInput.Event = EMIE_MOUSE_WHEEL;
	ev.MouseInput.Wheel = wheel;
	ev.MouseInput.X = pos.X;
	ev.MouseInput.Y = pos.Y;
	ev.MouseInput.ButtonStates = heldButtonMask();
	ev.MouseInput.Shift = shiftHeld();
	RenderingEngine::get_raw_device()->postEventFromUser(ev);
}

/*
 * Cursor handling
 */

v2f GamepadMenuNavigator::currentCursor() const
{
	// The OS reports the cursor position with a delay after we move it, so
	// trust our own value for a short while after the last gamepad movement.
	if (m_cursor_valid && porting::getTimeUs() - m_cursor_stamp_us < CURSOR_TRUST_US)
		return m_cursor;

	auto *cc = RenderingEngine::get_raw_device()->getCursorControl();
	if (!cc)
		return m_cursor;
	const auto pos = cc->getPosition();
	return v2f(pos.X, pos.Y);
}

v2s32 GamepadMenuNavigator::getCursorPos() const
{
	const v2f c = currentCursor();
	return v2s32(std::lround(c.X), std::lround(c.Y));
}

void GamepadMenuNavigator::setCursorPos(v2s32 pos)
{
	m_cursor = v2f(pos.X, pos.Y);
	m_cursor_valid = true;
	m_cursor_stamp_us = porting::getTimeUs();

	if (auto *cc = RenderingEngine::get_raw_device()->getCursorControl())
		cc->setPosition(pos.X, pos.Y);
	postMouseMove(pos);
}

void GamepadMenuNavigator::moveCursorBy(v2f delta)
{
	v2f target = currentCursor() + delta;

	if (auto *driver = RenderingEngine::get_video_driver()) {
		const auto size = driver->getScreenSize();
		target.X = rangelim(target.X, 0.0f, (float)size.Width - 1.0f);
		target.Y = rangelim(target.Y, 0.0f, (float)size.Height - 1.0f);
	}

	const v2s32 old_pos = getCursorPos();
	const v2s32 new_pos(std::lround(target.X), std::lround(target.Y));

	// Keep the sub-pixel remainder so slow movement still works
	m_cursor = target;
	m_cursor_valid = true;
	m_cursor_stamp_us = porting::getTimeUs();

	if (new_pos != old_pos) {
		if (auto *cc = RenderingEngine::get_raw_device()->getCursorControl())
			cc->setPosition(new_pos.X, new_pos.Y);
		postMouseMove(new_pos);
	}
}
