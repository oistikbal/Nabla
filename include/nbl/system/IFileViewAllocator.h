#ifndef __NBL_SYSTEM_I_FILE_ALLOCATOR_H_INCLUDED__
#define __NBL_SYSTEM_I_FILE_ALLOCATOR_H_INCLUDED__

#include <cstdint>
#include <cstdlib>

namespace nbl::system
{
	class IFileViewAllocator
	{
	public:
		virtual void* alloc(size_t size) = 0;
		virtual bool dealloc(void* data) = 0;
	};

	class CPlainHeapAllocator : public IFileViewAllocator
	{
	public:
		void* alloc(size_t size) override
		{
			return malloc(size);
		}
		bool dealloc(void* data) override
		{
			free(data);
			return true;
		}
	};

	class CNullAllocator : public IFileViewAllocator
	{
	public:
		void* alloc(size_t size) override
		{
			return nullptr;
		}
		bool dealloc(void* data) override
		{
			return true;
		}
	};
}
	#ifdef _NBL_PLATFORM_WINDOWS_
	#include <nbl/system/CFileViewVirtualAllocatorWin32.h>
		using VirtualAllocator = nbl::system::CFileViewVirtualAllocatorWin32;
	#elif defined(_NBL_PLATFORM_LINUX_) || defined(_NBL_PLATFORM_ANDROID_)
	#include <nbl/system/CFileViewVirtualAllocatorX11.h>
		using VirtualAllocator = nbl::system::CFileViewVirtualAllocatorX11;
	#endif
#endif