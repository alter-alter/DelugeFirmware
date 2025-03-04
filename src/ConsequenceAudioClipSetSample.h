/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#ifndef CONSEQUENCEAUDIOCLIPSETSAMPLE_H_
#define CONSEQUENCEAUDIOCLIPSETSAMPLE_H_

#include "Consequence.h"
#include "DString.h"

class AudioClip;

class ConsequenceAudioClipSetSample final : public Consequence {
public:
	ConsequenceAudioClipSetSample(AudioClip* newClip);
	int revert(int time, ModelStack* modelStack);

	AudioClip* clip;
	String filePathToRevertTo;
	uint64_t endPosToRevertTo;
};

#endif /* CONSEQUENCEAUDIOCLIPSETSAMPLE_H_ */
