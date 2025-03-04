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

#include <InstrumentClipView.h>
#include <sounddrum.h>
#include "kit.h"
#include <string.h>
#include "storagemanager.h"
#include "song.h"
#include "View.h"
#include "uart.h"
#include "GeneralMemoryAllocator.h"
#include "ActionLogger.h"
#include "AudioEngine.h"
#include "VoiceVector.h"
#include "voice.h"
#include <new>
#include "Clip.h"

SoundDrum::SoundDrum() : Drum(DRUM_TYPE_SOUND), arpeggiator() {
	nameIsDiscardable = false;
}

/*
// Started but didn't finish this - it's hard!
Drum* SoundDrum::clone() {
	void* drumMemory = generalMemoryAllocator.alloc(sizeof(SoundDrum), NULL, false, true);
	if (!drumMemory) return NULL;
	SoundDrum* newDrum = new (drumMemory) SoundDrum();



	return newDrum;
}
*/

bool SoundDrum::readTagFromFile(char const* tagName) {
	if (!strcmp(tagName, "name")) {
		storageManager.readTagOrAttributeValueString(&name);
		storageManager.exitTag("name");
	}

	else if (readDrumTagFromFile(tagName)) {}
	else return false;

	return true;
}

bool SoundDrum::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	return Sound::allowNoteTails(modelStack, disregardSampleLoop);
}

bool SoundDrum::anyNoteIsOn() {
	return Sound::anyNoteIsOn();
}

bool SoundDrum::hasAnyVoices() {
	return Sound::hasAnyVoices();
}

void SoundDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
                       int fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {

	// If part of a Kit, and in choke mode, choke other drums
	if (polyphonic == POLYPHONY_CHOKE) {
		kit->choke();
	}

	Sound::noteOn(modelStack, &arpeggiator, NOTE_FOR_DRUM, mpeValues, sampleSyncLength, ticksLate, samplesLate,
	              velocity, fromMIDIChannel);
}
void SoundDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int velocity) {
	Sound::allNotesOff(modelStack, &arpeggiator);
}

extern bool expressionValueChangesMustBeDoneSmoothly;

void SoundDrum::expressionEvent(int newValue, int whichExpressionDimension) {

	int s = whichExpressionDimension + PATCH_SOURCE_X;

	//sourcesChanged |= 1 << s; // We'd ideally not want to apply this to all voices though...

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		if (expressionValueChangesMustBeDoneSmoothly) {
			thisVoice->expressionEventSmooth(newValue, s);
		}
		else {
			thisVoice->expressionEventImmediate(this, newValue, s);
		}
	}

	// Must update MPE values in Arp too - useful either if it's on, or if we're in true monophonic mode - in either case, we could need to suddenly do a note-on for a different note that the Arp knows about, and need these MPE values.
	arpeggiator.arpNote.mpeValues[whichExpressionDimension] = newValue >> 16;
}

void SoundDrum::polyphonicExpressionEventOnChannelOrNote(int newValue, int whichExpressionDimension,
                                                         int channelOrNoteNumber, int whichCharacteristic) {
	// Because this is a Drum, we disregard the noteCode (which is what channelOrNoteNumber always is in our case - but yeah, that's all irrelevant.
	expressionEvent(newValue, whichExpressionDimension);
}

void SoundDrum::unassignAllVoices() {
	Sound::unassignAllVoices();
}

void SoundDrum::setupPatchingForAllParamManagers(Song* song) {
	song->setupPatchingForAllParamManagersForDrum(this);
}

int SoundDrum::loadAllSamples(bool mayActuallyReadFiles) {
	return Sound::loadAllAudioFiles(mayActuallyReadFiles);
}

void SoundDrum::prepareForHibernation() {
	Sound::prepareForHibernation();
}

void SoundDrum::writeToFile(bool savingSong, ParamManager* paramManager) {
	storageManager.writeOpeningTagBeginning("sound");
	storageManager.writeAttribute("name", name.get());

	Sound::writeToFile(savingSong, paramManager, &arpSettings);

	if (savingSong) Drum::writeMIDICommandsToFile();

	storageManager.writeClosingTag("sound");
}

void SoundDrum::getName(char* buffer) {
}

int SoundDrum::readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack =
	    setupModelStackWithSong(modelStackMemory, song)->addTimelineCounter(clip)->addModControllableButNoNoteRow(this);

	return Sound::readFromFile(modelStack, readAutomationUpToPos, &arpSettings);
}

// modelStack may be NULL
void SoundDrum::choke(ModelStackWithSoundFlags* modelStack) {
	if (polyphonic == POLYPHONY_CHOKE) {

		// Don't choke it if it's auditioned
		if (getRootUI() == &instrumentClipView && instrumentClipView.isDrumAuditioned(this)) return;

		// Ok, choke it
		fastReleaseAllVoices(modelStack); // Accepts NULL
	}
}

void SoundDrum::setSkippingRendering(bool newSkipping) {
	if (kit && newSkipping != skippingRendering) {
		if (newSkipping) {
			kit->drumsWithRenderingActive.deleteAtKey((int32_t)(Drum*)this);
		}
		else {
			kit->drumsWithRenderingActive.insertAtKey((int32_t)(Drum*)this);
		}
	}

	Sound::setSkippingRendering(newSkipping);
}

uint8_t* SoundDrum::getModKnobMode() {
	return &kit->modKnobMode;
}

void SoundDrum::drumWontBeRenderedForAWhile() {
	Sound::wontBeRenderedForAWhile();
}

ArpeggiatorBase* SoundDrum::getArp() {
	return &arpeggiator;
}
