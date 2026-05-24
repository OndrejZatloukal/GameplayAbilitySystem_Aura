// Copyright Druid Mechanics


#include "Actor/AuraBuildPiece.h"

#include "Aura/Aura.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

namespace
{
	FAuraBuildPieceDefinition MakeFloorDefinition()
	{
		FAuraBuildPieceDefinition Definition;
		Definition.WoodCost = 2;
		Definition.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Assets/Dungeon/SM_Tile_2x2.SM_Tile_2x2"));
		Definition.MeshScale = FVector(1.f, 1.f, 0.32f);
		Definition.MeshOffset = FVector::ZeroVector;
		Definition.HalfExtent = FVector(100.f, 100.f, 10.f);
		Definition.PreviewLabelOffset = FVector(0.f, 0.f, 120.f);
		Definition.SnapPoints = {
			{FVector(100.f, 0.f, 0.f), FRotator(0.f, 0.f, 0.f), EAuraBuildSnapType::FloorEdge},
			{FVector(-100.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f), EAuraBuildSnapType::FloorEdge},
			{FVector(0.f, 100.f, 0.f), FRotator(0.f, 90.f, 0.f), EAuraBuildSnapType::FloorEdge},
			{FVector(0.f, -100.f, 0.f), FRotator(0.f, -90.f, 0.f), EAuraBuildSnapType::FloorEdge},
			{FVector(0.f, 0.f, 10.f), FRotator(0.f, 0.f, 0.f), EAuraBuildSnapType::FloorTop},
			{FVector(0.f, 0.f, -10.f), FRotator(0.f, 0.f, 0.f), EAuraBuildSnapType::FloorBottom}
		};
		return Definition;
	}

	FAuraBuildPieceDefinition MakeWallDefinition()
	{
		FAuraBuildPieceDefinition Definition;
		Definition.WoodCost = 4;
		Definition.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Assets/Dungeon/SM_Wall_2x2.SM_Wall_2x2"));
		Definition.MeshScale = FVector(1.f, 1.f, 1.f);
		Definition.MeshOffset = FVector(0.f, 0.f, 100.f);
		Definition.HalfExtent = FVector(10.f, 100.f, 100.f);
		Definition.PreviewLabelOffset = FVector(0.f, 0.f, 240.f);
		Definition.SnapPoints = {
			{FVector(0.f, 0.f, 100.f), FRotator(0.f, 0.f, 0.f), EAuraBuildSnapType::WallTop},
			{FVector(0.f, 0.f, -100.f), FRotator(0.f, 0.f, 0.f), EAuraBuildSnapType::WallBottom},
			{FVector(0.f, 100.f, 0.f), FRotator(0.f, 90.f, 0.f), EAuraBuildSnapType::WallSide},
			{FVector(0.f, -100.f, 0.f), FRotator(0.f, -90.f, 0.f), EAuraBuildSnapType::WallSide}
		};
		return Definition;
	}
}

AAuraBuildPiece::AAuraBuildPiece()
{
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>("SceneRoot");
	SetRootComponent(SceneRoot);

	BuildMesh = CreateDefaultSubobject<UStaticMeshComponent>("BuildMesh");
	BuildMesh->SetupAttachment(SceneRoot);
	BuildMesh->SetMobility(EComponentMobility::Movable);
	BuildMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BuildMesh->SetCollisionObjectType(ECC_WorldDynamic);
	BuildMesh->SetCollisionResponseToAllChannels(ECR_Block);
	BuildMesh->SetRenderCustomDepth(false);
	BuildMesh->SetCustomDepthStencilValue(CUSTOM_DEPTH_BLUE);

	PreviewLabel = CreateDefaultSubobject<UTextRenderComponent>("PreviewLabel");
	PreviewLabel->SetupAttachment(SceneRoot);
	PreviewLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	PreviewLabel->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	PreviewLabel->SetWorldSize(36.f);
	PreviewLabel->SetTextRenderColor(FColor(100, 200, 255));
	PreviewLabel->SetText(FText::FromString(TEXT("Floor")));

	ApplyPieceDefinition();
}

void AAuraBuildPiece::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAuraBuildPiece, PieceType);
}

void AAuraBuildPiece::ConfigureBuildPiece(EAuraBuildPieceType InPieceType, bool bInPreviewMode)
{
	PieceType = InPieceType;
	bPreviewMode = bInPreviewMode;
	ApplyPieceDefinition();
	ApplyPreviewState();
}

void AAuraBuildPiece::SetBuildValid(const bool bIsValidPlacement) const
{
	BuildMesh->SetRenderCustomDepth(true);
	BuildMesh->SetCustomDepthStencilValue(bIsValidPlacement ? CUSTOM_DEPTH_BLUE : CUSTOM_DEPTH_RED);
	PreviewLabel->SetTextRenderColor(bIsValidPlacement ? FColor(100, 200, 255) : FColor(255, 90, 90));
}

void AAuraBuildPiece::SetStructuralSupportVisual(const int32 SupportStrength, const int32 MaxSupportStrength) const
{
	if (bPreviewMode)
	{
		return;
	}

	int32 StencilValue = CUSTOM_DEPTH_RED;
	if (SupportStrength >= MaxSupportStrength)
	{
		StencilValue = CUSTOM_DEPTH_BLUE;
	}
	else if (SupportStrength > 0)
	{
		StencilValue = CUSTOM_DEPTH_TAN;
	}

	SetInspectionStencil(StencilValue);
}

void AAuraBuildPiece::SetInspectionStencil(const int32 StencilValue) const
{
	if (bPreviewMode)
	{
		return;
	}

	BuildMesh->SetRenderCustomDepth(true);
	BuildMesh->SetCustomDepthStencilValue(StencilValue);
}

void AAuraBuildPiece::ClearStructuralSupportVisual() const
{
	if (bPreviewMode)
	{
		return;
	}

	BuildMesh->SetRenderCustomDepth(false);
	BuildMesh->SetCustomDepthStencilValue(CUSTOM_DEPTH_BLUE);
}

const FAuraBuildPieceDefinition& AAuraBuildPiece::GetPieceDefinition(const EAuraBuildPieceType InPieceType)
{
	static FAuraBuildPieceDefinition FloorDefinition = MakeFloorDefinition();
	static FAuraBuildPieceDefinition WallDefinition = MakeWallDefinition();
	static UStaticMesh* FallbackMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	static FAuraBuildPieceDefinition FallbackDefinition = []()
	{
		FAuraBuildPieceDefinition Definition;
		Definition.WoodCost = 2;
		Definition.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		Definition.MeshScale = FVector(2.f, 2.f, 0.2f);
		Definition.HalfExtent = FVector(100.f, 100.f, 10.f);
		Definition.PreviewLabelOffset = FVector(0.f, 0.f, 120.f);
		return Definition;
	}();

	auto ResolveDefinition = [&](FAuraBuildPieceDefinition& Definition) -> const FAuraBuildPieceDefinition&
	{
		if (!IsValid(Definition.Mesh))
		{
			Definition.Mesh = FallbackMesh;
		}
		return Definition;
	};

	switch (InPieceType)
	{
	case EAuraBuildPieceType::Wall:
		return ResolveDefinition(WallDefinition);
	case EAuraBuildPieceType::Floor:
	default:
		return ResolveDefinition(FloorDefinition);
	}
}

int32 AAuraBuildPiece::GetPieceWoodCost(const EAuraBuildPieceType InPieceType)
{
	return GetPieceDefinition(InPieceType).WoodCost;
}

FVector AAuraBuildPiece::GetPieceHalfExtent(const EAuraBuildPieceType InPieceType)
{
	return GetPieceDefinition(InPieceType).HalfExtent;
}

TArray<FAuraBuildSnapPoint> AAuraBuildPiece::GetSnapPoints(const EAuraBuildPieceType InPieceType)
{
	return GetPieceDefinition(InPieceType).SnapPoints;
}

int32 AAuraBuildPiece::GetSnapConnectionSupportCost(const EAuraBuildSnapType FirstSnapType, const EAuraBuildSnapType SecondSnapType)
{
	if ((FirstSnapType == EAuraBuildSnapType::WallTop && SecondSnapType == EAuraBuildSnapType::WallBottom)
		|| (FirstSnapType == EAuraBuildSnapType::WallBottom && SecondSnapType == EAuraBuildSnapType::WallTop)
		|| (FirstSnapType == EAuraBuildSnapType::WallTop && SecondSnapType == EAuraBuildSnapType::FloorBottom)
		|| (FirstSnapType == EAuraBuildSnapType::FloorBottom && SecondSnapType == EAuraBuildSnapType::WallTop)
		|| (FirstSnapType == EAuraBuildSnapType::FloorTop && SecondSnapType == EAuraBuildSnapType::FloorBottom)
		|| (FirstSnapType == EAuraBuildSnapType::FloorBottom && SecondSnapType == EAuraBuildSnapType::FloorTop)
		|| (FirstSnapType == EAuraBuildSnapType::FloorEdge && SecondSnapType == EAuraBuildSnapType::WallBottom)
		|| (FirstSnapType == EAuraBuildSnapType::WallBottom && SecondSnapType == EAuraBuildSnapType::FloorEdge))
	{
		return 1;
	}

	if ((FirstSnapType == EAuraBuildSnapType::FloorEdge && SecondSnapType == EAuraBuildSnapType::FloorEdge)
		|| (FirstSnapType == EAuraBuildSnapType::WallSide && SecondSnapType == EAuraBuildSnapType::WallSide))
	{
		return 2;
	}

	return INDEX_NONE;
}

bool AAuraBuildPiece::AreSnapTypesConnected(const EAuraBuildSnapType FirstSnapType, const EAuraBuildSnapType SecondSnapType)
{
	return GetSnapConnectionSupportCost(FirstSnapType, SecondSnapType) != INDEX_NONE;
}

TArray<FTransform> AAuraBuildPiece::GetWorldSnapPointTransforms() const
{
	TArray<FTransform> WorldSnapPoints;
	for (const FAuraBuildSnapPoint& SnapPoint : GetSnapPoints(PieceType))
	{
		const FVector WorldLocation = GetActorTransform().TransformPosition(SnapPoint.LocalLocation);
		const FRotator WorldRotation = (GetActorRotation() + SnapPoint.Rotation).GetNormalized();
		WorldSnapPoints.Add(FTransform(WorldRotation, WorldLocation));
	}

	return WorldSnapPoints;
}

void AAuraBuildPiece::OnRep_PieceType()
{
	ApplyPieceDefinition();
}

void AAuraBuildPiece::LoadActor_Implementation()
{
	ApplyPieceDefinition();
	ApplyPreviewState();
}

void AAuraBuildPiece::ApplyPieceDefinition() const
{
	const FAuraBuildPieceDefinition& Definition = GetPieceDefinition(PieceType);
	BuildMesh->SetStaticMesh(Definition.Mesh);
	BuildMesh->SetRelativeLocation(Definition.MeshOffset);
	BuildMesh->SetRelativeScale3D(Definition.MeshScale);
	PreviewLabel->SetRelativeLocation(Definition.PreviewLabelOffset);
	PreviewLabel->SetText(FText::FromString(GetPieceDisplayName()));
}

void AAuraBuildPiece::ApplyPreviewState() const
{
	BuildMesh->SetCollisionEnabled(bPreviewMode ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	BuildMesh->SetCastShadow(!bPreviewMode);
	BuildMesh->SetRenderCustomDepth(bPreviewMode);
	BuildMesh->SetCustomDepthStencilValue(CUSTOM_DEPTH_BLUE);
	PreviewLabel->SetHiddenInGame(!bPreviewMode);
}

FString AAuraBuildPiece::GetPieceDisplayName() const
{
	switch (PieceType)
	{
	case EAuraBuildPieceType::Wall:
		return TEXT("Wall  |  1/2 Select  Q/E Switch  R Rotate  LMB Place  RMB Remove  Esc Exit");
	case EAuraBuildPieceType::Floor:
	default:
		return TEXT("Floor  |  1/2 Select  Q/E Switch  R Rotate  LMB Place  RMB Remove  Esc Exit");
	}
}
