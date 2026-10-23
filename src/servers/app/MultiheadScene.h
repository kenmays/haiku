/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Multi-output logical desktop layout model.
 */

#ifndef MULTIHEAD_SCENE_H
#define MULTIHEAD_SCENE_H

#include <InterfaceDefs.h>
#include <Rect.h>
#include <SupportDefs.h>

class BRegion;

#include <vector>


/*
 * Describes one output in global logical desktop coordinates.
 *
 * The ID is stable for the lifetime of the output and is not derived from
 * its position. Frames may use negative coordinates for displays positioned
 * to the left of or above the primary display.
 */
struct DisplayOutput {
	int32	id;
	BRect	frame;
	float	scale;
	bool	enabled;

	DisplayOutput(int32 outputID, const BRect& outputFrame, float outputScale,
		bool outputEnabled = true)
		:
		id(outputID),
		frame(outputFrame),
		scale(outputScale),
		enabled(outputEnabled)
	{
	}
};


/*
 * Geometry-only scene layout shared by the app_server display and scene
 * management work. This class deliberately does not own Screen, HWInterface,
 * or DrawingEngine instances: output lifetime and hardware operations remain
 * the responsibility of ScreenManager and VirtualScreen.
 *
 * Callers must serialize access if the object is shared between threads.
 */
class MultiheadScene {
public:
							MultiheadScene();

	status_t			AddOutput(int32 id, const BRect& frame,
							float scale = 1.0f);
	status_t				RemoveOutput(int32 id);
	status_t			SetOutputFrame(int32 id, const BRect& frame);
	status_t			SetOutputScale(int32 id, float scale);
	status_t			SetOutputEnabled(int32 id, bool enabled);

	int32				CountOutputs() const;
	const DisplayOutput*	OutputAt(int32 index) const;
	const DisplayOutput*	OutputByID(int32 id) const;
	int32				OutputAtPoint(BPoint point) const;
	int32				OutputWithLargestIntersection(const BRect& frame) const;
	status_t			GlobalToOutputPixel(int32 id, BPoint global,
							BPoint& pixel) const;
	status_t			OutputPixelToGlobal(int32 id, BPoint pixel,
							BPoint& global) const;
	status_t			ClipRegionToOutput(int32 id, const BRegion& globalRegion,
							int32 pixelWidth, int32 pixelHeight,
							BRegion& outputRegion) const;

	/*
	 * Append IDs of enabled outputs intersecting frame to outputIDs.
	 * Existing entries in outputIDs are retained.
	 */
	void				OutputsIntersecting(const BRect& frame,
							std::vector<int32>& outputIDs) const;

	BRect				Bounds() const;

private:
	static bool		_IsValidFrame(const BRect& frame);
	static bool		_IsValidScale(float scale);
	int32				_FindOutputIndex(int32 id) const;

	std::vector<DisplayOutput> fOutputs;
};

#endif // MULTIHEAD_SCENE_H
