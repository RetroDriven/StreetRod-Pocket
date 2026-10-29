# Pocket Controls

| Button | Behavior |
| --- | --- |
| D-pad | Move the on-screen hand/cursor; navigate on-screen keyboard |
| A | Primary mouse click; select highlighted virtual key |
| Y | Secondary mouse click |
| B | Escape / Back; cancel on-screen keyboard |
| X | Delete when using the numeric keypad |
| Start | Confirm numeric entry / on-screen keyboard action when available |
| Select + Start | Open/Close virtual keyboard if needed) |

The original DOS user interface is mouse-driven. To enter a name, select the editable field with **A**, then use the top-docked virtual keyboard. To enter an offer amount in the garage, move the hand to the **I'll take $** field and press **A** to open the numeric keypad; D-pad to select digits, X to delete, Start to confirm, B to cancel.

Physical button behavior may vary by the system's user-defined Pocket controller mapping. See `pocket/pocket_controls.h` and `pocket/pocket_engine_host.c` for the source mappings.
