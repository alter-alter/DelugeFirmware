/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "MasterCompressor.h"
#include "AudioSample.h"


MasterCompressor::MasterCompressor() {
    compressor.setSampleRate(44100);
    compressor.initRuntime();
    compressor.setAttack(10);
    compressor.setRelease(100);
    //compressor.setThresh(0.5f);// dB = 20 * log10(value) , value = 10^(dB/20)
    compressor.setThresh(0.0);//threshold (dB) 0...-69
    compressor.setRatio(1.0 / 4.00);//ratio (compression: < 1 ; expansion: > 1)
    makeup=1.0;//value;
}


void MasterCompressor::render(StereoSample* buffer, uint16_t numSamples) {


	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	if(compressor.getThresh()<0.0){
		do {
			double l= thisSample->l / 2147483648.0; //(thisSample->l < 0)? thisSample->l / 2147483648.0 : thisSample->l / 2147483647.0 ;
			double r= thisSample->r / 2147483648.0;//(thisSample->r < 0)? thisSample->r / 2147483648.0 : thisSample->r / 2147483647.0 ;
			compressor.process(l, r);

			thisSample->l = l*makeup*2147483647;//l < 0? (int)(l*2147483648) : (int)(l*2147483647);
			thisSample->r = r*makeup*2147483647;//r < 0? (int)(r*2147483648) : (int)(r*2147483647);

			if(makeup>0){

			}

		} while (++thisSample != bufferEnd);
	}


}
