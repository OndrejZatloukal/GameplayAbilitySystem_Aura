// Copyright Druid Mechanics

#include "Actor/AuraBuildPiece.h"
#include "Game/LoadScreenSaveGame.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAuraBuildPieceDefinitionTest,
	"Aura.BuildSystem.BuildPieceDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAuraBuildPieceDefinitionTest::RunTest(const FString& Parameters)
{
	const FAuraBuildPieceDefinition& FloorDefinition = AAuraBuildPiece::GetPieceDefinition(EAuraBuildPieceType::Floor);
	const FAuraBuildPieceDefinition& WallDefinition = AAuraBuildPiece::GetPieceDefinition(EAuraBuildPieceType::Wall);

	TestNotNull(TEXT("Floor mesh should resolve"), FloorDefinition.Mesh.Get());
	TestNotNull(TEXT("Wall mesh should resolve"), WallDefinition.Mesh.Get());
	TestTrue(TEXT("Floor half extent should be non-zero"), FloorDefinition.HalfExtent.GetMin() > 0.f);
	TestTrue(TEXT("Wall half extent should be non-zero"), WallDefinition.HalfExtent.GetMin() > 0.f);
	TestEqual(TEXT("Floor wood cost should stay at two"), FloorDefinition.WoodCost, 2);
	TestEqual(TEXT("Wall wood cost should stay at four"), WallDefinition.WoodCost, 4);
	TestEqual(TEXT("Shared floor wood cost helper should match definition"), AAuraBuildPiece::GetPieceWoodCost(EAuraBuildPieceType::Floor), 2);
	TestEqual(TEXT("Shared wall wood cost helper should match definition"), AAuraBuildPiece::GetPieceWoodCost(EAuraBuildPieceType::Wall), 4);
	ULoadScreenSaveGame* SaveGame = NewObject<ULoadScreenSaveGame>();
	TestNotNull(TEXT("Build save game object should instantiate"), SaveGame);
	if (SaveGame)
	{
		TestEqual(TEXT("Build wood should default to the starting amount"), SaveGame->BuildWood, 80);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAuraBuildPieceSnapTopologyTest,
	"Aura.BuildSystem.SnapTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAuraBuildPieceSnapTopologyTest::RunTest(const FString& Parameters)
{
	const TArray<FAuraBuildSnapPoint> FloorSnapPoints = AAuraBuildPiece::GetSnapPoints(EAuraBuildPieceType::Floor);
	const TArray<FAuraBuildSnapPoint> WallSnapPoints = AAuraBuildPiece::GetSnapPoints(EAuraBuildPieceType::Wall);

	TestEqual(TEXT("Floor should expose six snap points"), FloorSnapPoints.Num(), 6);
	TestEqual(TEXT("Wall should expose four snap points"), WallSnapPoints.Num(), 4);

	int32 FloorEdgeCount = 0;
	int32 FloorTopCount = 0;
	int32 FloorBottomCount = 0;
	for (const FAuraBuildSnapPoint& SnapPoint : FloorSnapPoints)
	{
		FloorEdgeCount += SnapPoint.SnapType == EAuraBuildSnapType::FloorEdge ? 1 : 0;
		FloorTopCount += SnapPoint.SnapType == EAuraBuildSnapType::FloorTop ? 1 : 0;
		FloorBottomCount += SnapPoint.SnapType == EAuraBuildSnapType::FloorBottom ? 1 : 0;
	}

	TestEqual(TEXT("Floor should expose four floor-edge snaps"), FloorEdgeCount, 4);
	TestEqual(TEXT("Floor should expose one floor-top snap"), FloorTopCount, 1);
	TestEqual(TEXT("Floor should expose one floor-bottom snap"), FloorBottomCount, 1);

	int32 WallSideCount = 0;
	int32 WallTopCount = 0;
	int32 WallBottomCount = 0;
	for (const FAuraBuildSnapPoint& SnapPoint : WallSnapPoints)
	{
		WallSideCount += SnapPoint.SnapType == EAuraBuildSnapType::WallSide ? 1 : 0;
		WallTopCount += SnapPoint.SnapType == EAuraBuildSnapType::WallTop ? 1 : 0;
		WallBottomCount += SnapPoint.SnapType == EAuraBuildSnapType::WallBottom ? 1 : 0;
	}

	TestEqual(TEXT("Wall should expose two wall-side snaps"), WallSideCount, 2);
	TestEqual(TEXT("Wall should expose one wall-top snap"), WallTopCount, 1);
	TestEqual(TEXT("Wall should expose one wall-bottom snap"), WallBottomCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAuraBuildPieceStructuralRulesTest,
	"Aura.BuildSystem.StructuralRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAuraBuildPieceStructuralRulesTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Floor edge spans should cost two support strength"),
		AAuraBuildPiece::GetSnapConnectionSupportCost(EAuraBuildSnapType::FloorEdge, EAuraBuildSnapType::FloorEdge),
		2);
	TestEqual(
		TEXT("Wall side chains should cost two support strength"),
		AAuraBuildPiece::GetSnapConnectionSupportCost(EAuraBuildSnapType::WallSide, EAuraBuildSnapType::WallSide),
		2);

	TestEqual(
		TEXT("Wall stacking should cost one support strength"),
		AAuraBuildPiece::GetSnapConnectionSupportCost(EAuraBuildSnapType::WallTop, EAuraBuildSnapType::WallBottom),
		1);
	TestEqual(
		TEXT("Floor stacking should cost one support strength"),
		AAuraBuildPiece::GetSnapConnectionSupportCost(EAuraBuildSnapType::FloorTop, EAuraBuildSnapType::FloorBottom),
		1);
	TestEqual(
		TEXT("Wall-to-floor support should cost one support strength"),
		AAuraBuildPiece::GetSnapConnectionSupportCost(EAuraBuildSnapType::WallTop, EAuraBuildSnapType::FloorBottom),
		1);
	TestEqual(
		TEXT("Wall-on-floor support should cost one support strength"),
		AAuraBuildPiece::GetSnapConnectionSupportCost(EAuraBuildSnapType::FloorEdge, EAuraBuildSnapType::WallBottom),
		1);

	TestEqual(
		TEXT("Incompatible snap types should not connect"),
		AAuraBuildPiece::GetSnapConnectionSupportCost(EAuraBuildSnapType::FloorTop, EAuraBuildSnapType::WallSide),
		INDEX_NONE);
	TestTrue(
		TEXT("Connected snap types should report true"),
		AAuraBuildPiece::AreSnapTypesConnected(EAuraBuildSnapType::WallTop, EAuraBuildSnapType::FloorBottom));
	TestFalse(
		TEXT("Incompatible snap types should report false"),
		AAuraBuildPiece::AreSnapTypesConnected(EAuraBuildSnapType::FloorTop, EAuraBuildSnapType::WallSide));

	return true;
}

#endif
