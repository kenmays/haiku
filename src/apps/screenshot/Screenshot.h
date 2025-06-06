/*
 * Copyright 2010 Wim van der Meer <WPJvanderMeer@gmail.com>
 * Distributed under the terms of the MIT License.
 */
#ifndef SCREENSHOT_H
#define SCREENSHOT_H


#include <Application.h>
#include <Catalog.h>


class BBitmap;
class Utility;


class Screenshot : public BApplication {
public:
						Screenshot();
						~Screenshot();

			void		ReadyToRun();
			void		ArgvReceived(int32 argc, char** argv);

private:
			void		_ShowHelp();
			void		_New(bigtime_t delay);
			status_t	_GetActiveWindowFrame();
			int32		_ImageType(const char* name) const;

private:
			Utility*	fUtility;
			bool		fLaunchGui;
			bool		fSelectArea;
};


#endif // SCREENSHOT_H
