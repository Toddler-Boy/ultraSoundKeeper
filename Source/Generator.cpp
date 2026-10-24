#include <algorithm>

#include "Generator.h"

//-----------------------------------------------------------------------------

namespace
{
	constexpr auto	pi = 3.14159265358979323846;
	constexpr auto	fadeSeconds = 0.01;

	// sin of a phase in [0, 2 pi): an odd polynomial to x^11 on [-pi/2, pi/2], the rest by symmetry, error under 1e-7
	double sine ( double phase )
	{
		auto	x = phase - pi;
		if ( x > pi / 2 )
			x = pi - x;
		else if ( x < -pi / 2 )
			x = -pi - x;

		const auto	x2 = x * x;
		return -x * ( 1 - x2 * ( 1.0 / 6 ) * ( 1 - x2 * ( 1.0 / 20 ) * ( 1 - x2 * ( 1.0 / 42 ) * ( 1 - x2 * ( 1.0 / 72 ) * ( 1 - x2 * ( 1.0 / 110 ) ) ) ) ) );
	}

	// One-pole smoothing of the gain towards the volume
	double nextGain ( generator::State& state, double target, int sampleRate )
	{
		state.gain += ( target - state.gain ) / ( fadeSeconds * sampleRate );
		return state.gain;
	}

	// Uniform -1 to 1 (the LCG from musl)
	double white ( uint64_t& random )
	{
		random = random * 6364136223846793005ULL + 1;
		return double ( ( random >> 32 ) & 0x7FFFFFFF ) / double ( 0x7FFFFFFF ) * 2.0 - 1.0;
	}

	// Paul Kellet's filter, about -3 dB per octave
	double pink ( std::array<double, 7>& s, double white )
	{
		s[ 0 ] = 0.99886 * s[ 0 ] + white * 0.0555179;
		s[ 1 ] = 0.99332 * s[ 1 ] + white * 0.0750759;
		s[ 2 ] = 0.96900 * s[ 2 ] + white * 0.1538520;
		s[ 3 ] = 0.86650 * s[ 3 ] + white * 0.3104856;
		s[ 4 ] = 0.55000 * s[ 4 ] + white * 0.5329522;
		s[ 5 ] = -0.7616 * s[ 5 ] - white * 0.0168980;
		const auto	value = ( s[ 0 ] + s[ 1 ] + s[ 2 ] + s[ 3 ] + s[ 4 ] + s[ 5 ] + s[ 6 ] + white * 0.5362 ) * 0.11;
		s[ 6 ] = white * 0.115926;
		return value;
	}

	void fill ( float* frame, int channels, float value )
	{
		for ( auto c = 0; c < channels; ++c )
			frame[ c ] = value;
	}
}
//-----------------------------------------------------------------------------

bool generator::render ( State& state, const config::Settings& settings, const Format& format, float* buffer, int frames )
{
	const auto	volume = settings.volumeTenths / 1000.0;

	switch ( settings.mode )
	{
		case config::Mode::openOnly:
		case config::Mode::silence:
			return false;

		case config::Mode::fluctuate:
		{
			// The smallest step the output resolves, up for one frame and down for the next, frequency times a second
			const auto	step = format.output16Bit ? 1.0f / 32767 : 1.0f / 8388607;
			const auto	every = std::max ( uint64_t ( format.sampleRate / std::max ( settings.frequencyHz, 1 ) ), uint64_t ( 2 ) );

			for ( auto i = 0; i < frames; ++i, ++state.frame )
			{
				const auto	pulse = state.frame % every == 0;
				const auto	negative = ( state.frame / every ) & 1;
				fill ( buffer + i * format.channels, format.channels, pulse ? ( negative ? -step : step ) : 0.0f );
			}
			return true;
		}

		case config::Mode::sine:
		{
			const auto	increment = 2 * pi * std::min ( double ( settings.frequencyHz ), format.sampleRate / 2.0 ) / format.sampleRate;

			for ( auto i = 0; i < frames; ++i )
			{
				const auto	gain = nextGain ( state, volume, format.sampleRate );
				fill ( buffer + i * format.channels, format.channels, float ( sine ( state.phase ) * gain ) );

				state.phase += increment;
				if ( state.phase >= 2 * pi )
					state.phase -= 2 * pi;
			}
			return true;
		}

		case config::Mode::whiteNoise:
		case config::Mode::pinkNoise:
			for ( auto i = 0; i < frames; ++i )
			{
				const auto	gain = nextGain ( state, volume, format.sampleRate );
				auto		value = white ( state.random );
				if ( settings.mode == config::Mode::pinkNoise )
					value = pink ( state.pink, value );

				fill ( buffer + i * format.channels, format.channels, float ( value * gain ) );
			}
			return true;
	}

	return false;
}
//-----------------------------------------------------------------------------
