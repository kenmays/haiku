/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include "MultiheadScene.h"

#include <Region.h>

#include <TestSuiteAddon.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>


class MultiheadSceneTest : public CppUnit::TestFixture {
	CPPUNIT_TEST_SUITE(MultiheadSceneTest);
	CPPUNIT_TEST(OutputSelectionUsesGlobalCoordinates);
	CPPUNIT_TEST(MixedScalePixelClippingRoundsOutwards);
	CPPUNIT_TEST(NegativeOriginMapsToLocalPixels);
	CPPUNIT_TEST(GenerationTracksOutputChanges);
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
