/*
 * Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include <vm/VMTranslationMap.h>

#include <slab/Slab.h>
#include <vm/vm_page.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>


// #pragma mark - VMTranslationMap


VMTranslationMap::VMTranslationMap()
	:
	fMapCount(0)
{
	recursive_lock_init(&fLock, "translation map");
}


VMTranslationMap::~VMTranslationMap()
{
	recursive_lock_destroy(&fLock);
}


status_t
VMTranslationMap::DebugMarkRangePresent(addr_t start, addr_t end,
	bool markPresent)
{
	return B_NOT_SUPPORTED;
}


/*!	Unmaps a range of pages of an area.

	The default implementation just iterates over all virtual pages of the
	range and calls UnmapPage(). This is obviously not particularly efficient.
*/
void
VMTranslationMap::UnmapPages(VMArea* area, addr_t base, size_t size,
	bool updatePageQueue)
{
	ASSERT(base % B_PAGE_SIZE == 0);
	ASSERT(size % B_PAGE_SIZE == 0);

	addr_t address = base;
	addr_t end = address + size;
#if DEBUG_PAGE_ACCESS
	for (; address != end; address += B_PAGE_SIZE) {
		phys_addr_t physicalAddress;
		uint32 flags;
		if (Query(address, &physicalAddress, &flags) == B_OK
			&& (flags & PAGE_PRESENT) != 0) {
			vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
			if (page != NULL) {
				DEBUG_PAGE_ACCESS_START(page);
				UnmapPage(area, address, updatePageQueue);
				DEBUG_PAGE_ACCESS_END(page);
			} else
				UnmapPage(area, address, updatePageQueue);
		}
	}
#else
	for (; address != end; address += B_PAGE_SIZE)
		UnmapPage(area, address, updatePageQueue);
#endif
}


/*!	Unmaps all of an area's pages.
	If \a deletingAddressSpace is \c true, the address space the area belongs to
	is in the process of being destroyed and isn't used by anyone anymore. For
	some architectures this can be used for optimizations (e.g. not unmapping
	pages or at least not needing to invalidate TLB entries).
	If \a ignoreTopCachePageFlags is \c true, the area is in the process of
	being destroyed and its top cache is otherwise unreferenced. I.e. all mapped
	pages that live in the top cache area going to be freed and the page
	accessed and modified flags don't need to be propagated.

	The default implementation just iterates over all virtual pages of the
	area and calls UnmapPage(). This is obviously not particularly efficient.
*/
void
VMTranslationMap::UnmapArea(VMArea* area, bool deletingAddressSpace,
	bool ignoreTopCachePageFlags)
{
	addr_t address = area->Base();
	addr_t end = address + area->Size();
#if DEBUG_PAGE_ACCESS
	for (; address != end; address += B_PAGE_SIZE) {
		phys_addr_t physicalAddress;
		uint32 flags;
		if (Query(address, &physicalAddress, &flags) == B_OK
			&& (flags & PAGE_PRESENT) != 0) {
			vm_page* page = vm_lookup_page(physicalAddress / B_PAGE_SIZE);
			if (page != NULL) {
				DEBUG_PAGE_ACCESS_START(page);
				UnmapPage(area, address, true);
				DEBUG_PAGE_ACCESS_END(page);
			} else
				UnmapPage(area, address, true);
		}
	}
#else
	for (; address != end; address += B_PAGE_SIZE)
		UnmapPage(area, address, true);
#endif
}


/*!	Print mapping information for a virtual address.
	The method navigates the paging structures and prints all relevant
	information on the way.
	The method is invoked from a KDL command. The default implementation is a
	no-op.
	\param virtualAddress The virtual address to look up.
*/
void
VMTranslationMap::DebugPrintMappingInfo(addr_t virtualAddress)
{
#if KDEBUG
	kprintf("VMTranslationMap::DebugPrintMappingInfo not implemented\n");
#endif
}


/*!	Find virtual addresses mapped to the given physical address.
	For each virtual address the method finds, it invokes the callback object's
	HandleVirtualAddress() method. When that method returns \c true, the search
	is terminated and \c true is returned.
	The method is invoked from a KDL command. The default implementation is a
	no-op.
	\param physicalAddress The physical address to search for.
	\param callback Callback object to be notified of each found virtual
		address.
	\return \c true, if for a found virtual address the callback's
		HandleVirtualAddress() returned \c true, \c false otherwise.
*/
bool
VMTranslationMap::DebugGetReverseMappingInfo(phys_addr_t physicalAddress,
	ReverseMappingInfoCallback& callback)
{
#if KDEBUG
	kprintf("VMTranslationMap::DebugGetReverseMappingInfo not implemented\n");
#endif
	return false;
}


/*!	Called by UnmapPage() after performing the architecture specific part.
	Looks up the page, updates its flags, removes the page-area mapping, and
	requeues the page, if necessary.

	If \c mappingsQueue is unspecified, then it unlocks the map and frees the
	page-area mapping. If \c mappingsQueue is specified, then it adds the removed
	mapping to the queue and does NOT unlock the map.
*/
void
VMTranslationMap::PageUnmapped(VMArea* area, page_num_t pageNumber,
	bool accessed, bool modified, bool updatePageQueue, VMAreaMappings* mappingsQueue)
{
	if (area->cache_type == CACHE_TYPE_DEVICE) {
		if (mappingsQueue == NULL)
			recursive_lock_unlock(&fLock);
		return;
	}

	// get the page
	vm_page* page = vm_lookup_page(pageNumber);
	ASSERT_PRINT(page != NULL, "page number: %#" B_PRIxPHYSADDR
		", accessed: %d, modified: %d", pageNumber, accessed, modified);

	if (mappingsQueue != NULL) {
		DEBUG_PAGE_ACCESS_START(page);
	} else {
		DEBUG_PAGE_ACCESS_CHECK(page);
	}

	// transfer the accessed/dirty flags to the page
	page->accessed |= accessed;
	page->modified |= modified;

	// remove the mapping object/decrement the wired_count of the page
	vm_page_mapping* mapping = NULL;
	if (area->wiring == B_NO_LOCK) {
		vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
		while ((mapping = iterator.Next()) != NULL) {
			if (mapping->area == area) {
				area->mappings.Remove(mapping);
				page->mappings.Remove(mapping);
				break;
			}
		}

		ASSERT_PRINT(mapping != NULL, "page: %p, page number: %#"
			B_PRIxPHYSADDR ", accessed: %d, modified: %d", page,
			pageNumber, accessed, modified);
	} else
		page->DecrementWiredCount();

	if (mappingsQueue == NULL)
		recursive_lock_unlock(&fLock);

	if (!page->IsMapped()) {
		atomic_add(&gMappedPagesCount, -1);

		if (updatePageQueue) {
			if (page->Cache()->temporary)
				vm_page_set_state(page, PAGE_STATE_INACTIVE);
			else if (page->modified)
				vm_page_set_state(page, PAGE_STATE_MODIFIED);
			else
				vm_page_set_state(page, PAGE_STATE_CACHED);
		}
	}

	if (mappingsQueue != NULL) {
		DEBUG_PAGE_ACCESS_END(page);
	}

	if (mapping != NULL) {
		if (mappingsQueue == NULL) {
			bool isKernelSpace = area->address_space == VMAddressSpace::Kernel();
			vm_free_page_mapping(pageNumber, mapping,
				CACHE_DONT_WAIT_FOR_MEMORY
					| (isKernelSpace ? CACHE_DONT_LOCK_KERNEL_SPACE : 0));
		} else {
			mappingsQueue->Add(mapping);
		}
	}
}


/*!	Called by ClearAccessedAndModified() after performing the architecture
	specific part.
	Looks up the page and removes the page-area mapping.
*/
void
VMTranslationMap::UnaccessedPageUnmapped(VMArea* area, page_num_t pageNumber)
{
	if (area->cache_type == CACHE_TYPE_DEVICE) {
		recursive_lock_unlock(&fLock);
		return;
	}

	// get the page
	vm_page* page = vm_lookup_page(pageNumber);
	ASSERT_PRINT(page != NULL, "page number: %#" B_PRIxPHYSADDR, pageNumber);

	// remove the mapping object/decrement the wired_count of the page
	vm_page_mapping* mapping = NULL;
	if (area->wiring == B_NO_LOCK) {
		vm_page_mappings::Iterator iterator = page->mappings.GetIterator();
		while ((mapping = iterator.Next()) != NULL) {
			if (mapping->area == area) {
				area->mappings.Remove(mapping);
				page->mappings.Remove(mapping);
				break;
			}
		}

		ASSERT_PRINT(mapping != NULL, "page: %p, page number: %#"
			B_PRIxPHYSADDR, page, pageNumber);
	} else
		page->DecrementWiredCount();

	recursive_lock_unlock(&fLock);

	if (!page->IsMapped())
		atomic_add(&gMappedPagesCount, -1);

	if (mapping != NULL) {
		vm_free_page_mapping(pageNumber, mapping,
			CACHE_DONT_WAIT_FOR_MEMORY | CACHE_DONT_LOCK_KERNEL_SPACE);
			// Since this is called by the page daemon, we never want to lock
			// the kernel address space.
	}
}


// #pragma mark - ReverseMappingInfoCallback


VMTranslationMap::ReverseMappingInfoCallback::~ReverseMappingInfoCallback()
{
}


// #pragma mark - VMPhysicalPageMapper


VMPhysicalPageMapper::VMPhysicalPageMapper()
{
}


VMPhysicalPageMapper::~VMPhysicalPageMapper()
{
}
