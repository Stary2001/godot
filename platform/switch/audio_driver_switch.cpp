/*************************************************************************/
/*  audio_driver_switch.cpp                                                */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "audio_driver_switch.h"

#include "core/os/os.h"
#include "core/project_settings.h"

#include <malloc.h>
#include <errno.h>

static const AudioRendererConfig arConfig =
    {
        .output_rate     = AudioRendererOutputRate_48kHz,
        .num_voices      = 24,
        .num_effects     = 0,
        .num_sinks       = 1,
        .num_mix_objs    = 1,
        .num_mix_buffers = 2,
    };

Error AudioDriverSwitch::init_device() {
	
	int latency = GLOBAL_DEF("audio/output_latency", DEFAULT_OUTPUT_LATENCY);
	mix_rate = GLOBAL_DEF("audio/mix_rate", 48000);
	channels = 2;
	speaker_mode = SPEAKER_MODE_STEREO;
	buffer_size = closest_power_of_2(latency * mix_rate / 1000);
	samples_in.resize(buffer_size * channels);
	samples_out.resize(buffer_size * channels);

	Result res = audrenInitialize(&arConfig);
    printf("audrenInitialize: %x\n", res);
    res = audrvCreate(&audren_driver, &arConfig, 2);
    printf("audrenInitialize: %x\n", res);
    
	audren_buffer_size = (sizeof(int16_t) * buffer_size * channels);
	audren_pool_size = ((audren_buffer_size * 2) + 0xFFF) &~ 0xFFF;
	audren_pool_ptr = memalign(0x1000, audren_pool_size);
	
	for (int i = 0; i < 2; i++) {
		audren_buffers[i] = {0};
        audren_buffers[i].data_raw = audren_pool_ptr;
        audren_buffers[i].size = audren_buffer_size * 2;
        audren_buffers[i].start_sample_offset = i * buffer_size;
        audren_buffers[i].end_sample_offset = audren_buffers[i].start_sample_offset + buffer_size;
    }
    
    int mpid = audrvMemPoolAdd(&audren_driver, audren_pool_ptr, audren_pool_size);
	audrvMemPoolAttach(&audren_driver, mpid);

	static const u8 sink_channels[] = { 0, 1 };
	audrvDeviceSinkAdd(&audren_driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

	res = audrvUpdate(&audren_driver);
	printf("audrvUpdate: %x\n", res);

	res = audrenStartAudioRenderer();
	printf("audrenStartAudioRenderer: %x\n", res);

	audrvVoiceInit(&audren_driver, 0, channels, PcmFormat_Int16, mix_rate);
	audrvVoiceSetDestinationMix(&audren_driver, 0, AUDREN_FINAL_MIX_ID);
	if (channels == 1) {
        audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 0, 1);
    } else {
        audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&audren_driver, 0, 0.0f, 0, 1);
        audrvVoiceSetMixFactor(&audren_driver, 0, 0.0f, 1, 0);
        audrvVoiceSetMixFactor(&audren_driver, 0, 1.0f, 1, 1);
    }

	return OK;
}

Error AudioDriverSwitch::init() {
	
	active = false;
	thread_exited = false;
	exit_thread = false;

	Error err = init_device();
	if (err == OK) {
		mutex = Mutex::create();
		thread = Thread::create(AudioDriverSwitch::thread_func, this);
	}

	return err;
}

void AudioDriverSwitch::thread_func(void *p_udata) {
	
	AudioDriverSwitch *ad = (AudioDriverSwitch *)p_udata;

	svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2B);

	while (!ad->exit_thread) {

		ad->lock();
		ad->start_counting_ticks();

		if (!ad->active) {
			for (unsigned int i = 0; i < ad->buffer_size * ad->channels; i++) {
				ad->samples_out.write[i] = 0;
			}
		} else {
			ad->audio_server_process(ad->buffer_size, ad->samples_in.ptrw());
			for (unsigned int i = 0; i < ad->buffer_size * ad->channels; i++) {
				ad->samples_out.write[i] = ad->samples_in[i] >> 16;
			}
		}
		
		int free_buffer = -1;
		for (int i = 0; i < 2; i++) {
			if (ad->audren_buffers[i].state == AudioDriverWaveBufState_Free
				|| ad->audren_buffers[i].state == AudioDriverWaveBufState_Done) {
				free_buffer = i;
				break;
			}
		}

		if (free_buffer >= 0) {
			uint8_t *ptr = (uint8_t *) ad->audren_pool_ptr + (free_buffer * ad->audren_buffer_size);
			memcpy(ptr, ad->samples_out.ptr(), ad->audren_buffer_size);
			armDCacheFlush(ptr, ad->audren_buffer_size);
			audrvVoiceAddWaveBuf(&ad->audren_driver, 0, &ad->audren_buffers[free_buffer]);
			if (!audrvVoiceIsPlaying(&ad->audren_driver, 0)) {
				audrvVoiceStart(&ad->audren_driver, 0);
			}
			audrvUpdate(&ad->audren_driver);
			audrenWaitFrame();
			while (ad->audren_buffers[free_buffer].state != AudioDriverWaveBufState_Playing) {
				audrvUpdate(&ad->audren_driver);
			}
		} else {
			//printf("aud: no free buffer\n");
			ad->stop_counting_ticks();
			ad->unlock();
			OS::get_singleton()->delay_usec(1000);
			ad->lock();
			ad->start_counting_ticks();
		}

		ad->stop_counting_ticks();
		ad->unlock();
	}

	ad->thread_exited = true;
}

void AudioDriverSwitch::start() {

	active = true;
}

int AudioDriverSwitch::get_mix_rate() const {

	return mix_rate;
}

AudioDriver::SpeakerMode AudioDriverSwitch::get_speaker_mode() const {

	return speaker_mode;
}

Array AudioDriverSwitch::get_device_list() {

	Array list;
	list.push_back("Default");
	return list;
}

String AudioDriverSwitch::get_device() {

	return device_name;
}

void AudioDriverSwitch::set_device(String device) {

	lock();
	new_device = device;
	unlock();
}

void AudioDriverSwitch::lock() {

	if (!thread || !mutex)
		return;
	mutex->lock();
}

void AudioDriverSwitch::unlock() {

	if (!thread || !mutex)
		return;
	mutex->unlock();
}

void AudioDriverSwitch::finish() {

	if (thread) {
		exit_thread = true;
		Thread::wait_to_finish(thread);

		memdelete(thread);
		thread = NULL;

		if (mutex) {
			memdelete(mutex);
			mutex = NULL;
		}
	}

	audrvClose(&audren_driver);
	audrenExit();
}

AudioDriverSwitch::AudioDriverSwitch() :
		thread(NULL),
		mutex(NULL),
		device_name("Default"),
		new_device("Default") {
}

AudioDriverSwitch::~AudioDriverSwitch() {
}
