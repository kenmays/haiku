/*
 * Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*! User Runtime Loader support in the kernel */


#include <KernelExport.h>

#include <kernel.h>
#include <kimage.h>
#include <kscheduler.h>
#include <lock.h>
#include <Notifications.h>
#include <team.h>
#include <thread.h>
#include <thread_types.h>
#include <user_debugger.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>

#include <stdlib.h>
#include <string.h>


//#define TRACE_IMAGE
#ifdef TRACE_IMAGE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define ADD_DEBUGGER_COMMANDS


namespace {

struct ImageTableDefinition {
	typedef image_id		KeyType;
	typedef struct image	ValueType;

	size_t HashKey(image_id key) const { return key; }
	size_t Hash(struct image* value) const { return value->info.basic_info.id; }
	bool Compare(image_id key, struct image* value) const
		{ return value->info.basic_info.id == key; }
	struct image*& GetLink(struct image* value) const
		{ return value->hash_link; }
};

typedef BOpenHashTable<ImageTableDefinition> ImageTable;


class ImageNotificationService : public DefaultNotificationService {
public:
	ImageNotificationService()
		: DefaultNotificationService("images")
	{
	}

	void Notify(uint32 eventCode, struct image* image)
	{
		char eventBuffer[128];
		KMessage event;
		event.SetTo(eventBuffer, sizeof(eventBuffer), IMAGE_MONITOR);
		event.AddInt32("event", eventCode);
		event.AddInt32("image", image->info.basic_info.id);
		event.AddPointer("imageStruct", image);

		DefaultNotificationService::Notify(event, eventCode);
	}
};

} // namespace


static image_id sNextImageID = 1;
static mutex sImageMutex = MUTEX_INITIALIZER("image");
static ImageTable* sImageTable;
static ImageNotificationService sNotificationService;


/*!	Registers an image with the specified team.
*/
static image_id
register_image(Team *team, extended_image_info *info, size_t size, bool locked)
{
	image_id id = atomic_add(&sNextImageID, 1);
	struct image *image;

	image = (struct image*)malloc(sizeof(struct image));
	if (image == NULL)
		return B_NO_MEMORY;

	memcpy(&image->info, info, sizeof(extended_image_info));
	image->team = team->id;

	if (!locked)
		mutex_lock(&sImageMutex);

	image->info.basic_info.id = id;

	// Add the app image to the head of the list. Some code relies on it being
	// the first image to be returned by get_next_image_info().
	if (image->info.basic_info.type == B_APP_IMAGE)
		team->image_list.Add(image, false);
	else
		team->image_list.Add(image);
	sImageTable->Insert(image);

	// notify listeners
	sNotificationService.Notify(IMAGE_ADDED, image);

	if (!locked)
		mutex_unlock(&sImageMutex);

	TRACE(("register_image(team = %p, image id = %ld, image = %p\n", team, id, image));
	return id;
}


/*!	Registers an image with the specified team.
*/
image_id
register_image(Team *team, extended_image_info *info, size_t size)
{
	return register_image(team, info, size, false);
}


/*!	Unregisters an image from the specified team.
*/
status_t
unregister_image(Team *team, image_id id)
{
	status_t status = B_ENTRY_NOT_FOUND;

	mutex_lock(&sImageMutex);

	struct image *image = sImageTable->Lookup(id);
	if (image != NULL && image->team == team->id) {
		team->image_list.Remove(image);
		sImageTable->Remove(image);
		status = B_OK;
	}

	mutex_unlock(&sImageMutex);

	if (status == B_OK) {
		// notify the debugger
		user_debug_image_deleted(&image->info.basic_info);

		// notify listeners
		sNotificationService.Notify(IMAGE_REMOVED, image);

		free(image);
	}

	return status;
}


status_t
copy_images(team_id fromTeamId, Team *toTeam)
{
	// get the team
	Team* fromTeam = Team::Get(fromTeamId);
	if (fromTeam == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(fromTeam, true);

	MutexLocker locker(sImageMutex);

	for (struct image* image = fromTeam->image_list.First();
			image != NULL; image = fromTeam->image_list.GetNext(image)) {
		image_id id = register_image(toTeam, &image->info, sizeof(image->info),
			true);
		if (id < 0)
			return id;
	}

	return B_OK;
}


/*!	Counts the registered images from the specified team.
	Interrupts must be enabled.
*/
int32
count_images(Team *team)
{
	MutexLocker locker(sImageMutex);

	int32 count = 0;
	for (struct image* image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		count++;
	}

	return count;
}


/*!	Removes all images from the specified team. Must only be called
	with a team that has already been removed from the list (in thread_exit()).
*/
status_t
remove_images(Team *team)
{
	ASSERT(team != NULL);

	mutex_lock(&sImageMutex);

	DoublyLinkedList<struct image> images;
	images.TakeFrom(&team->image_list);

	for (struct image* image = images.First();
			image != NULL; image = images.GetNext(image)) {
		sImageTable->Remove(image);
	}

	mutex_unlock(&sImageMutex);

	while (struct image* image = images.RemoveHead())
		free(image);

	return B_OK;
}


status_t
_get_image_info(image_id id, image_info *info, size_t size)
{
	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	status_t status = B_ENTRY_NOT_FOUND;

	mutex_lock(&sImageMutex);

	struct image *image = sImageTable->Lookup(id);
	if (image != NULL) {
		memcpy(info, &image->info.basic_info, size);
		status = B_OK;
	}

	mutex_unlock(&sImageMutex);

	return status;
}


status_t
_get_next_image_info(team_id teamID, int32 *cookie, image_info *info,
	size_t size)
{
	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	// get the team
	Team* team = Team::Get(teamID);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	// iterate through the team's images
	MutexLocker imageLocker(sImageMutex);

	int32 count = 0;

	for (struct image* image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		if (count == *cookie) {
			memcpy(info, &image->info.basic_info, size);
			(*cookie)++;
			return B_OK;
		}
		count++;
	}

	return B_ENTRY_NOT_FOUND;
}


#ifdef ADD_DEBUGGER_COMMANDS
static int
dump_images_list(int argc, char **argv)
{
	Team *team;

	if (argc > 1) {
		team_id id = strtol(argv[1], NULL, 0);
		team = team_get_team_struct_locked(id);
		if (team == NULL) {
			kprintf("No team with ID %" B_PRId32 " found\n", id);
			return 1;
		}
	} else
		team = thread_get_current_thread()->team;

	kprintf("Registered images of team %" B_PRId32 "\n", team->id);
	kprintf("    ID %-*s   size    %-*s   size    name\n",
		B_PRINTF_POINTER_WIDTH, "text", B_PRINTF_POINTER_WIDTH, "data");

	for (struct image* image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		image_info *info = &image->info.basic_info;

		kprintf("%6" B_PRId32 " %p %-7" B_PRId32 " %p %-7" B_PRId32 " %s\n",
			info->id, info->text, info->text_size, info->data, info->data_size,
			info->name);
	}

	return 0;
}
#endif


struct image*
image_iterate_through_images(image_iterator_callback callback, void* cookie)
{
	MutexLocker locker(sImageMutex);

	ImageTable::Iterator it = sImageTable->GetIterator();
	struct image* image = NULL;
	while ((image = it.Next()) != NULL) {
		if (callback(image, cookie))
			break;
	}

	return image;
}


struct image*
image_iterate_through_team_images(team_id teamID,
	image_iterator_callback callback, void* cookie)
{
	// get the team
	Team* team = Team::Get(teamID);
	if (team == NULL)
		return NULL;
	BReference<Team> teamReference(team, true);

	// iterate through the team's images
	MutexLocker imageLocker(sImageMutex);

	struct image *image = NULL;
	for (image = team->image_list.First();
			image != NULL; image = team->image_list.GetNext(image)) {
		if (callback(image, cookie))
			break;
	}

	return image;
}


status_t
image_init(void)
{
	sImageTable = new(std::nothrow) ImageTable;
	if (sImageTable == NULL) {
		panic("image_init(): Failed to allocate image table!");
		return B_NO_MEMORY;
	}

	status_t error = sImageTable->Init();
	if (error != B_OK) {
		panic("image_init(): Failed to init image table: %s", strerror(error));
		return error;
	}

	new(&sNotificationService) ImageNotificationService();

	sNotificationService.Register();

#ifdef ADD_DEBUGGER_COMMANDS
	add_debugger_command("team_images", &dump_images_list, "Dump all registered images from the current team");
#endif

	return B_OK;
}


static void
notify_loading_app(status_t result, bool suspend)
{
	Team* team = thread_get_current_thread()->team;

	TeamLocker teamLocker(team);

	if (team->loading_info != NULL) {
		// there's indeed someone waiting

		thread_prepare_suspend();

		// wake up the waiting thread
		team->loading_info->result = result;
		team->loading_info->condition.NotifyAll();
		team->loading_info = NULL;

		// we're done with the team stuff
		teamLocker.Unlock();

		// suspend ourselves, if desired
		if (suspend)
			thread_suspend(true);
	}
}


//	#pragma mark -
//	Functions exported for the user space


status_t
_user_unregister_image(image_id id)
{
	return unregister_image(thread_get_current_thread()->team, id);
}


image_id
_user_register_image(extended_image_info *userInfo, size_t size)
{
	extended_image_info info;

	if (size != sizeof(info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&info, userInfo, size) < B_OK)
		return B_BAD_ADDRESS;

	return register_image(thread_get_current_thread()->team, &info, size);
}


void
_user_image_relocated(image_id id)
{
	image_info info;
	status_t error;

	// get an image info
	error = _get_image_info(id, &info, sizeof(image_info));
	if (error != B_OK) {
		dprintf("_user_image_relocated(%" B_PRId32 "): Failed to get image "
			"info: %" B_PRIx32 "\n", id, error);
		return;
	}

	// notify the debugger
	user_debug_image_created(&info);

	// If the image is the app image, loading is done. We need to notify the
	// thread who initiated the process and is now waiting for us to be done.
	if (info.type == B_APP_IMAGE)
		notify_loading_app(B_OK, true);
}


void
_user_loading_app_failed(status_t error)
{
	if (error >= B_OK)
		error = B_ERROR;

	notify_loading_app(error, false);

	_user_exit_team(error);
}


status_t
_user_get_image_info(image_id id, image_info *userInfo, size_t size)
{
	image_info info;
	status_t status;

	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = _get_image_info(id, &info, sizeof(image_info));

	if (user_memcpy(userInfo, &info, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_get_next_image_info(team_id team, int32 *_cookie, image_info *userInfo,
	size_t size)
{
	image_info info;
	status_t status;
	int32 cookie;

	if (size > sizeof(image_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo) || !IS_USER_ADDRESS(_cookie)
		|| user_memcpy(&cookie, _cookie, sizeof(int32)) < B_OK) {
		return B_BAD_ADDRESS;
	}

	status = _get_next_image_info(team, &cookie, &info, sizeof(image_info));

	if (user_memcpy(userInfo, &info, size) < B_OK
		|| user_memcpy(_cookie, &cookie, sizeof(int32)) < B_OK) {
		return B_BAD_ADDRESS;
	}

	return status;
}

