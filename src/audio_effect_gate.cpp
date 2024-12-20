/**************************************************************************/
/*  audio_effect_gate.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_effect_gate.h"

//#include <godot_cpp/servers/audio_server.h>

#include <godot_cpp/classes/audio_server.hpp>


namespace godot{

void AudioEffectGateInstance::_bind_methods() {
/*
	ClassDB::bind_method(D_METHOD("set_threshold_db", "threshold_db"), &AudioEffectGate::set_threshold_db);
	ClassDB::bind_method(D_METHOD("get_threshold_db"), &AudioEffectGate::get_threshold_db);

	ClassDB::bind_method(D_METHOD("set_attack_ms", "attack_ms"), &AudioEffectGate::set_attack_ms);
	ClassDB::bind_method(D_METHOD("get_attack_ms"), &AudioEffectGate::get_attack_ms);

	ClassDB::bind_method(D_METHOD("set_hold_ms", "hold_ms"), &AudioEffectGate::set_hold_ms);
	ClassDB::bind_method(D_METHOD("get_hold_ms"), &AudioEffectGate::get_hold_ms);

	ClassDB::bind_method(D_METHOD("set_release_ms", "release_ms"), &AudioEffectGate::set_release_ms);
	ClassDB::bind_method(D_METHOD("get_release_ms"), &AudioEffectGate::get_release_ms);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "threshold_db", PROPERTY_HINT_RANGE, "-100,0,0.01,suffix:dB"), "set_threshold_db", "get_threshold_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attack_ms", PROPERTY_HINT_RANGE, "1,2000,1,suffix:ms"), "set_attack_ms", "get_attack_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hold_ms", PROPERTY_HINT_RANGE, "1,2000,1,suffix:ms"), "set_hold_ms", "get_hold_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "release_ms", PROPERTY_HINT_RANGE, "1,2000,1,suffix:ms"), "set_release_ms", "get_release_ms");
	*/
}

void AudioEffectGateInstance::_process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	float sample_rate = AudioServer::get_singleton()->get_mix_rate();

	float signal_total_l_sqr = 0.f;
	float signal_total_r_sqr = 0.f;
	for (int i = 0; i < p_frame_count; i++) {
		signal_total_l_sqr += p_src_frames[i].left * p_src_frames[i].left;
		signal_total_r_sqr += p_src_frames[i].right * p_src_frames[i].right;
	}

	float rms_l = Math::sqrt(signal_total_l_sqr / p_frame_count);
	float rms_r = Math::sqrt(signal_total_r_sqr / p_frame_count);

	float rms = MAX(rms_l, rms_r);
	float db_rms = Math::linear2db(rms);

	update_gate_state(db_rms);

	for (int i = 0; i < p_frame_count; i++) {
		p_dst_frames[i] = p_src_frames[i];
		p_dst_frames[i].left *= next_envelope_value(sample_rate);
		p_dst_frames[i].right *= next_envelope_value(sample_rate);
	}
}

float AudioEffectGateInstance::next_envelope_value(float p_sample_rate) {
	float next_env_value = 0.f;
	switch (gate_state) {
		case GATE_CLOSED:
			next_env_value = 0.f;
			break;
		case GATE_ATTACK: // 0 -> 1
			next_env_value = last_envelope_value + (1000.f / p_sample_rate / base->attack_ms);
			break;
		case GATE_OPEN:
			next_env_value = 1.f;
			break;
		case GATE_HOLD:
			next_env_value = 1.f;
			samples_since_below_threshold++;
			break;
		case GATE_RELEASE: // 1 -> 0
			next_env_value = last_envelope_value - (1000.f / p_sample_rate / base->release_ms);
			break;
		default:
			break;
	}

	if (gate_state == GATE_ATTACK && next_env_value >= 1.f) {
		// ATTACK -> OPEN

		gate_state = GATE_OPEN;
		next_env_value = 1.f;

	} else if (gate_state == GATE_HOLD && (1000.f * samples_since_below_threshold) / p_sample_rate >= base->hold_ms) {
		// HOLD -> RELEASE

		gate_state = GATE_RELEASE;

	} else if (gate_state == GATE_RELEASE && next_env_value <= 0.f) {
		// RELEASE -> CLOSED

		gate_state = GATE_CLOSED;
		next_env_value = 0.f;
	}

	last_envelope_value = next_env_value;
	return next_env_value;
}

void AudioEffectGateInstance::update_gate_state(float p_db_rms) {
	// GATE_HOLD -> GATE_OPEN at threshold level
	if (gate_state == GATE_CLOSED || gate_state == GATE_RELEASE || gate_state == GATE_HOLD) {
		// Was closed or closing
		bool above_threshold = p_db_rms >= base->threshold_db;
		if (above_threshold) {
			if (gate_state == GATE_HOLD) {
				// HOLD -> OPEN

				gate_state = GATE_OPEN;

			} else {
				// RELEASE/CLOSED -> ATTACK

				gate_state = GATE_ATTACK;
			}
		}

	} else /* (gate_state == GATE_OPEN || gate_state == GATE_ATTACK) */ {
		bool below_threshold = p_db_rms < (base->threshold_db + base->hysteresis);
		if (below_threshold) {
			if (gate_state == GATE_ATTACK) {
				// ATTACK -> RELEASE

				gate_state = GATE_RELEASE;
			} else if (gate_state == GATE_OPEN) {
				// OPEN -> HOLD

				gate_state = GATE_HOLD;
				samples_since_below_threshold = 0;
			}
		}
	}

	// GATE_HOLD -> GATE_OPEN at hysteresis level
	// if (gate_state == GATE_CLOSED || gate_state == GATE_RELEASE) {
	// 	bool above_threshold = p_db_rms >= base->threshold_db;
	// 	if (above_threshold) {
	// 		// RELEASE/CLOSED -> ATTACK

	// 		gate_state = GATE_ATTACK;
	// 	}

	// } else /* (gate_state == GATE_OPEN || gate_state == GATE_ATTACK || gate_state == GATE_HOLD) */ {
	// 	bool below_threshold = p_db_rms < (base->threshold_db + base->hysteresis);
	// 	if (below_threshold) {
	// 		if (gate_state == GATE_ATTACK) {
	// 			// ATTACK -> RELEASE

	// 			gate_state = GATE_RELEASE;
	// 		} else if (gate_state == GATE_OPEN) {
	// 			// OPEN -> HOLD

	// 			gate_state = GATE_HOLD;
	// 			samples_since_below_threshold = 0;
	// 		}
	// 	} else {
	// 		if (gate_state == GATE_HOLD) {
	// 			// HOLD -> OPEN

	// 			gate_state = GATE_OPEN;
	// 		}
	// 	}
	// }
}

Ref<AudioEffectInstance> AudioEffectGate::_instantiate() {
	Ref<AudioEffectGateInstance> ins;
	ins.instantiate();
	ins->base = Ref<AudioEffectGate>(this);

	return ins;
}


void AudioEffectGate::set_threshold_db(float p_threshold_db) {
	threshold_db = p_threshold_db;
}

float AudioEffectGate::get_threshold_db() const {
	return threshold_db;
}

void AudioEffectGate::set_hysteresis(float p_hysteresis) {
	hysteresis = p_hysteresis;
}

float AudioEffectGate::get_hysteresis() const {
	return hysteresis;
}

void AudioEffectGate::set_attack_ms(float p_attack_ms) {
	attack_ms = p_attack_ms;
}

float AudioEffectGate::get_attack_ms() {
	return attack_ms;
}

void AudioEffectGate::set_hold_ms(float p_hold_ms) {
	hold_ms = p_hold_ms;
}

float AudioEffectGate::get_hold_ms() {
	return hold_ms;
}

void AudioEffectGate::set_release_ms(float p_release_ms) {
	release_ms = p_release_ms;
}

float AudioEffectGate::get_release_ms() {
	return release_ms;
}

void AudioEffectGate::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_threshold_db", "threshold_db"), &AudioEffectGate::set_threshold_db);
	ClassDB::bind_method(D_METHOD("get_threshold_db"), &AudioEffectGate::get_threshold_db);

	ClassDB::bind_method(D_METHOD("set_hysteresis", "hysteresis"), &AudioEffectGate::set_hysteresis);
	ClassDB::bind_method(D_METHOD("get_hysteresis"), &AudioEffectGate::get_hysteresis);

	ClassDB::bind_method(D_METHOD("set_attack_ms", "attack_ms"), &AudioEffectGate::set_attack_ms);
	ClassDB::bind_method(D_METHOD("get_attack_ms"), &AudioEffectGate::get_attack_ms);

	ClassDB::bind_method(D_METHOD("set_hold_ms", "hold_ms"), &AudioEffectGate::set_hold_ms);
	ClassDB::bind_method(D_METHOD("get_hold_ms"), &AudioEffectGate::get_hold_ms);

	ClassDB::bind_method(D_METHOD("set_release_ms", "release_ms"), &AudioEffectGate::set_release_ms);
	ClassDB::bind_method(D_METHOD("get_release_ms"), &AudioEffectGate::get_release_ms);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "threshold_db", PROPERTY_HINT_RANGE, "-100,0,0.01,suffix:dB"), "set_threshold_db", "get_threshold_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hysteresis", PROPERTY_HINT_RANGE, "-18,0,0.01,suffix:dB"), "set_hysteresis", "get_hysteresis");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attack_ms", PROPERTY_HINT_RANGE, "1,2000,1,suffix:ms"), "set_attack_ms", "get_attack_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hold_ms", PROPERTY_HINT_RANGE, "1,2000,1,suffix:ms"), "set_hold_ms", "get_hold_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "release_ms", PROPERTY_HINT_RANGE, "1,2000,1,suffix:ms"), "set_release_ms", "get_release_ms");
}

};
