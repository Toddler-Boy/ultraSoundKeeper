#include <array>
#include <cstdint>

#include "GUI_TrayIcon.h"

//-----------------------------------------------------------------------------

namespace
{
	// The design lives in a 512 unit square, drawn supersampled and averaged down to alpha
	constexpr auto	designSize = 512.0f;
	constexpr auto	supersample = 4;

	struct Dib
	{
		HBITMAP		bitmap = nullptr;
		uint32_t*	pixels = nullptr;
	};

	Dib createDib ( int width, int height )
	{
		auto	info = BITMAPINFO {};
		info.bmiHeader.biSize = sizeof ( info.bmiHeader );
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		auto		bits = static_cast<void*> ( nullptr );
		const auto	bitmap = CreateDIBSection ( nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0 );
		return { bitmap, static_cast<uint32_t*> ( bits ) };
	}

	// Positive values only
	int roundToInt ( float value )
	{
		return int ( value + 0.5f );
	}

	// White on black into the supersampled scratch, grid units in, pixels out through scale.
	// Six rounded bars of a waveform, centred; paused flattens them all to the shortest
	void drawDesign ( HDC dc, bool paused, float scale )
	{
		constexpr auto	barWidth = 56.0f;
		constexpr auto	barPitch = 84.0f;
		constexpr auto	heights = std::array { 110.0f, 400.0f, 230.0f, 330.0f, 180.0f, 110.0f };

		const auto	at = [ & ] ( float units ) { return roundToInt ( units * scale ); };

		SelectObject ( dc, GetStockObject ( NULL_PEN ) );
		SelectObject ( dc, GetStockObject ( WHITE_BRUSH ) );

		const auto	left = ( designSize - ( barPitch * ( heights.size () - 1 ) + barWidth ) ) / 2;
		for ( auto i = 0u; i < heights.size (); ++i )
		{
			const auto	height = paused ? heights.front () : heights[ i ];
			const auto	x = left + barPitch * i;
			const auto	y = ( designSize - height ) / 2;
			RoundRect ( dc, at ( x ), at ( y ), at ( x + barWidth ) + 1, at ( y + height ) + 1, at ( barWidth ), at ( barWidth ) );
		}
	}
}
//-----------------------------------------------------------------------------

HICON trayIcon::create ( bool paused, int size, bool lightTaskbar )
{
	const auto	scratchSize = size * supersample;
	const auto	scratch = createDib ( scratchSize, scratchSize );
	const auto	dc = CreateCompatibleDC ( nullptr );
	const auto	oldBitmap = SelectObject ( dc, scratch.bitmap );
	drawDesign ( dc, paused, float ( scratchSize ) / designSize );
	GdiFlush ();
	SelectObject ( dc, oldBitmap );
	DeleteDC ( dc );

	const auto	colour = createDib ( size, size );
	const auto	rgb = lightTaskbar ? 0x000000u : 0xFFFFFFu;

	for ( auto y = 0; y < size; ++y )
		for ( auto x = 0; x < size; ++x )
		{
			auto	sum = 0u;
			for ( auto sy = 0; sy < supersample; ++sy )
				for ( auto sx = 0; sx < supersample; ++sx )
					sum += scratch.pixels[ ( y * supersample + sy ) * scratchSize + x * supersample + sx ] & 0xFF;

			const auto	alpha = sum / ( supersample * supersample );
			if ( alpha != 0 )
				colour.pixels[ y * size + x ] = ( alpha << 24 ) | rgb;
		}

	DeleteObject ( scratch.bitmap );

	// An all-zero mask: the colour bitmap's alpha decides
	const auto	maskBytes = SIZE_T ( ( size + 15 ) / 16 * 2 * size );
	const auto	maskBits = HeapAlloc ( GetProcessHeap (), HEAP_ZERO_MEMORY, maskBytes );
	const auto	mask = CreateBitmap ( size, size, 1, 1, maskBits );
	HeapFree ( GetProcessHeap (), 0, maskBits );

	auto		info = ICONINFO { TRUE, 0, 0, mask, colour.bitmap };
	const auto	icon = CreateIconIndirect ( &info );

	DeleteObject ( mask );
	DeleteObject ( colour.bitmap );
	return icon;
}
//-----------------------------------------------------------------------------
