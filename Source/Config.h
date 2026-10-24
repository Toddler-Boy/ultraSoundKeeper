#pragma once

//-----------------------------------------------------------------------------

// What the tray menu sets, stored under HKCU\Software\ultraSoundKeeper

namespace config
{
	enum class Mode
	{
		openOnly,
		silence,
		fluctuate,
		sine,
		whiteNoise,
		pinkNoise
	};

	enum class Outputs
	{
		primary,
		all,
		digital,
		analog
	};

	struct Settings
	{
		Mode	mode;
		Outputs	outputs;
		int		frequencyHz;	// Fluctuate: fluctuations per second, Sine: the tone
		int		volumeTenths;	// in 0.1 % steps
		bool	sleepWithDisplay;
		bool	sleepWithLock;
	};

	[[ nodiscard ]] Settings load ();
	void save ( const Settings& settings );
}
//-----------------------------------------------------------------------------
