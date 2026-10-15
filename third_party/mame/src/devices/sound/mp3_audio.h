// license:BSD-3-Clause
// copyright-holders:windyfairy
/***************************************************************************

    MP3 audio decoder

***************************************************************************/

#ifndef MAME_SOUND_MP3_AUDIO_H
#define MAME_SOUND_MP3_AUDIO_H

#pragma once

#include <stdint.h>

class mp3_audio
{
public:
	mp3_audio(const void *base);
	~mp3_audio();

	void register_save(device_t &host);

	bool decode_buffer(int &pos, int limit, short *output, int &output_samples, int &sample_rate, int &channels);

	void clear();

	// HIRAKU: this decoder's state made a copy of `o`'s (not its data: each decodes from its own
	// base), so mas3507d's hk_predict() can decode ahead on a copy, leaving the real one as it was
	void hk_copy_state(const mp3_audio &o);
	bool hk_found_stream() const { return m_found_stream; }

private:
	struct mp3dec_local_t;

	const uint8_t *base;

	std::unique_ptr<mp3dec_local_t> dec;
	bool m_found_stream;
};

#endif
