# Gamepad support

Luanti can be played and navigated entirely with a gamepad (anything SDL
recognises as a game controller: Xbox, PlayStation, Switch Pro, Steam Deck, ...).

## In game

| Input                | Action                                   |
| -------------------- | ---------------------------------------- |
| Left stick           | Move                                     |
| Right stick          | Look                                     |
| A / South            | Jump                                     |
| B / East             | Sneak                                    |
| X / West, L3         | Aux1 (sprint / fast move / climb down)   |
| Y / North            | Inventory                                |
| RT                   | Dig / punch / use                        |
| LT                   | Place / use                              |
| LB / RB              | Previous / next hotbar slot              |
| D-Pad up             | Zoom                                     |
| D-Pad down           | Drop item                                |
| D-Pad left           | Toggle fly                               |
| D-Pad right          | Open chat                                |
| R3 (right stick)     | Toggle camera mode                       |
| Back / Select        | Toggle minimap                           |
| Start                | Pause menu                               |

Everything can be rebound in *Settings -> Controls*:

* **Gamepad Bindings** page: shows only the gamepad part of every action.
  Select a button, then press the gamepad button or move the stick direction
  you want. The `+` button adds a second binding, the cross removes one.
  Keyboard and mouse bindings are never changed or shown here, and conflicts
  between gamepad bindings are highlighted.
* **Layout** buttons at the top of that page switch all gamepad bindings at
  once (keyboard/mouse bindings are kept). *Default* also acts as "reset
  gamepad bindings".

  | Layout        | What changes                                                        |
  | ------------- | ------------------------------------------------------------------- |
  | Default       | See the table above                                                 |
  | Southpaw      | Move with the right stick, look with the left stick; stick clicks swap |
  | Bumper jumper | Jump on LB, Aux1 on A/L3, chat on RB, fly on X, hotbar on D-Pad left/right |

  "Layout: Custom" is shown when your bindings match none of them.
* **Actions and Keybindings** page: the classic page showing all bindings of an
  action together (keyboard, mouse and gamepad).

The gamepad vibrates when you take damage (`gamepad_rumble`).

## In menus and formspecs

The main menu, pause menu, inventory and every mod formspec can be used
without mouse or keyboard (`gamepad_menu_navigation`).

| Input                 | Action                                                          |
| --------------------- | --------------------------------------------------------------- |
| D-Pad / left stick    | Move focus to the nearest widget or inventory slot in that direction. Lists, text fields and scrollbars use the arrows themselves; move sideways or past the first/last row to leave them. |
| A / South             | Left click (hold to keep the button pressed)                    |
| X / West              | Right click                                                     |
| R3 (right stick)      | Middle click                                                    |
| B / East              | Back / close                                                    |
| LB / RB               | Previous / next tab                                             |
| Right stick           | Move a free cursor                                              |
| LT + right stick up/down | Scroll the element under the cursor                          |
| RT (held)             | Acts as Shift (e.g. quick-move items in the inventory)          |
| Y / Start             | Keep their normal meaning (close inventory / close pause menu)  |

The focus and the cursor always move together, so tooltips and clicks match
what is highlighted. While a "press a button" key binding is waiting for
input, all gamepad input is passed through to it so any button can be bound.

Text fields work with the system on-screen keyboard (Steam Deck, Steam Big
Picture, consoles, ...), which SDL opens automatically when a text field
is focused.

## Tuning

All settings are in *Settings -> Controls -> Gamepads and Joysticks*.

| Setting                         | Purpose                                                    |
| ------------------------------- | ---------------------------------------------------------- |
| `joystick_inner_deadzone`       | Stick dead zone (default 0.25)                             |
| `joystick_outer_deadzone`       | Treat nearly-full deflection as full                       |
| `gamepad_radial_deadzone`       | Circular dead zone instead of one per axis                 |
| `gamepad_response_curve`        | 1 = linear, higher = finer control near the centre         |
| `gamepad_trigger_threshold`     | How far a trigger must be pulled to count as pressed       |
| `joystick_frustum_sensitivity`  | Look speed                                                 |
| `gamepad_look_vertical_ratio`   | Vertical look speed relative to horizontal                 |
| `gamepad_invert_look_y`         | Invert vertical look                                       |
| `gamepad_rumble`, `gamepad_rumble_strength` | Vibration                                      |
| `gamepad_menu_navigation`       | Enable/disable gamepad control of menus                    |
| `gamepad_cursor_speed`          | Free cursor speed in menus                                 |
| `gamepad_menu_repeat_delay`, `gamepad_menu_repeat_interval` | Auto-repeat of menu navigation |

## Notes for developers

* Raw gamepad axis events are turned into key presses in
  `MyEventReceiver::handleGamepadAxis` (`src/client/inputhandler.cpp`). Sticks
  are processed as pairs so the dead zone is circular.
* `src/gui/gamepadnav.{h,cpp}` contains `GamepadMenuNavigator`. It consumes
  gamepad events while a menu is open, and works by moving the real cursor and
  posting ordinary mouse/keyboard events through `IrrlichtDevice::postEventFromUser`,
  so formspecs and mods need no changes. It is stepped once per frame from
  `RenderingEngine::run()`.
* `IrrlichtDevice::rumbleGamepad()` / `hasGamepad()` expose SDL's rumble support.
