#pragma once

#include <new>

#include <windows.h>

//-----------------------------------------------------------------------------

// Objects with virtual functions can't be globals here: nothing runs their constructors without a C runtime

namespace heap
{
	template <class T>
	[[ nodiscard ]] T* create ()
	{
		auto*	memory = HeapAlloc ( GetProcessHeap (), HEAP_ZERO_MEMORY, sizeof ( T ) );
		return memory != nullptr ? new ( memory ) T {} : nullptr;
	}

	template <class T>
	void destroy ( T* object )
	{
		if ( object == nullptr )
			return;

		object->~T ();
		HeapFree ( GetProcessHeap (), 0, object );
	}
}
//-----------------------------------------------------------------------------
