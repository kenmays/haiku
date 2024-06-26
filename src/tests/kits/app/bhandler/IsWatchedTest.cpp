//------------------------------------------------------------------------------
//	IsWatchedTest.cpp
//
//------------------------------------------------------------------------------

// Standard Includes -----------------------------------------------------------

// System Includes -------------------------------------------------------------

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------
#include "IsWatchedTest.h"

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------

//------------------------------------------------------------------------------
/**
	IsWatched()
	@case		No added watchers
	@results	Returns false
 */
void TIsWatchedTest::IsWatched1()
{
	CPPUNIT_ASSERT(!fHandler.IsWatched());
}
//------------------------------------------------------------------------------
/**
	IsWatched()
	@case		Add then remove watcher
	@results	Returns true after add, returns false after remove
	@note		Original implementation fails this test.  Either the removal
				doesn't happen (unlikely) or some list-within-a-list doesn't
				get removed when there's nothing in it anymore.
 */
void TIsWatchedTest::IsWatched2()
{
	BHandler Watcher;
	fHandler.StartWatching(&Watcher, '1234');
	CPPUNIT_ASSERT(fHandler.IsWatched() == true);

	fHandler.StopWatching(&Watcher, '1234');
#ifndef TEST_R5
	CPPUNIT_ASSERT(fHandler.IsWatched() == false);
#endif
}
//------------------------------------------------------------------------------
/**
	IsWatched()
	@case		Add watcher, send notice, then remove watcher
	@results	Returns true after add, returns false after remove
 */
void TIsWatchedTest::IsWatched3()
{
	BHandler Watcher;
	fHandler.StartWatching(&Watcher, '1234');
	CPPUNIT_ASSERT(fHandler.IsWatched() == true);

	fHandler.SendNotices('1234');

	fHandler.StopWatching(&Watcher, '1234');
	CPPUNIT_ASSERT(fHandler.IsWatched() == false);
}
//------------------------------------------------------------------------------
/**
	IsWatched()
	@case		Remove inexistent watcher
	@results	Returns false
 */
void TIsWatchedTest::IsWatched4()
{
	BHandler Watcher;

	fHandler.StopWatching(&Watcher, '1234');
	CPPUNIT_ASSERT(fHandler.IsWatched() == false);
}
//------------------------------------------------------------------------------
/**
	IsWatched()
	@case		Send notices without watchers
	@results	Returns false
 */
void TIsWatchedTest::IsWatched5()
{
	BHandler Watcher;

	// Create handler's internal list
	fHandler.StartWatching(&Watcher, '1234');
	fHandler.StopWatching(&Watcher, '1234');

	fHandler.SendNotices('1234');
	CPPUNIT_ASSERT(fHandler.IsWatched() == false);
}
//------------------------------------------------------------------------------
Test* TIsWatchedTest::Suite()
{
	TestSuite* SuiteOfTests = new TestSuite("BHandler::IsWatched");

	ADD_TEST4(BHandler, SuiteOfTests, TIsWatchedTest, IsWatched1);
	ADD_TEST4(BHandler, SuiteOfTests, TIsWatchedTest, IsWatched2);
	ADD_TEST4(BHandler, SuiteOfTests, TIsWatchedTest, IsWatched3);
	ADD_TEST4(BHandler, SuiteOfTests, TIsWatchedTest, IsWatched4);
	ADD_TEST4(BHandler, SuiteOfTests, TIsWatchedTest, IsWatched5);

	return SuiteOfTests;
}
//------------------------------------------------------------------------------

/*
 * $Log $
 *
 * $Id  $
 *
 */


