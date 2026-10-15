// license:BSD-3-Clause
// copyright-holders:windyfairy
/*
* Toshiba TC9446F, Audio Digital Processor for Decode of Dolby Digital (AC-3), MPEG2 Audio
*/

#ifndef MAME_SOUND_TC9446F_H
#define MAME_SOUND_TC9446F_H

#pragma once

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_STDIO
#include "minimp3/minimp3.h"

class tc9446f_device : public device_t,
						public device_sound_interface
{
public:
	// construction/destruction
	tc9446f_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto mpeg_frame_sync_cb() { return cb_mpeg_frame_sync.bind(); }
	auto demand_cb() { return cb_demand.bind(); }

	int midio_r();
	int miack_r();

	void mimd_w(bool line);
	void mics_w(bool line);
	void midio_w(bool line);
	void mick_w(bool line);
	void audio_w(uint8_t data);

	void reset_playback();

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

	virtual void sound_stream_update(sound_stream &stream) override;

public:
	// HIRAKU: the first `n` samples (fewer: as far as is certain) the next sound_stream_update()
	// will put out, worked out ahead of it on copies; for the host's --fine-sound
	// (runtime/sys573/board.cpp). Returns how many are ready, at `l` and `r`.
	int hk_predict(int n, const float *&l, const float *&r);

protected:

private:
	int mp3_find_frame(int offset);
	void stream_update();
	void fill_buffer();
	void append_buffer(sound_stream &stream, int& pos, int scount);

	float gain_to_percentage(int val);

	devcb_write_line cb_mpeg_frame_sync;
	devcb_write_line cb_demand;

	sound_stream* stream;
	sound_stream_flags stream_flags;

	enum mode_select_t : uint8_t { SERIAL = 0, I2C };
	mode_select_t m_mode_select;
	int m_miack;

	int m_indata, m_inbits;

	int m_cmd_target_addr, m_cmd_word_count, m_cmd_cur_word;

	enum mp3_decoder_state_t : uint8_t { DECODER_STREAM_SEARCHING = 0, DECODER_STREAM_INITIAL_BUFFER, DECODER_STREAM_BUFFER_FILL, DECODER_STREAM_BUFFER };
	mp3_decoder_state_t mp3_decoder_state;
	int mp3_offset = 0;
	int mp3_offset_last = 0;
	mp3dec_t mp3_dec;

	mp3dec_frame_info_t mp3_info;
	std::array<uint8_t, 0x4000> mp3data;
	std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> samples;
	uint32_t mp3data_count, current_rate;
	uint32_t decoded_frame_count, decoded_samples;
	int32_t sample_count, samples_idx;

	// HIRAKU: hk_predict()'s copies, valid until the stream next updates (or data or a reset
	// changes what they were copied from)
	bool m_hk_valid = false, m_hk_end = false, m_hk_silent = false;
	std::vector<float> m_hk_out[2];
	std::array<uint8_t, 0x4000> m_hk_data;
	std::array<mp3d_sample_t, MINIMP3_MAX_SAMPLES_PER_FRAME> m_hk_samples;
	mp3dec_t m_hk_dec;
	mp3dec_frame_info_t m_hk_info;
	uint32_t m_hk_data_count = 0;
	int32_t m_hk_sample_count = 0, m_hk_samples_idx = 0;
};

DECLARE_DEVICE_TYPE(TC9446F, tc9446f_device)

#endif // MAME_SOUND_TC9446F_H
