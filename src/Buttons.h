/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef BUTTONS_H_
#define BUTTONS_H_

#include "r_typedefs.h"

namespace Buttons {

int buttonAction(int x, int y, bool on, bool inCardRoutine);
bool isButtonPressed(int x, int y);
bool isShiftButtonPressed();
bool isNewOrShiftButtonPressed();
void noPressesHappening(bool inCardRoutine);

extern bool recordButtonPressUsedUp;
} // namespace Buttons

#endif /* BUTTONS_H_ */
