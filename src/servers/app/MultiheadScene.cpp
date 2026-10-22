/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include "MultiheadScene.h"

#include <cmath>


MultiheadScene::MultiheadScene()
{
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

	return B_OK;
}


status_t
MultiheadScene::RemoveOutput(int32 id)
{
	int32 index = _FindOutputIndex(id);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	fOutputs.erase(fOutputs.begin() + index);
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

	fOutputs[index].frame = frame;
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

	fOutputs[index].scale = scale;
	return B_OK;
}


status_t
MultiheadScene::SetOutputEnabled(int32 id, bool enabled)
{
	int32 index = _FindOutputIndex(id);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	fOutputs[index].enabled = enabled;
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
