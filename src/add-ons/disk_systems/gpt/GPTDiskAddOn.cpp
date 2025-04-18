/*
 * Copyright 2013, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "GPTDiskAddOn.h"

#include <new>
#include <stdio.h>

#include <DiskDeviceTypes.h>
#include <MutablePartition.h>
#include <PartitioningInfo.h>
#include <PartitionParameterEditor.h>

#include <AutoDeleter.h>
#include <disk_device_types.h>

#include "gpt.h"

#include "GPTPartitionHandle.h"
#include "Utility.h"


//#define TRACE_GPT_DISK_ADD_ON
#undef TRACE
#ifdef TRACE_GPT_DISK_ADD_ON
#	define TRACE(x...) printf(x)
#else
#	define TRACE(x...) do {} while (false)
#endif


// #pragma mark - GPTDiskAddOn


GPTDiskAddOn::GPTDiskAddOn()
	:
	BDiskSystemAddOn(EFI_PARTITION_NAME)
{
}


GPTDiskAddOn::~GPTDiskAddOn()
{
}


status_t
GPTDiskAddOn::CreatePartitionHandle(BMutablePartition* partition,
	BPartitionHandle** _handle)
{
	GPTPartitionHandle* handle
		= new(std::nothrow) GPTPartitionHandle(partition);
	if (handle == NULL)
		return B_NO_MEMORY;

	status_t error = handle->Init();
	if (error != B_OK) {
		delete handle;
		return error;
	}

	*_handle = handle;
	return B_OK;
}


bool
GPTDiskAddOn::CanInitialize(const BMutablePartition* partition)
{
	// If it's big enough, we can initialize it.
	return partition->Size() >= round_up(partition->BlockSize()
			+ EFI_PARTITION_ENTRY_COUNT * EFI_PARTITION_ENTRY_SIZE,
		partition->BlockSize());
}


status_t
GPTDiskAddOn::ValidateInitialize(const BMutablePartition* partition,
	BString* name, const char* parameters)
{
	if (!CanInitialize(partition)
		|| (parameters != NULL && parameters[0] != '\0'))
		return B_BAD_VALUE;

	// we don't support a content name
	if (name != NULL)
		name->Truncate(0);

	return B_OK;
}


status_t
GPTDiskAddOn::Initialize(BMutablePartition* partition, const char* name,
	const char* parameters, BPartitionHandle** _handle)
{
	if (!CanInitialize(partition)
		|| (name != NULL && name[0] != '\0')
		|| (parameters != NULL && parameters[0] != '\0'))
		return B_BAD_VALUE;

	GPTPartitionHandle* handle
		= new(std::nothrow) GPTPartitionHandle(partition);
	if (handle == NULL)
		return B_NO_MEMORY;

	status_t status = partition->SetContentType(Name());
	if (status != B_OK) {
		delete handle;
		return status;
	}

	partition->SetContentName(NULL);
	partition->SetContentParameters(NULL);
	partition->SetContentSize(
		round_down(partition->Size(), partition->BlockSize()));
	partition->Changed(B_PARTITION_CHANGED_INITIALIZATION);

	*_handle = handle;
	return B_OK;
}
