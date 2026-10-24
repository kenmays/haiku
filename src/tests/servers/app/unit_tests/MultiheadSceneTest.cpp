/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include "MultiheadScene.h"

#include <Region.h>

#include <cmath>
#include <limits>

#include <TestSuiteAddon.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>


class MultiheadSceneTest : public CppUnit::TestFixture {
	CPPUNIT_TEST_SUITE(MultiheadSceneTest);
	CPPUNIT_TEST(OutputSelectionUsesGlobalCoordinates);
	CPPUNIT_TEST(MixedScalePixelClippingRoundsOutwards);
	CPPUNIT_TEST(NegativeOriginMapsToLocalPixels);
	CPPUNIT_TEST(FractionalOutputOriginsMapToLocalPixels);
	CPPUNIT_TEST(GenerationTracksOutputChanges);
	CPPUNIT_TEST(OutputRemovalAndReconnectionPreservesStableIdentity);
	CPPUNIT_TEST(InvalidOutputUpdatesAreRejectedAtomically);
	CPPUNIT_TEST(DisabledOutputsAreExcludedFromLayoutAndDamage);
	CPPUNIT_TEST(OffscreenDamageDoesNotCollapseOntoFramebufferEdges);
	CPPUNIT_TEST(OverflowingCoordinateConversionDoesNotPublishResult);
	CPPUNIT_TEST_SUITE_END();

public:
	void OutputSelectionUsesGlobalCoordinates()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(10, BRect(-1920, 0, -1, 1079), 1.0f));
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(20, BRect(0, 0, 2559, 1439), 1.5f));

		CPPUNIT_ASSERT_EQUAL((int32)10, scene.OutputAtPoint(BPoint(-10, 20)));
		CPPUNIT_ASSERT_EQUAL((int32)20, scene.OutputAtPoint(BPoint(10, 20)));
		CPPUNIT_ASSERT_EQUAL((int32)20,
			scene.OutputWithLargestIntersection(BRect(-100, 0, 100, 100)));
	}

	void MixedScalePixelClippingRoundsOutwards()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(20, BRect(0, 0, 99, 99), 1.5f));

		BRegion damage;
		damage.Include(BRect(-10, -10, 10, 10));

		BRegion pixels;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.ClipRegionToOutput(20, damage, 150, 150, pixels));
		CPPUNIT_ASSERT_EQUAL((int32)1, pixels.CountRects());
		CPPUNIT_ASSERT(pixels.RectAtInt(0) == BRect(0, 0, 15, 15));
	}

	void GenerationTracksOutputChanges()
	{
		MultiheadScene scene;
		uint64 generation = scene.Generation();

		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(10, BRect(0, 0, 99, 99), 1.0f));
		CPPUNIT_ASSERT(scene.Generation() != generation);
		generation = scene.Generation();

		// No-op updates must not invalidate render batches.
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.SetOutputFrame(10, BRect(0, 0, 99, 99)));
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK, scene.SetOutputScale(10, 1.0f));
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.SetOutputEnabled(10, true));
		CPPUNIT_ASSERT_EQUAL(generation, scene.Generation());

		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.SetOutputFrame(10, BRect(-100, 0, -1, 99)));
		CPPUNIT_ASSERT(scene.Generation() != generation);
		generation = scene.Generation();
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK, scene.SetOutputScale(10, 2.0f));
		CPPUNIT_ASSERT(scene.Generation() != generation);
		generation = scene.Generation();
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.SetOutputEnabled(10, false));
		CPPUNIT_ASSERT(scene.Generation() != generation);
		generation = scene.Generation();
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK, scene.RemoveOutput(10));
		CPPUNIT_ASSERT(scene.Generation() != generation);
	}

	void OutputRemovalAndReconnectionPreservesStableIdentity()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(42, BRect(-100, 0, -1, 99), 1.0f));
		uint64 generation = scene.Generation();

		CPPUNIT_ASSERT_EQUAL((status_t)B_OK, scene.RemoveOutput(42));
		CPPUNIT_ASSERT(scene.Generation() != generation);
		CPPUNIT_ASSERT_EQUAL((int32)-1,
			scene.OutputAtPoint(BPoint(-20, 20)));
		CPPUNIT_ASSERT(scene.OutputByID(42) == NULL);

		generation = scene.Generation();
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(42, BRect(0, 0, 199, 99), 2.0f));
		CPPUNIT_ASSERT(scene.Generation() != generation);
		CPPUNIT_ASSERT_EQUAL((int32)42,
			scene.OutputAtPoint(BPoint(50, 50)));

		BPoint pixel;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.GlobalToOutputPixel(42, BPoint(10, 10), pixel));
		CPPUNIT_ASSERT(pixel == BPoint(20, 20));
	}


	// Malformed driver/layout data must not mutate a published scene or
	// invalidate render plans that still refer to its valid generation.
	void InvalidOutputUpdatesAreRejectedAtomically()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(10, BRect(-100, -50, -1, 49), 1.0f));
		const uint64 generation = scene.Generation();

		CPPUNIT_ASSERT_EQUAL((status_t)B_BAD_VALUE,
			scene.AddOutput(11, BRect(0, 0, 99, 99), 0.0f));
		CPPUNIT_ASSERT_EQUAL((status_t)B_BAD_VALUE,
			scene.AddOutput(11, BRect(0, 0, 99, 99),
				std::numeric_limits<float>::infinity()));
		CPPUNIT_ASSERT_EQUAL((status_t)B_NAME_IN_USE,
			scene.AddOutput(10, BRect(0, 0, 99, 99), 1.0f));
		CPPUNIT_ASSERT_EQUAL((status_t)B_BAD_VALUE,
			scene.SetOutputFrame(10, BRect(0, 0, -1, 99)));
		CPPUNIT_ASSERT_EQUAL((status_t)B_BAD_VALUE,
			scene.SetOutputScale(10, -1.0f));

		CPPUNIT_ASSERT_EQUAL(generation, scene.Generation());
		CPPUNIT_ASSERT_EQUAL((int32)1, scene.CountOutputs());
		const DisplayOutput* output = scene.OutputByID(10);
		CPPUNIT_ASSERT(output != NULL);
		CPPUNIT_ASSERT(output->frame == BRect(-100, -50, -1, 49));
		CPPUNIT_ASSERT_EQUAL(1.0f, output->scale);
	}

	void DisabledOutputsAreExcludedFromLayoutAndDamage()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(10, BRect(0, 0, 99, 99), 1.0f));
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(20, BRect(100, 0, 199, 99), 2.0f));

		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.SetOutputEnabled(20, false));
		CPPUNIT_ASSERT(scene.Bounds() == BRect(0, 0, 99, 99));
		CPPUNIT_ASSERT_EQUAL((int32)10,
			scene.OutputAtPoint(BPoint(150, 20)));
		CPPUNIT_ASSERT_EQUAL((int32)-1,
			scene.OutputWithLargestIntersection(BRect(120, 0, 180, 50)));

		BPoint pixel(7, 9);
		CPPUNIT_ASSERT_EQUAL((status_t)B_ENTRY_NOT_FOUND,
			scene.GlobalToOutputPixel(20, BPoint(120, 10), pixel));
		// Failed conversion must not publish a partial result.
		CPPUNIT_ASSERT(pixel == BPoint(7, 9));

		BRegion damage;
		damage.Include(BRect(120, 0, 180, 50));
		BRegion outputDamage;
		CPPUNIT_ASSERT_EQUAL((status_t)B_ENTRY_NOT_FOUND,
			scene.ClipRegionToOutput(20, damage, 200, 100, outputDamage));
		CPPUNIT_ASSERT_EQUAL((int32)0, outputDamage.CountRects());
	}

	void OffscreenDamageDoesNotCollapseOntoFramebufferEdges()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(10, BRect(-100, -50, -1, 49), 1.5f));

		BRegion damage;
		damage.Include(BRect(20, 20, 30, 30));
		damage.Include(BRect(-150, 10, -120, 20));
		BRegion pixels;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.ClipRegionToOutput(10, damage, 150, 150, pixels));

		// Both input rectangles are wholly outside the output's logical
		// frame. Clamping before rejection would incorrectly dirty edge pixels.
		CPPUNIT_ASSERT_EQUAL((int32)0, pixels.CountRects());
	}

	void OverflowingCoordinateConversionDoesNotPublishResult()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(10, BRect(0, 0, 99, 99),
				std::numeric_limits<float>::max()));

		BPoint pixel(17, 23);
		CPPUNIT_ASSERT_EQUAL((status_t)B_BAD_VALUE,
			scene.GlobalToOutputPixel(10, BPoint(1.0e30f, 1.0e30f), pixel));
		CPPUNIT_ASSERT(pixel == BPoint(17, 23));

		// The output remains usable after a rejected conversion.
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.GlobalToOutputPixel(10, BPoint(0, 0), pixel));
		CPPUNIT_ASSERT(pixel == BPoint(0, 0));

		// The inverse mapping must also reject non-finite results without
		// publishing a partially converted global coordinate.
		MultiheadScene tinyScaleScene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			tinyScaleScene.AddOutput(20, BRect(0, 0, 99, 99),
				std::numeric_limits<float>::min()));
		BPoint global(31, 37);
		CPPUNIT_ASSERT_EQUAL((status_t)B_BAD_VALUE,
			tinyScaleScene.OutputPixelToGlobal(20,
				BPoint(std::numeric_limits<float>::max(), 1.0f), global));
		CPPUNIT_ASSERT(global == BPoint(31, 37));

		// Finite float inputs can overflow during an intermediate float
		// subtraction/division even when the final transformed coordinate fits.
		const float maxFloat = std::numeric_limits<float>::max();
		MultiheadScene wideRangeScene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			wideRangeScene.AddOutput(30,
				BRect(-maxFloat, 0, -maxFloat, 0), 1.0e-38f));
		BPoint widePixel;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			wideRangeScene.GlobalToOutputPixel(30,
				BPoint(maxFloat, 0), widePixel));
		CPPUNIT_ASSERT(std::isfinite(widePixel.x));
		CPPUNIT_ASSERT(widePixel.x > 6.0f && widePixel.x < 7.0f);

		MultiheadScene inverseRangeScene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			inverseRangeScene.AddOutput(40,
				BRect(-maxFloat, 0, -maxFloat, 0), 0.5f));
		BPoint wideGlobal;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			inverseRangeScene.OutputPixelToGlobal(40,
				BPoint(maxFloat, 0), wideGlobal));
		CPPUNIT_ASSERT(std::isfinite(wideGlobal.x));
		CPPUNIT_ASSERT(wideGlobal.x > 3.0e38f);
	}

	void FractionalOutputOriginsMapToLocalPixels()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(50, BRect(1, 1, 100, 100), 1.5f));

		BPoint pixel;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.GlobalToOutputPixel(50, BPoint(2.0f, 3.0f), pixel));
		CPPUNIT_ASSERT(std::fabs(pixel.x - 1.5f) < 0.001f);
		CPPUNIT_ASSERT(std::fabs(pixel.y - 3.0f) < 0.001f);

		BPoint global;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.OutputPixelToGlobal(50, pixel, global));
		CPPUNIT_ASSERT(std::fabs(global.x - 2.0f) < 0.001f);
		CPPUNIT_ASSERT(std::fabs(global.y - 3.0f) < 0.001f);

		BRegion damage;
		damage.Include(BRect(1, 1, 2, 2));
		BRegion pixels;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.ClipRegionToOutput(50, damage, 150, 150, pixels));
		CPPUNIT_ASSERT(pixels.CountRects() > 0);
		CPPUNIT_ASSERT(pixels.Frame().left == 0.0f);
		CPPUNIT_ASSERT(pixels.Frame().top == 0.0f);
	}

	void NegativeOriginMapsToLocalPixels()
	{
		MultiheadScene scene;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.AddOutput(10, BRect(-100, -50, -1, 49), 2.0f));

		BPoint pixel;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.GlobalToOutputPixel(10, BPoint(-90, -40), pixel));
		CPPUNIT_ASSERT(pixel == BPoint(20, 20));

		BPoint global;
		CPPUNIT_ASSERT_EQUAL((status_t)B_OK,
			scene.OutputPixelToGlobal(10, pixel, global));
		CPPUNIT_ASSERT(global == BPoint(-90, -40));
	}
};


CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(MultiheadSceneTest, getTestSuiteName());
