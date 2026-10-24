#pragma once

#include <array>
#include <cstdint>

#include "Config.h"

//-----------------------------------------------------------------------------

namespace generator
{
	struct Format
	{
		int		sampleRate;
		int		channels;
		bool	output16Bit;	// Fluctuate's smallest step depends on the output's resolution
	};

	// All state of one running generator, zero is the start
	struct State
	{
		uint64_t				frame;
		double					phase;	// of the sine, 0 to 2 pi
		double					gain;
		std::array<double, 7>	pink;
		uint64_t				random;
	};

	// Writes frames of interleaved float32, the same value on every channel; false = nothing written, the buffer is silence.
	// Mode, frequency and volume are read from settings on every call, the gain follows the volume with a 10 ms time constant
	[[ nodiscard ]] bool render ( State& state, const config::Settings& settings, const Format& format, float* buffer, int frames );
}
//-----------------------------------------------------------------------------
