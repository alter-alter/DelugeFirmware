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

#include "gui/ui/sample_marker_editor.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/clip/audio_clip.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_vector.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"
#include "util/misc.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};

SampleMarkerEditor sampleMarkerEditor{};

SampleMarkerEditor::SampleMarkerEditor() {
}

SampleHolder* getCurrentSampleHolder() {
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return &((AudioClip*)currentSong->currentClip)->sampleHolder;
	}
	else {
		return &((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder;
	}
}

MultisampleRange* getCurrentMultisampleRange() {
	return (MultisampleRange*)soundEditor.currentMultiRange;
}

SampleControls* getCurrentSampleControls() {
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return &((AudioClip*)currentSong->currentClip)->sampleControls;
	}
	else {
		return &soundEditor.currentSource->sampleControls;
	}
}

bool SampleMarkerEditor::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*cols = 0b10;
	return true;
}

bool SampleMarkerEditor::opened() {

	if (getRootUI() == &keyboardScreen) {
		PadLEDs::skipGreyoutFade();
	}

	uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);

	waveformBasicNavigator.sample = (Sample*)getCurrentSampleHolder()->audioFile;

	if (!waveformBasicNavigator.sample) {
		numericDriver.displayPopup(HAVE_OLED ? "No sample" : "CANT");
		return false;
	}

	waveformBasicNavigator.opened(getCurrentSampleHolder());

	blinkInvisible = false;

	uiNeedsRendering(this, 0xFFFFFFFF, 0);

#if !HAVE_OLED
	displayText();
#endif

	if (getRootUI() != &instrumentClipView) {
		renderingNeededRegardlessOfUI(0, 0xFFFFFFFF);
	}

	focusRegained();
	return true;
}

void SampleMarkerEditor::recordScrollAndZoom() {
	if (markerType != MarkerType::NONE) {
		getCurrentSampleHolder()->waveformViewScroll = waveformBasicNavigator.xScroll;
		getCurrentSampleHolder()->waveformViewZoom = waveformBasicNavigator.xZoom;
	}
}

void SampleMarkerEditor::writeValue(uint32_t value, MarkerType markerTypeNow) {
	if (markerTypeNow == MarkerType::NOT_AVAILABLE) {
		markerTypeNow = markerType;
	}

	int clipType = currentSong->currentClip->type;

	bool audioClipActive;
	if (clipType == CLIP_TYPE_AUDIO) {
		audioClipActive = (playbackHandler.isEitherClockActive() && currentSong->isClipActive(currentSong->currentClip)
		                   && ((AudioClip*)currentSong->currentClip)->voiceSample);

		((AudioClip*)currentSong->currentClip)->unassignVoiceSample();
	}

	if (markerTypeNow == MarkerType::START) {
		getCurrentSampleHolder()->startPos = value;
	}
	else if (markerTypeNow == MarkerType::LOOP_START) {
		getCurrentMultisampleRange()->sampleHolder.loopStartPos = value;
	}
	else if (markerTypeNow == MarkerType::LOOP_END) {
		getCurrentMultisampleRange()->sampleHolder.loopEndPos = value;
	}
	else if (markerTypeNow == MarkerType::END) {
		getCurrentSampleHolder()->endPos = value;
	}

	getCurrentSampleHolder()->claimClusterReasons(getCurrentSampleControls()->reversed,
	                                              CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE);

	if (clipType == CLIP_TYPE_AUDIO) {
		if (audioClipActive) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			currentSong->currentClip->resumePlayback(modelStack, true);
		}
	}
	else {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithSoundFlags* modelStack = soundEditor.getCurrentModelStack(modelStackMemory)->addSoundFlags();
		soundEditor.currentSound->sampleZoneChanged(markerTypeNow, soundEditor.currentSourceIndex, modelStack);
		((Instrument*)currentSong->currentClip->output)->beenEdited(true);
	}
}

int SampleMarkerEditor::getStartColOnScreen(int32_t unscrolledPos) {
	return divide_round_negative((int32_t)(unscrolledPos - waveformBasicNavigator.xScroll),
	                             waveformBasicNavigator.xZoom);
}

int SampleMarkerEditor::getEndColOnScreen(int32_t unscrolledPos) {
	return divide_round_negative((int32_t)(unscrolledPos - 1 - waveformBasicNavigator.xScroll),
	                             waveformBasicNavigator.xZoom);
}

int SampleMarkerEditor::getStartPosFromCol(int col) {
	return waveformBasicNavigator.xScroll + col * waveformBasicNavigator.xZoom;
}

int SampleMarkerEditor::getEndPosFromCol(int col) {
	return waveformBasicNavigator.xScroll + (col + 1) * waveformBasicNavigator.xZoom;
}

void SampleMarkerEditor::getColsOnScreen(MarkerColumn* cols) {
	cols[util::to_underlying(MarkerType::START)].pos = getCurrentSampleHolder()->startPos;
	cols[util::to_underlying(MarkerType::START)].colOnScreen =
	    getStartColOnScreen(cols[util::to_underlying(MarkerType::START)].pos);

	if (currentSong->currentClip->type != CLIP_TYPE_AUDIO) {
		cols[util::to_underlying(MarkerType::LOOP_START)].pos = getCurrentMultisampleRange()->sampleHolder.loopStartPos;
		cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen =
		    cols[util::to_underlying(MarkerType::LOOP_START)].pos
		        ? getStartColOnScreen(cols[util::to_underlying(MarkerType::LOOP_START)].pos)
		        : -2147483648;

		cols[util::to_underlying(MarkerType::LOOP_END)].pos = getCurrentMultisampleRange()->sampleHolder.loopEndPos;
		cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen =
		    cols[util::to_underlying(MarkerType::LOOP_END)].pos
		        ? getEndColOnScreen(cols[util::to_underlying(MarkerType::LOOP_END)].pos)
		        : -2147483648;
	}

	else {
		cols[util::to_underlying(MarkerType::LOOP_START)].pos = 0;
		cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen = -2147483648;

		cols[util::to_underlying(MarkerType::LOOP_END)].pos = 0;
		cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen = -2147483648;
	}

	cols[util::to_underlying(MarkerType::END)].pos = getCurrentSampleHolder()->endPos;
	cols[util::to_underlying(MarkerType::END)].colOnScreen =
	    getEndColOnScreen(cols[util::to_underlying(MarkerType::END)].pos);
}

void SampleMarkerEditor::selectEncoderAction(int8_t offset) {
	if (currentUIMode && currentUIMode != UI_MODE_AUDITIONING) {
		return;
	}

	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	int oldCol = cols[util::to_underlying(markerType)].colOnScreen;
	int oldPos = cols[util::to_underlying(markerType)].pos;
	int newCol = oldCol + offset;

	// Make sure we don't drive one marker into the other
	for (int c = 0; c < kNumMarkerTypes; c++) {
		if (c == util::to_underlying(markerType)) {
			continue;
		}
		if (cols[c].colOnScreen == oldCol || cols[c].colOnScreen == newCol) {
			return;
		}
	}

	int32_t newMarkerPos = (markerType < MarkerType::LOOP_END) ? getStartPosFromCol(newCol) : getEndPosFromCol(newCol);

	if (newMarkerPos < 0) {
		newMarkerPos = 0;
	}

	if (offset >= 0) {
		if (markerType == MarkerType::END && shouldAllowExtraScrollRight()) {
			if (newMarkerPos < oldPos) {
				return;
			}
		}
		else {
			if (newMarkerPos > waveformBasicNavigator.sample->lengthInSamples) {
				newMarkerPos = waveformBasicNavigator.sample->lengthInSamples;
			}
		}
	}

	writeValue(newMarkerPos);

	// If marker was on-screen...
	if (oldCol >= 0 && oldCol < kDisplayWidth) {

		getColsOnScreen(cols);
		// It might have changed, and despite having a newCol variable above, that's only our desired value - we might have run into the end of the sample
		newCol = cols[util::to_underlying(markerType)].colOnScreen;

		// But isn't anymore...
		if (newCol < 0 || newCol >= kDisplayWidth) {

			// Move scroll
			waveformBasicNavigator.xScroll += waveformBasicNavigator.xZoom * offset;

			if (waveformBasicNavigator.xScroll < 0) {
				waveformBasicNavigator.xScroll = 0; // Shouldn't happen...
			}

			recordScrollAndZoom();
		}
	}

	blinkInvisible = false;

	uiNeedsRendering(this, 0xFFFFFFFF, 0);
#if HAVE_OLED
	renderUIsForOled();
#else
	displayText();
#endif
}

ActionResult SampleMarkerEditor::padAction(int x, int y, int on) {

	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (currentUIMode != UI_MODE_AUDITIONING) { // Don't want to do this while auditioning - too easy to do by mistake
		ActionResult soundEditorResult = soundEditor.potentialShortcutPadAction(x, y, on);
		if (soundEditorResult != ActionResult::NOT_DEALT_WITH) {
			return soundEditorResult;
		}
	}

	// Audition pads - pass to UI beneath
	if (x == kDisplayWidth + 1) {
		if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
			instrumentClipView.padAction(x, y, on);
		}
		return ActionResult::DEALT_WITH;
	}

	// Mute pads
	else if (x == kDisplayWidth) {
		if (on && !currentUIMode) {
			exitUI();
		}
	}

	else {

		// Press down
		if (on) {

			if (currentUIMode && currentUIMode != UI_MODE_AUDITIONING
			    && currentUIMode != UI_MODE_HOLDING_SAMPLE_MARKER) {
				return ActionResult::DEALT_WITH;
			}

			MarkerColumn cols[kNumMarkerTypes];
			getColsOnScreen(cols);

			// See which one we pressed
			MarkerType markerPressed = MarkerType::NONE;
			for (int m = 0; m < kNumMarkerTypes; m++) {
				if (cols[m].colOnScreen == x) {
					if (markerPressed != MarkerType::NONE) {
						// Get out if there are two markers occupying the same col we pressed
						return ActionResult::DEALT_WITH;
					}
					markerPressed = MarkerType{m};
				}
			}

			int32_t value;

			// If already holding a marker down...
			if (currentUIMode == UI_MODE_HOLDING_SAMPLE_MARKER) {

				if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {
					// See which one we were holding down
					MarkerType markerHeld = MarkerType::NONE;
					for (int m = 0; m < kNumMarkerTypes; m++) {
						if (cols[m].colOnScreen == pressX) {
							markerHeld = MarkerType{m};
						}
					}

					// -----------------------------------------------------------
					MarkerType newMarkerType;
					int32_t newValue;

					// If start or end, add a loop point
					if (markerHeld == MarkerType::START) {

						// Unless we actually just tapped the already existing loop point
						if (x == cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen) {
							markerType = MarkerType::LOOP_START;
							value = 0;
							writeValue(value);
							markerType = MarkerType::START; // Switch it back
							goto doRender;
						}

						// Limit position
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (getCurrentMultisampleRange()->sampleHolder.loopEndPos
						    && cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}

						newMarkerType = MarkerType::LOOP_START;
						newValue = getStartPosFromCol(x);

ensureNotPastSampleLength:
						// Loop start and end points are not allowed to be further right than the sample waveform length
						if (newValue >= waveformBasicNavigator.sample->lengthInSamples) {
							return ActionResult::DEALT_WITH;
						}
						markerType = newMarkerType;
						value = newValue;
					}
					else if (markerHeld == MarkerType::END) {

						// Unless we actually just tapped the already existing loop point
						if (x == cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen) {
							markerType = MarkerType::LOOP_END;
							value = 0;
							writeValue(value);
							markerType = MarkerType::END; // Switch it back
							goto doRender;
						}

						// Limit position
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}

						newMarkerType = MarkerType::LOOP_END;
						newValue = getEndPosFromCol(x);

						goto ensureNotPastSampleLength;
					}

					// Or if a loop point and they pressed the end marker, remove the loop point
					else if (markerHeld == MarkerType::LOOP_START) {
						if (x == cols[util::to_underlying(MarkerType::START)].colOnScreen) {
							value = 0;
							writeValue(value);
							markerType = MarkerType::START;

exitAfterRemovingLoopMarker:
							currentUIMode = UI_MODE_NONE;
							blinkInvisible = true;
							goto doRender;
						}
						else {
							return ActionResult::DEALT_WITH;
						}
					}
					else if (markerHeld == MarkerType::LOOP_END) {
						if (x == cols[util::to_underlying(MarkerType::END)].colOnScreen) {
							value = 0;
							writeValue(value);
							markerType = MarkerType::END;

							goto exitAfterRemovingLoopMarker;
						}
						else {
							return ActionResult::DEALT_WITH;
						}
					}

					currentUIMode = UI_MODE_NONE;
					blinkInvisible = false;
					goto doWriteValue;
				}
			}

			// Or if user not already holding a marker down...
			else {

				// If we tapped a marker...
				if (markerPressed >= MarkerType::START) {
					blinkInvisible = (markerType != markerPressed);
					markerType = markerPressed;
					currentUIMode = UI_MODE_HOLDING_SAMPLE_MARKER;
					pressX = x;
					pressY = y;
				}

				// Otherwise, move the current marker to where we tapped
				else {

					// Make sure it doesn't go past any other markers it shouldn't
					if (markerType == MarkerType::START) {
						if (cols[util::to_underlying(MarkerType::LOOP_START)].pos
						    && cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_END)].pos
						    && cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
					}

					else if (markerType == MarkerType::LOOP_START) {
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_END)].pos
						    && cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
					}

					else if (markerType == MarkerType::LOOP_END) {
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
						if (cols[util::to_underlying(MarkerType::END)].colOnScreen <= x) {
							return ActionResult::DEALT_WITH;
						}
					}

					else if (markerType == MarkerType::END) {
						if (cols[util::to_underlying(MarkerType::START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH;
						}
						if (cols[util::to_underlying(MarkerType::LOOP_START)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
						if (cols[util::to_underlying(MarkerType::LOOP_END)].colOnScreen >= x) {
							return ActionResult::DEALT_WITH; // Will be a big negative number if inactive
						}
					}

					value = (markerType < MarkerType::LOOP_END) ? getStartPosFromCol(x) : getEndPosFromCol(x);

					{
						uint32_t lengthInSamples = waveformBasicNavigator.sample->lengthInSamples;

						// Only the END marker, and only in some cases, is allowed to be further right than the waveform length
						if (markerType == MarkerType::END && shouldAllowExtraScrollRight()) {
							if (x > cols[util::to_underlying(markerType)].colOnScreen
							    && value < cols[util::to_underlying(markerType)].pos) {
								return ActionResult::DEALT_WITH; // Probably not actually necessary
							}
							if (value > lengthInSamples && value < lengthInSamples + waveformBasicNavigator.xZoom) {
								value = lengthInSamples;
							}
						}

						else {
							if (value > lengthInSamples) {
								value = lengthInSamples;
							}
						}
					}

					blinkInvisible = false;

doWriteValue:
					writeValue(value);
				}
			}

doRender:
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
#if HAVE_OLED
			renderUIsForOled();
#else
			displayText();
#endif
		}

		// Release press
		else {
			if (currentUIMode == UI_MODE_HOLDING_SAMPLE_MARKER) {
				if (x == pressX && y == pressY) {
					currentUIMode = UI_MODE_NONE;
				}
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SampleMarkerEditor::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	// Back button
	if (b == BACK) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUI();
		}
	}

	// Horizontal encoder button
	else if (b == X_ENC) {
		if (on) {
			if (isNoUIModeActive() || isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
				currentUIMode |= UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON;
			}
		}

		else {
			exitUIMode(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON);
		}
	}

	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

void SampleMarkerEditor::exitUI() {
	numericDriver.setNextTransitionDirection(-1);
	close();
}

static const uint32_t zoomUIModes[] = {UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON, UI_MODE_AUDITIONING, 0};

ActionResult SampleMarkerEditor::horizontalEncoderAction(int offset) {

	// We're quite likely going to need to read the SD card to do either scrolling or zooming
	if (sdRoutineLock) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	MarkerColumn* colsToSend = NULL;
	MarkerColumn cols[kNumMarkerTypes];
	if (markerType != MarkerType::NONE) {
		getColsOnScreen(cols);
		colsToSend = cols;
	}

	bool success = false;

	// Zoom
	if (isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)) {
		if (isUIModeWithinRange(zoomUIModes)) {
			success = waveformBasicNavigator.zoom(offset, shouldAllowExtraScrollRight(), colsToSend, markerType);
			if (success) {
				uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
			}
		}
	}

	// Scroll
	else if (isUIModeWithinRange(&zoomUIModes[1])) { // Allow during auditioning only
		success = waveformBasicNavigator.scroll(offset, shouldAllowExtraScrollRight(), colsToSend);

		if (success) {
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
	}

	if (success) {
		recordScrollAndZoom();
		blinkInvisible = false;
	}
	return ActionResult::DEALT_WITH;
}

// Just for the blinking marker I think
ActionResult SampleMarkerEditor::timerCallback() {

	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	int x = cols[util::to_underlying(markerType)].colOnScreen;
	if (x < 0 || x >= kDisplayWidth) {
		return ActionResult::
		    DEALT_WITH; // Shouldn't happen, but let's be safe - and not set the timer again if it's offscreen
	}

	blinkInvisible = !blinkInvisible;

	// Clear col
	for (int y = 0; y < kDisplayHeight; y++) {
		memset(PadLEDs::image[y][x], 0, 3);
	}

	renderForOneCol(x, PadLEDs::image, cols);

	PadLEDs::sortLedsForCol(x);
	uartFlushIfNotSending(UART_ITEM_PIC_PADS);

	uiTimerManager.setTimer(TIMER_UI_SPECIFIC, kSampleMarkerBlinkTime);

	return ActionResult::DEALT_WITH;
}

ActionResult SampleMarkerEditor::verticalEncoderAction(int offset, bool inCardRoutine) {
	if (Buttons::isShiftButtonPressed() || Buttons::isButtonPressed(hid::button::X_ENC)
	    || currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return ActionResult::DEALT_WITH;
	}

	// Must say these buttons were not pressed, or else editing might take place
	ActionResult result = instrumentClipView.verticalEncoderAction(offset, inCardRoutine);

	if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
		return result;
	}

	if (getRootUI() == &keyboardScreen) {
		uiNeedsRendering(this, 0, 0xFFFFFFFF);
	}

	return result;
}

bool SampleMarkerEditor::renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (getRootUI() != &keyboardScreen) {
		return false;
	}
	return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
}

void SampleMarkerEditor::graphicsRoutine() {

#ifdef TEST_SAMPLE_LOOP_POINTS
	if (!currentlyAccessingCard && !(getNoise() >> 27)) {
		Uart::println("random change to marker -----------------------------");

		int minDistance = 1;

		uint8_t r = getRandom255();

		if (r < 64) {

			// Change start
			Uart::println("change loop start -------------------------------");

			if (!soundEditor.currentSource->reversed) {

				int newStartPos = ((uint32_t)getNoise() % (44100 * 120)) + 10 * 44100;

				if (newStartPos > soundEditor.currentMultiRange->endPos - minDistance)
					newStartPos = soundEditor.currentMultiRange->endPos - minDistance;
				if (soundEditor.currentMultiRange->loopEndPos
				    && newStartPos >= soundEditor.currentMultiRange->loopEndPos - minDistance)
					newStartPos = soundEditor.currentMultiRange->loopEndPos - minDistance;

				writeValue(newStartPos, MarkerType::START);
			}

			else {

				//writeValue(soundEditor.currentMultisampleRange->sample->lengthInSamples, MarkerType::END);

				//int newStartPos = soundEditor.currentMultisampleRange->sample->lengthInSamples;// - (((uint32_t)getNoise() % (44100 * 120)) + 10 * 44100);
				int newStartPos = soundEditor.currentMultiRange->sample->lengthInSamples
				                  - (((uint32_t)getNoise() % (44100 * 12)) + 0 * 44100);

				if (newStartPos < soundEditor.currentMultiRange->startPos + minDistance)
					newStartPos = soundEditor.currentMultiRange->startPos + minDistance;
				if (soundEditor.currentMultiRange->loopStartPos
				    && newStartPos <= soundEditor.currentMultiRange->loopStartPos + minDistance)
					newStartPos = soundEditor.currentMultiRange->loopStartPos + minDistance;

				writeValue(newStartPos, MarkerType::END);
			}
		}

		else { //if (r < 128) {
			// Change loop point

			if (!soundEditor.currentSource->reversed) {

				int newLoopEndPos;
				if (soundEditor.currentMultiRange->loopEndPos) {
					Uart::println("remove loop end -------------------------------");
					newLoopEndPos = 0;
				}
				else {
					Uart::println("set loop end -------------------------------");
					newLoopEndPos =
					    soundEditor.currentMultiRange->startPos + minDistance + ((uint32_t)getNoise() % (44100 * 1));

					if (newLoopEndPos > soundEditor.currentMultiRange->endPos)
						newLoopEndPos = soundEditor.currentMultiRange->endPos;
				}

				writeValue(newLoopEndPos, MarkerType::LOOP_END);
			}
			else {

				int newLoopEndPos;
				if (soundEditor.currentMultiRange->loopStartPos) {
					Uart::println("remove loop end -------------------------------");
					newLoopEndPos = 0;
				}
				else {
					Uart::println("set loop end -------------------------------");
					newLoopEndPos =
					    soundEditor.currentMultiRange->endPos - minDistance - ((uint32_t)getNoise() % (44100 * 1));

					if (newLoopEndPos < soundEditor.currentMultiRange->startPos)
						newLoopEndPos = soundEditor.currentMultiRange->startPos;
				}

				writeValue(newLoopEndPos, MarkerType::LOOP_START);
			}
		}

		Uart::println("end random change");
	}
#endif

	if (PadLEDs::flashCursor == FLASH_CURSOR_OFF) {
		return;
	}

	int newTickSquare = 255;

	VoiceSample* voiceSample = NULL;
	SamplePlaybackGuide* guide = NULL;

	// InstrumentClips / Samples
	if (currentSong->currentClip->type == CLIP_TYPE_INSTRUMENT) {

		if (soundEditor.currentSound->hasAnyVoices()) {

			Voice* assignedVoice = NULL;

			int ends[2];
			AudioEngine::activeVoices.getRangeForSound(soundEditor.currentSound, ends);
			for (int v = ends[0]; v < ends[1]; v++) {
				Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

				// Ensure correct MultisampleRange.
				if (thisVoice->guides[soundEditor.currentSourceIndex].audioFileHolder
				    != soundEditor.currentMultiRange->getAudioFileHolder()) {
					continue;
				}

				if (!assignedVoice || thisVoice->orderSounded > assignedVoice->orderSounded) {
					assignedVoice = thisVoice;
				}
			}

			if (assignedVoice) {
				VoiceUnisonPartSource* part = &assignedVoice->unisonParts[soundEditor.currentSound->numUnison >> 1]
				                                   .sources[soundEditor.currentSourceIndex];
				if (part && part->active) {
					voiceSample = part->voiceSample;
					guide = &assignedVoice->guides[soundEditor.currentSourceIndex];
				}
			}
		}
	}

	// AudioClips
	else {
		voiceSample = ((AudioClip*)currentSong->currentClip)->voiceSample;
		guide = &((AudioClip*)currentSong->currentClip)->guide;
	}

	if (voiceSample) {
		int samplePos = voiceSample->getPlaySample(waveformBasicNavigator.sample, guide);
		if (samplePos >= waveformBasicNavigator.xScroll) {
			newTickSquare = (samplePos - waveformBasicNavigator.xScroll) / waveformBasicNavigator.xZoom;
			if (newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, newTickSquare, kDisplayHeight);
	PadLEDs::setTickSquares(tickSquares, zeroes);
}

bool SampleMarkerEditor::shouldAllowExtraScrollRight() {

	if (markerType == MarkerType::NONE || getCurrentSampleControls()->reversed) {
		return false;
	}

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return true;
	}
	else {
		return (soundEditor.currentSource->repeatMode == SampleRepeatMode::STRETCH);
	}
}

void SampleMarkerEditor::renderForOneCol(int xDisplay,
                                         uint8_t thisImage[kDisplayHeight][kDisplayWidth + kSideBarWidth][3],
                                         MarkerColumn* cols) {

	waveformRenderer.renderOneCol(waveformBasicNavigator.sample, xDisplay, thisImage,
	                              &waveformBasicNavigator.renderData);

	renderMarkersForOneCol(xDisplay, thisImage, cols);
}

void SampleMarkerEditor::renderMarkersForOneCol(int xDisplay,
                                                uint8_t thisImage[kDisplayHeight][kDisplayWidth + kSideBarWidth][3],
                                                MarkerColumn* cols) {

	if (markerType != MarkerType::NONE) {

		bool reversed = getCurrentSampleControls()->reversed;

		MarkerType greenMarker = reversed ? MarkerType::END : MarkerType::START;
		MarkerType cyanMarker = reversed ? MarkerType::LOOP_END : MarkerType::LOOP_START;
		MarkerType purpleMarker = reversed ? MarkerType::LOOP_START : MarkerType::LOOP_END;
		MarkerType redMarker = reversed ? MarkerType::START : MarkerType::END;

		unsigned int markersActiveHere = 0;
		for (int m = 0; m < kNumMarkerTypes; m++) {
			markersActiveHere |=
			    (xDisplay == cols[m].colOnScreen && (!blinkInvisible || markerType != static_cast<MarkerType>(m))) << m;
		}

		if (markersActiveHere) {
			auto currentMarkerType = MarkerType{0};

			for (int y = 0; y < kDisplayHeight; y++) {
				while (!(markersActiveHere & (1 << util::to_underlying(currentMarkerType)))) {
					currentMarkerType =
					    static_cast<MarkerType>((util::to_underlying(currentMarkerType) + 1) % kNumMarkerTypes);
				}

				int existingColourAmount = thisImage[y][xDisplay][0];

				// Green
				if (currentMarkerType == greenMarker) {
					thisImage[y][xDisplay][0] >>= 2;
					thisImage[y][xDisplay][1] = 255 - existingColourAmount * 2;
					thisImage[y][xDisplay][2] >>= 2;
				}

				// Cyan
				else if (currentMarkerType == cyanMarker) {
					thisImage[y][xDisplay][0] >>= 1;
					thisImage[y][xDisplay][1] = 140 - existingColourAmount;
					thisImage[y][xDisplay][2] = 140 - existingColourAmount;
				}

				// Purple
				else if (currentMarkerType == purpleMarker) {
					thisImage[y][xDisplay][0] = 140 - existingColourAmount;
					thisImage[y][xDisplay][1] >>= 1;
					thisImage[y][xDisplay][2] = 140 - existingColourAmount;
				}

				// Red
				else if (currentMarkerType == redMarker) {
					thisImage[y][xDisplay][0] = 255 - existingColourAmount * 2;
					thisImage[y][xDisplay][1] >>= 2;
					thisImage[y][xDisplay][2] >>= 2;
				}

				currentMarkerType =
				    static_cast<MarkerType>((util::to_underlying(currentMarkerType) + 1) % kNumMarkerTypes);
			}
		}
	}
}

#if HAVE_OLED
void SampleMarkerEditor::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	uint32_t markerPosSamples = cols[util::to_underlying(markerType)].pos;

	char const* markerTypeText;
	switch (markerType) {
	case MarkerType::START:
		markerTypeText = "Start point";
		break;

	case MarkerType::END:
		markerTypeText = "End point";
		break;

	case MarkerType::LOOP_START:
		markerTypeText = "Loop start";
		break;

	case MarkerType::LOOP_END:
		markerTypeText = "Loop end";
		break;

	default:
		__builtin_unreachable();
	}

	OLED::drawScreenTitle(markerTypeText);

	int smallTextSpacingX = kTextSpacingX;
	int smallTextSizeY = kTextSpacingY;
	int yPixel = OLED_MAIN_TOPMOST_PIXEL + 17;
	int xPixel = 1;

	uint32_t hours = 0;
	uint32_t minutes = 0;
	uint64_t hundredmilliseconds =
	    (uint64_t)markerPosSamples * 100000 / waveformBasicNavigator.sample->sampleRate; // mSec

	if (hundredmilliseconds >= 6000000) {
		minutes = hundredmilliseconds / 6000000;
		hundredmilliseconds -= (uint64_t)minutes * 6000000;

		if (minutes >= 60) {
			hours = minutes / 60;
			minutes -= hours * 60;

			char buffer[12];
			intToString(hours, buffer);
			OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
			                 smallTextSizeY);
			xPixel += strlen(buffer) * smallTextSpacingX;

			OLED::drawChar('h', smallTextSpacingX, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
			               smallTextSizeY);
			xPixel += smallTextSpacingX * 2;
		}

		char buffer[12];
		intToString(minutes, buffer);
		OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
		xPixel += strlen(buffer) * smallTextSpacingX;

		OLED::drawChar('m', smallTextSpacingX, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
		               smallTextSizeY);
		xPixel += smallTextSpacingX * 2;
	}
	else {
		goto printSeconds;
	}

	if (hundredmilliseconds) {
printSeconds:
		int numDecimalPlaces;

		// Maybe we just want to display millisecond resolution (that's S with 3 decimal places)...
		if (hours || minutes || hundredmilliseconds >= 100000) {
			hundredmilliseconds /= 100;
			numDecimalPlaces = 3;
		}

		// Or, display milliseconds with 2 decimal places - very fine resolution.
		else {
			numDecimalPlaces = 2;
		}

		char buffer[13];
		intToString(hundredmilliseconds, buffer, numDecimalPlaces + 1);
		int length = strlen(buffer);
		memmove(&buffer[length - numDecimalPlaces + 1], &buffer[length - numDecimalPlaces], numDecimalPlaces + 1);
		buffer[length - numDecimalPlaces] = '.';

		OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
		xPixel += (length + 1) * smallTextSpacingX;

		if (hours || minutes) {
			OLED::drawChar('s', xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
		}
		else {
			xPixel += smallTextSpacingX;
			char const* secString = (numDecimalPlaces == 2) ? "msec" : "sec";
			OLED::drawString(secString, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX,
			                 smallTextSizeY);
		}
	}

	yPixel += 11;

	// Sample count
	xPixel = 1;

	OLED::drawChar('(', xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX;

	char buffer[12];
	intToString(markerPosSamples, buffer);
	OLED::drawString(buffer, xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX * (strlen(buffer) + 1);

	OLED::drawString("smpl)", xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS, smallTextSpacingX, smallTextSizeY);
	xPixel += smallTextSpacingX * 6;
}

#else

void SampleMarkerEditor::displayText() {

	MarkerColumn cols[kNumMarkerTypes];
	getColsOnScreen(cols);

	// Draw decimal number too
	uint32_t markerPos = cols[util::to_underlying(markerType)].pos;
	int32_t number = (uint64_t)markerPos * 1000 / waveformBasicNavigator.sample->sampleRate; // mSec
	int numDecimals = 3;

	while (number > 9999) {
		number /= 10;
		numDecimals--;
	}

	int drawDot = 3 - numDecimals;
	if (drawDot >= kNumericDisplayLength) {
		drawDot = 255;
	}

	char buffer[5];
	intToString(number, buffer, numDecimals + 1);

	numericDriver.setText(buffer, true, drawDot);
}
#endif

bool SampleMarkerEditor::renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
                                        uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                        bool drawUndefinedArea) {
	if (!image) {
		return true;
	}

	waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
	                                  waveformBasicNavigator.xZoom, image, &waveformBasicNavigator.renderData);

	if (markerType != MarkerType::NONE) {
		MarkerColumn cols[kNumMarkerTypes];
		getColsOnScreen(cols);

		for (int xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
			renderMarkersForOneCol(xDisplay, image, cols);
		}

		if (cols[util::to_underlying(markerType)].colOnScreen >= 0
		    && cols[util::to_underlying(markerType)].colOnScreen < kDisplayWidth) {
			uiTimerManager.setTimer(TIMER_UI_SPECIFIC, kSampleMarkerBlinkTime);
		}
	}

	return true;
}
