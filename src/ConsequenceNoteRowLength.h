/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#ifndef CONSEQUENCENOTEROWLENGTH_H_
#define CONSEQUENCENOTEROWLENGTH_H_

#include "Consequence.h"

class Action;
class ModelStackWithNoteRow;

class ConsequenceNoteRowLength final : public Consequence {
public:
	ConsequenceNoteRowLength(int newNoteRowId, int newLength);
	int revert(int time, ModelStack* modelStack);
	void performChange(ModelStackWithNoteRow* modelStack, Action* actionToRecordTo, int32_t oldPos,
	                   bool hadIndependentPlayPosBefore);
	int32_t backedUpLength;
	int noteRowId;
};

#endif /* CONSEQUENCENOTEROWLENGTH_H_ */
