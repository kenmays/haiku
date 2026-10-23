/*
 * Copyright 2005-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef VIRTUAL_SCREEN_H
#define VIRTUAL_SCREEN_H


#include "MultiheadScene.h"
#include "ScreenConfigurations.h"
#include "ScreenManager.h"

#include <Message.h>
#include <Region.h>
#include <vector>


class Desktop;
class DrawingEngine;
class HWInterface;


// A pointer-free snapshot of one output's contribution to a drawing command.
// IDs must be resolved to live Screen objects only while holding the relevant
// app_server ownership/locking discipline.
struct OutputRenderTarget {
	int32 screenID;
	uint64 generation;
	int32 pixelWidth;
	int32 pixelHeight;
	BRect frame;
	float scale;
	int32 rendererOffsetX;
	int32 rendererOffsetY;
	BRegion globalClip;
	BRegion pixelClip;

	OutputRenderTarget()
		:
		screenID(-1),
		generation(0),
		pixelWidth(0),
		pixelHeight(0),
		frame(0, 0, 0, 0),
		scale(1.0f),
		rendererOffsetX(0),
		rendererOffsetY(0)
	{
	}

	OutputRenderTarget(int32 id, const BRect& outputFrame, float outputScale,
		int32 offsetX, int32 offsetY, const BRegion& logicalClip,
		const BRegion& physicalClip)
		:
		screenID(id),
		generation(0),
		pixelWidth(0),
		pixelHeight(0),
		frame(outputFrame),
		scale(outputScale),
		rendererOffsetX(offsetX),
		rendererOffsetY(offsetY),
		globalClip(logicalClip),
		pixelClip(physicalClip)
	{
	}
};


class VirtualScreen {
public:
								VirtualScreen();
								~VirtualScreen();

			::DrawingEngine*	DrawingEngine() const
									{ return fDrawingEngine; }

			// TODO: can we have a multiplexing HWInterface as well?
			//	If not, this would need to be hidden, and only made
			//	available for the Screen class
			::HWInterface*		HWInterface() const
									{ return fHWInterface; }

			status_t			SetConfiguration(Desktop& desktop,
									const ScreenConfigurations& configurations,
									ScreenConfigurations& currentConfigurations,
									uint32* _changedScreens = NULL);

			status_t			AddScreen(Screen* screen,
									const ScreenConfigurations& configurations,
									ScreenConfigurations& currentConfigurations);
			status_t			RemoveScreen(Screen* screen);

			void				UpdateFrame();
			BRect				Frame() const;

			// TODO: we need to play with a real multi-screen configuration to
			//	figure out the specifics here
			void				SetScreenFrame(int32 index, BRect frame);
			status_t		SetScreenScale(int32 index, float scale);
			float			ScreenScaleAt(int32 index) const;

			Screen*				ScreenAt(int32 index) const;
			Screen*				ScreenByID(int32 id) const;
			Screen*				ScreenAtPoint(BPoint point) const;
			Screen*				ScreenForFrame(BRect frame) const;
			status_t			GlobalToScreenPixel(const Screen* screen,
									BPoint global, BPoint& pixel) const;
			status_t			ScreenPixelToGlobal(const Screen* screen,
									BPoint pixel, BPoint& global) const;
			// Clips global-coordinate damage to one output and converts it
			// to framebuffer coordinates, rounding scaled bounds outwards.
			// Drawing operations still need their own per-output transform.
			status_t			ClipRegionToScreen(const Screen* screen,
									const BRegion& globalRegion,
									BRegion& screenRegion) const;

			// Build a pointer-free plan for a logical damage region. Each target
			// contains damage clipped to one output in both logical and pixel
			// coordinates. Hardware targets must be resolved later by screenID.
			status_t			BuildRenderPlan(const BRegion& globalDamage,
									std::vector<OutputRenderTarget>& targets) const;
			status_t			BuildRenderTarget(const Screen* screen,
									const BRegion& globalDamage,
									OutputRenderTarget& target) const;
	bool				IsRenderTargetCurrent(
									const OutputRenderTarget& target) const;
			BRect				ScreenFrameAt(int32 index) const;
			int32				CountScreens() const;

private:
			status_t			_GetMode(Screen* screen,
									const ScreenConfigurations& configurations,
									display_mode& mode) const;
			void				_Reset();

	struct screen_item {
		Screen*	screen;
		BRect	frame;
		// TODO: do we want to have a different color per screen as well?
	};

			BRect				fFrame;
			MultiheadScene		fScene;
			BObjectList<screen_item, true> fScreenList;
			::DrawingEngine*	fDrawingEngine;
			::HWInterface*		fHWInterface;
};

#endif	/* VIRTUAL_SCREEN_H */
