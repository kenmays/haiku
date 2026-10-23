/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include "MultiheadScene.h"

#include <assert.h>
#include <float.h>
#include <math.h>


int
main()
{
	MultiheadScene scene;

	// Negative coordinates are valid: the left monitor is not the origin.
	assert(scene.AddOutput(10, BRect(-1280, 0, -1, 719), 1.0f) == B_OK);
	assert(scene.AddOutput(20, BRect(0, 0, 1919, 1079), 1.5f) == B_OK);
	assert(scene.CountOutputs() == 2);
	assert(scene.AddOutput(20, BRect(1920, 0, 3839, 1079), 1.0f)
		== B_NAME_IN_USE);
	assert(scene.AddOutput(-1, BRect(0, 0, 10, 10), 1.0f) == B_BAD_VALUE);
	assert(scene.AddOutput(30, BRect(0, 0, 10, 10), 0.0f) == B_BAD_VALUE);
	assert(scene.AddOutput(30, BRect(10, 10, 0, 0), 1.0f) == B_BAD_VALUE);
	assert(scene.AddOutput(30, BRect(0, 0, INFINITY, 10), 1.0f)
		== B_BAD_VALUE);

	assert(scene.OutputAtPoint(BPoint(-100, 20)) == 10);
	assert(scene.OutputAtPoint(BPoint(100, 20)) == 20);
	assert(scene.OutputAtPoint(BPoint(5000, 5000)) == -1);

	// Window assignment follows the largest intersected output, not the
	// output insertion order. Exact ties intentionally keep the first output.
	assert(scene.OutputWithLargestIntersection(BRect(-1000, 0, -500, 300))
		== 10);
	assert(scene.OutputWithLargestIntersection(BRect(100, 0, 500, 300))
		== 20);
	assert(scene.OutputWithLargestIntersection(BRect(-100, 0, 99, 100))
		== 10);
	assert(scene.OutputWithLargestIntersection(BRect(5000, 5000, 5100, 5100))
		== -1);
	assert(scene.OutputWithLargestIntersection(BRect(10, 10, 0, 0)) == -1);

	// Convert global logical coordinates to output-local physical pixels and
	// back, including a scaled output positioned at a negative origin.
	BPoint pixel;
	assert(scene.GlobalToOutputPixel(10, BPoint(-1180, 120), pixel) == B_OK);
	assert(pixel.x == 100 && pixel.y == 120);
	BPoint global;
	assert(scene.OutputPixelToGlobal(10, pixel, global) == B_OK);
	assert(global.x == -1180 && global.y == 120);

	// Scaling is independent of the output's negative global origin.
	assert(scene.SetOutputScale(10, 2.0f) == B_OK);
	assert(scene.GlobalToOutputPixel(10, BPoint(-1180, 120), pixel) == B_OK);
	assert(pixel.x == 200 && pixel.y == 240);
	assert(scene.OutputPixelToGlobal(10, pixel, global) == B_OK);
	assert(global.x == -1180 && global.y == 120);
	assert(scene.SetOutputScale(10, 1.0f) == B_OK);

	assert(scene.GlobalToOutputPixel(20, BPoint(100, 100), pixel) == B_OK);
	assert(pixel.x == 150 && pixel.y == 150);
	assert(scene.OutputPixelToGlobal(20, pixel, global) == B_OK);
	assert(global.x == 100 && global.y == 100);
	assert(scene.GlobalToOutputPixel(999, BPoint(0, 0), pixel)
		== B_ENTRY_NOT_FOUND);
	assert(scene.GlobalToOutputPixel(20, BPoint(INFINITY, 0), pixel)
		== B_BAD_VALUE);

	std::vector<int32> intersecting;
	scene.OutputsIntersecting(BRect(-20, 10, 20, 30), intersecting);
	assert(intersecting.size() == 2);
	assert(intersecting[0] == 10);
	assert(intersecting[1] == 20);

	BRect bounds = scene.Bounds();
	assert(bounds.left == -1280 && bounds.top == 0);
	assert(bounds.right == 1919 && bounds.bottom == 1079);

	assert(scene.SetOutputScale(20, 2.0f) == B_OK);
	assert(scene.SetOutputScale(20, -1.0f) == B_BAD_VALUE);

	// Finite inputs can still overflow during arithmetic. Failed transforms
	// must reject the result without partially overwriting the output point.
	pixel.Set(7, 8);
	assert(scene.GlobalToOutputPixel(20, BPoint(FLT_MAX, 0), pixel)
		== B_BAD_VALUE);
	assert(pixel.x == 7 && pixel.y == 8);
	assert(scene.SetOutputScale(20, 1e-38f) == B_OK);
	global.Set(9, 10);
	assert(scene.OutputPixelToGlobal(20, BPoint(FLT_MAX, 0), global)
		== B_BAD_VALUE);
	assert(global.x == 9 && global.y == 10);
	assert(scene.SetOutputScale(20, 2.0f) == B_OK);
	assert(scene.SetOutputFrame(20, BRect(0, 1080, 1919, 2159)) == B_OK);
	assert(scene.OutputAtPoint(BPoint(100, 1100)) == 20);
	assert(scene.SetOutputFrame(20, BRect(0, 0, 1919, 1079)) == B_OK);
	assert(scene.SetOutputEnabled(10, false) == B_OK);
	assert(scene.OutputAtPoint(BPoint(-100, 20)) == -1);
	assert(scene.Bounds().left == 0);
	std::vector<int32> afterDisable;
	scene.OutputsIntersecting(BRect(-20, 10, 20, 30), afterDisable);
	assert(afterDisable.size() == 1 && afterDisable[0] == 20);
	assert(scene.SetOutputEnabled(10, true) == B_OK);
	assert(scene.OutputAtPoint(BPoint(-100, 20)) == 10);

	assert(scene.RemoveOutput(10) == B_OK);
	assert(scene.RemoveOutput(10) == B_ENTRY_NOT_FOUND);
	assert(scene.OutputByID(20) != NULL);
	assert(scene.OutputAt(-1) == NULL);
	assert(scene.OutputAt(100) == NULL);

	return 0;
}
