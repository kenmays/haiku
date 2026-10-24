/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include "MultiheadScene.h"

#include <Region.h>

#include <algorithm>
#include <cmath>
#include <limits>


MultiheadScene::MultiheadScene()
	:
	fGeneration(1)
{
}


void
MultiheadScene::_AdvanceGeneration()
{
	// Keep zero reserved for an uninitialized/stale token if the counter wraps.
	if (++fGeneration == 0)
		fGeneration = 1;
}


bool
MultiheadScene::_IsValidFrame(const BRect& frame)
{
	return frame.IsValid() && std::isfinite(frame.left)
		&& std::isfinite(frame.top) && std::isfinite(frame.right)
		&& std::isfinite(frame.bottom);
}


bool
MultiheadScene::_IsValidScale(float scale)
{
	return std::isfinite(scale) && scale > 0.0f;
}


int32
MultiheadScene::_FindOutputIndex(int32 id) const
{
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (fOutputs[i].id == id)
			return (int32)i;
	}

	return -1;
}


status_t
MultiheadScene::AddOutput(int32 id, const BRect& frame, float scale)
{
	if (id < 0 || !_IsValidFrame(frame) || !_IsValidScale(scale))
		return B_BAD_VALUE;

	if (_FindOutputIndex(id) >= 0)
		return B_NAME_IN_USE;

	try {
		fOutputs.push_back(DisplayOutput(id, frame, scale));
	} catch (...) {
		return B_NO_MEMORY;
	}

	_AdvanceGeneration();
	return B_OK;
}


status_t
MultiheadScene::RemoveOutput(int32 id)
{
	int32 index = _FindOutputIndex(id);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	fOutputs.erase(fOutputs.begin() + index);
	_AdvanceGeneration();
	return B_OK;
}


status_t
MultiheadScene::SetOutputFrame(int32 id, const BRect& frame)
{
	if (!_IsValidFrame(frame))
		return B_BAD_VALUE;

	int32 index = _FindOutputIndex(id);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	if (fOutputs[index].frame.left != frame.left
		|| fOutputs[index].frame.top != frame.top
		|| fOutputs[index].frame.right != frame.right
		|| fOutputs[index].frame.bottom != frame.bottom) {
		fOutputs[index].frame = frame;
		_AdvanceGeneration();
	}
	return B_OK;
}


status_t
MultiheadScene::SetOutputScale(int32 id, float scale)
{
	if (!_IsValidScale(scale))
		return B_BAD_VALUE;

	int32 index = _FindOutputIndex(id);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	if (fOutputs[index].scale != scale) {
		fOutputs[index].scale = scale;
		_AdvanceGeneration();
	}
	return B_OK;
}


status_t
MultiheadScene::SetOutputEnabled(int32 id, bool enabled)
{
	int32 index = _FindOutputIndex(id);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	if (fOutputs[index].enabled != enabled) {
		fOutputs[index].enabled = enabled;
		_AdvanceGeneration();
	}
	return B_OK;
}


int32
MultiheadScene::CountOutputs() const
{
	return (int32)fOutputs.size();
}


const DisplayOutput*
MultiheadScene::OutputAt(int32 index) const
{
	if (index < 0 || (size_t)index >= fOutputs.size())
		return NULL;

	return &fOutputs[index];
}


const DisplayOutput*
MultiheadScene::OutputByID(int32 id) const
{
	int32 index = _FindOutputIndex(id);
	return index < 0 ? NULL : &fOutputs[index];
}


int32
MultiheadScene::OutputAtPoint(BPoint point) const
{
	// Search in reverse insertion order so the most recently registered output
	// wins in the unlikely event that output rectangles overlap.
	for (int32 i = (int32)fOutputs.size() - 1; i >= 0; i--) {
		const DisplayOutput& output = fOutputs[i];
		if (output.enabled && output.frame.Contains(point))
			return output.id;
	}

	return -1;
}


int32
MultiheadScene::OutputWithLargestIntersection(const BRect& frame) const
{
	if (!_IsValidFrame(frame))
		return -1;

	int32 bestID = -1;
	double bestArea = 0.0;

	for (size_t i = 0; i < fOutputs.size(); i++) {
		const DisplayOutput& output = fOutputs[i];
		if (!output.enabled || !output.frame.Intersects(frame))
			continue;

		double left = std::max((double)frame.left,
			(double)output.frame.left);
		double top = std::max((double)frame.top,
			(double)output.frame.top);
		double right = std::min((double)frame.right,
			(double)output.frame.right);
		double bottom = std::min((double)frame.bottom,
			(double)output.frame.bottom);
		double area = (right - left + 1.0) * (bottom - top + 1.0);

		// Keep the first output on ties so selection remains deterministic.
		if (area > bestArea) {
			bestArea = area;
			bestID = output.id;
		}
	}

	return bestID;
}

status_t
MultiheadScene::GlobalToOutputPixel(int32 id, BPoint global,
	BPoint& pixel) const
{
	if (!std::isfinite(global.x) || !std::isfinite(global.y))
		return B_BAD_VALUE;

	const DisplayOutput* output = OutputByID(id);
	if (output == NULL || !output->enabled)
		return B_ENTRY_NOT_FOUND;

	// Do intermediate arithmetic in double precision. Subtracting two finite
	// float coordinates can overflow in float before a small output scale is
	// applied, even when the final pixel coordinate is representable.
	const double x = ((double)global.x - output->frame.left)
		* (double)output->scale;
	const double y = ((double)global.y - output->frame.top)
		* (double)output->scale;
	if (!std::isfinite(x) || !std::isfinite(y)
		|| std::fabs(x) > std::numeric_limits<float>::max()
		|| std::fabs(y) > std::numeric_limits<float>::max()) {
		return B_BAD_VALUE;
	}

	pixel = BPoint((float)x, (float)y);
	return B_OK;
}


status_t
MultiheadScene::OutputPixelToGlobal(int32 id, BPoint pixel,
	BPoint& global) const
{
	if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y))
		return B_BAD_VALUE;

	const DisplayOutput* output = OutputByID(id);
	if (output == NULL || !output->enabled)
		return B_ENTRY_NOT_FOUND;

	// As above, keep division and origin addition in double precision so
	// overflow in an intermediate float operation cannot corrupt a valid
	// result or leak a partial value to the caller.
	const double x = (double)output->frame.left
		+ (double)pixel.x / output->scale;
	const double y = (double)output->frame.top
		+ (double)pixel.y / output->scale;
	if (!std::isfinite(x) || !std::isfinite(y)
		|| std::fabs(x) > std::numeric_limits<float>::max()
		|| std::fabs(y) > std::numeric_limits<float>::max()) {
		return B_BAD_VALUE;
	}

	global = BPoint((float)x, (float)y);
	return B_OK;
}


status_t
MultiheadScene::ClipRegionToOutput(int32 id, const BRegion& globalRegion,
	int32 pixelWidth, int32 pixelHeight, BRegion& outputRegion) const
{
	outputRegion.MakeEmpty();
	if (pixelWidth <= 0 || pixelHeight <= 0)
		return B_BAD_VALUE;

	const DisplayOutput* output = OutputByID(id);
	if (output == NULL || !output->enabled)
		return B_ENTRY_NOT_FOUND;

	const BRect& frame = output->frame;
	if (!_IsValidFrame(frame)
		|| (double)frame.left < -2147483648.0
		|| (double)frame.left > 2147483647.0
		|| (double)frame.top < -2147483648.0
		|| (double)frame.top > 2147483647.0
		|| (double)frame.right < -2147483648.0
		|| (double)frame.right > 2147483647.0
		|| (double)frame.bottom < -2147483648.0
		|| (double)frame.bottom > 2147483647.0) {
		return B_BAD_VALUE;
	}

	BRegion clipped(globalRegion);
	BRegion outputBounds(frame);
	clipped.IntersectWith(&outputBounds);

	BRegion converted;
	converted.MakeEmpty();
	const double scale = output->scale;
	const int32 maxX = pixelWidth - 1;
	const int32 maxY = pixelHeight - 1;
	for (int32 i = 0; i < clipped.CountRects(); i++) {
		BRect rect = clipped.RectAtInt(i);
		double left = std::floor(((double)rect.left - frame.left) * scale);
		double top = std::floor(((double)rect.top - frame.top) * scale);
		double right = std::ceil(((double)rect.right - frame.left) * scale);
		double bottom = std::ceil(((double)rect.bottom - frame.top) * scale);

		// Discard damage that lies wholly outside the framebuffer before
		// clamping. Clamping first would collapse an out-of-bounds rectangle
		// onto the last row or column and spuriously dirty edge pixels.
		if (right < 0.0 || bottom < 0.0 || left > (double)maxX
			|| top > (double)maxY) {
			continue;
		}

		// Round damage outwards, then clamp the intersecting portion to the
		// actual pixel dimensions.
		left = std::max(0.0, std::min((double)maxX, left));
		top = std::max(0.0, std::min((double)maxY, top));
		right = std::max(0.0, std::min((double)maxX, right));
		bottom = std::max(0.0, std::min((double)maxY, bottom));

		if (left <= right && top <= bottom) {
			converted.Include(BRect((float)left, (float)top,
				(float)right, (float)bottom));
		}
	}

	outputRegion = converted;
	return B_OK;
}

void
MultiheadScene::OutputsIntersecting(const BRect& frame,
	std::vector<int32>& outputIDs) const
{
	if (!_IsValidFrame(frame))
		return;

	for (size_t i = 0; i < fOutputs.size(); i++) {
		const DisplayOutput& output = fOutputs[i];
		if (output.enabled && output.frame.Intersects(frame))
			outputIDs.push_back(output.id);
	}
}


BRect
MultiheadScene::Bounds() const
{
	BRect bounds(0, 0, -1, -1);
	bool hasBounds = false;

	for (size_t i = 0; i < fOutputs.size(); i++) {
		const DisplayOutput& output = fOutputs[i];
		if (!output.enabled)
			continue;

		if (!hasBounds) {
			bounds = output.frame;
			hasBounds = true;
		} else {
			bounds.left = std::min(bounds.left, output.frame.left);
			bounds.top = std::min(bounds.top, output.frame.top);
			bounds.right = std::max(bounds.right, output.frame.right);
			bounds.bottom = std::max(bounds.bottom, output.frame.bottom);
		}
	}

	return bounds;
}
