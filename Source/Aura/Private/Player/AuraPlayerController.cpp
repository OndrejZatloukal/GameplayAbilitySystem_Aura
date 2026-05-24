// Copyright Druid Mechanics


#include "Player/AuraPlayerController.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AuraGameplayTags.h"
#include "Engine/OverlapResult.h"
#include "EnhancedInputSubsystems.h"
#include "Game/AuraGameInstance.h"
#include "Game/AuraGameModeBase.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "Actor/MagicCircle.h"
#include "Actor/AuraBuildPiece.h"
#include "Aura/Aura.h"
#include "Components/DecalComponent.h"
#include "Components/SplineComponent.h"
#include "InputAction.h"
#include "Input/AuraInputComponent.h"
#include "InputMappingContext.h"
#include "Interaction/EnemyInterface.h"
#include "Game/LoadScreenSaveGame.h"
#include "GameFramework/Character.h"
#include "Interaction/HighlightInterface.h"
#include "Net/UnrealNetwork.h"
#include "UI/Widget/BuildModeWidget.h"
#include "UI/Widget/DamageTextComponent.h"

namespace
{
	constexpr float BuildSnapConnectionTolerance = 35.f;

	FString GetSnapTypeLabel(const EAuraBuildSnapType SnapType)
	{
		switch (SnapType)
		{
		case EAuraBuildSnapType::FloorEdge:
			return TEXT("Floor Edge");
		case EAuraBuildSnapType::FloorTop:
			return TEXT("Floor Top");
		case EAuraBuildSnapType::FloorBottom:
			return TEXT("Floor Bottom");
		case EAuraBuildSnapType::WallSide:
			return TEXT("Wall Side");
		case EAuraBuildSnapType::WallTop:
			return TEXT("Wall Top");
		case EAuraBuildSnapType::WallBottom:
			return TEXT("Wall Bottom");
		default:
			return TEXT("Unknown");
		}
	}

	FString GetPieceTypeLabel(const EAuraBuildPieceType PieceType)
	{
		return PieceType == EAuraBuildPieceType::Wall ? TEXT("Wall") : TEXT("Floor");
	}

	bool IsSnapCompatible(const EAuraBuildPieceType SelectedPieceType, const EAuraBuildSnapType SnapType)
	{
		switch (SelectedPieceType)
		{
		case EAuraBuildPieceType::Floor:
			return SnapType == EAuraBuildSnapType::FloorEdge
				|| SnapType == EAuraBuildSnapType::WallTop
				|| SnapType == EAuraBuildSnapType::FloorTop;
		case EAuraBuildPieceType::Wall:
			return SnapType == EAuraBuildSnapType::FloorEdge || SnapType == EAuraBuildSnapType::WallTop
				|| SnapType == EAuraBuildSnapType::WallBottom || SnapType == EAuraBuildSnapType::WallSide;
		default:
			return false;
		}
	}

	bool RequiresOpposingSnapRotation(const EAuraBuildSnapType FirstSnapType, const EAuraBuildSnapType SecondSnapType)
	{
		return (FirstSnapType == EAuraBuildSnapType::FloorEdge && SecondSnapType == EAuraBuildSnapType::FloorEdge)
			|| (FirstSnapType == EAuraBuildSnapType::WallSide && SecondSnapType == EAuraBuildSnapType::WallSide);
	}

	bool IsSnapRotationCompatible(
		const FAuraBuildSnapPoint& SelectedSnapPoint,
		const FRotator& CandidatePieceRotation,
		const FAuraBuildSnapPoint& TargetSnapPoint,
		const FRotator& TargetSnapRotation)
	{
		if (!AAuraBuildPiece::AreSnapTypesConnected(SelectedSnapPoint.SnapType, TargetSnapPoint.SnapType))
		{
			return false;
		}

		if (!RequiresOpposingSnapRotation(SelectedSnapPoint.SnapType, TargetSnapPoint.SnapType))
		{
			return true;
		}

		const FRotator SelectedWorldRotation = (CandidatePieceRotation + SelectedSnapPoint.Rotation).GetNormalized();
		const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(SelectedWorldRotation.Yaw, TargetSnapRotation.Yaw));
		return YawDelta >= 135.f;
	}

	TArray<FTransform> GetPlacementSnapPointTransforms(const FVector& Location, const FRotator& Rotation, const EAuraBuildPieceType PieceType)
	{
		TArray<FTransform> WorldSnapPoints;
		const FTransform PieceTransform(Rotation, Location);
		for (const FAuraBuildSnapPoint& SnapPoint : AAuraBuildPiece::GetSnapPoints(PieceType))
		{
			const FVector WorldLocation = PieceTransform.TransformPosition(SnapPoint.LocalLocation);
			const FRotator WorldRotation = (Rotation + SnapPoint.Rotation).GetNormalized();
			WorldSnapPoints.Add(FTransform(WorldRotation, WorldLocation));
		}

		return WorldSnapPoints;
	}

	int32 GetSnapPointSetsConnectionCost(
		const TArray<FAuraBuildSnapPoint>& FirstLocalSnapPoints,
		const TArray<FTransform>& FirstWorldSnapPoints,
		const TArray<FAuraBuildSnapPoint>& SecondLocalSnapPoints,
		const TArray<FTransform>& SecondWorldSnapPoints)
	{
		int32 BestConnectionCost = INDEX_NONE;

		for (int32 FirstIndex = 0; FirstIndex < FirstWorldSnapPoints.Num(); ++FirstIndex)
		{
			if (!FirstLocalSnapPoints.IsValidIndex(FirstIndex))
			{
				continue;
			}

			for (int32 SecondIndex = 0; SecondIndex < SecondWorldSnapPoints.Num(); ++SecondIndex)
			{
				if (!SecondLocalSnapPoints.IsValidIndex(SecondIndex))
				{
					continue;
				}

				const int32 ConnectionCost = AAuraBuildPiece::GetSnapConnectionSupportCost(
					FirstLocalSnapPoints[FirstIndex].SnapType,
					SecondLocalSnapPoints[SecondIndex].SnapType);
				if (ConnectionCost == INDEX_NONE)
				{
					continue;
				}

				if (FVector::DistSquared(FirstWorldSnapPoints[FirstIndex].GetLocation(), SecondWorldSnapPoints[SecondIndex].GetLocation()) > FMath::Square(BuildSnapConnectionTolerance))
				{
					continue;
				}

				if (BestConnectionCost == INDEX_NONE || ConnectionCost < BestConnectionCost)
				{
					BestConnectionCost = ConnectionCost;
				}
			}
		}

		return BestConnectionCost;
	}

	bool AreSnapPointSetsConnected(
		const TArray<FAuraBuildSnapPoint>& FirstLocalSnapPoints,
		const TArray<FTransform>& FirstWorldSnapPoints,
		const TArray<FAuraBuildSnapPoint>& SecondLocalSnapPoints,
		const TArray<FTransform>& SecondWorldSnapPoints)
	{
		return GetSnapPointSetsConnectionCost(
			FirstLocalSnapPoints,
			FirstWorldSnapPoints,
			SecondLocalSnapPoints,
			SecondWorldSnapPoints) != INDEX_NONE;
	}
}

AAuraPlayerController::AAuraPlayerController()
{
	bReplicates = true;
	Spline = CreateDefaultSubobject<USplineComponent>("Spline");
	StarterBuildPieces = {EAuraBuildPieceType::Floor, EAuraBuildPieceType::Wall};
	BuildPieceClass = AAuraBuildPiece::StaticClass();

	BuildModeContext = CreateDefaultSubobject<UInputMappingContext>("BuildModeContext");
	ToggleBuildModeAction = CreateDefaultSubobject<UInputAction>("ToggleBuildModeAction");
	RotateBuildPieceAction = CreateDefaultSubobject<UInputAction>("RotateBuildPieceAction");
	PreviousBuildPieceAction = CreateDefaultSubobject<UInputAction>("PreviousBuildPieceAction");
	NextBuildPieceAction = CreateDefaultSubobject<UInputAction>("NextBuildPieceAction");
	CancelBuildModeAction = CreateDefaultSubobject<UInputAction>("CancelBuildModeAction");
	RemoveBuildPieceAction = CreateDefaultSubobject<UInputAction>("RemoveBuildPieceAction");
	SelectFirstBuildPieceAction = CreateDefaultSubobject<UInputAction>("SelectFirstBuildPieceAction");
	SelectSecondBuildPieceAction = CreateDefaultSubobject<UInputAction>("SelectSecondBuildPieceAction");

	ToggleBuildModeAction->ValueType = EInputActionValueType::Boolean;
	RotateBuildPieceAction->ValueType = EInputActionValueType::Boolean;
	PreviousBuildPieceAction->ValueType = EInputActionValueType::Boolean;
	NextBuildPieceAction->ValueType = EInputActionValueType::Boolean;
	CancelBuildModeAction->ValueType = EInputActionValueType::Boolean;
	RemoveBuildPieceAction->ValueType = EInputActionValueType::Boolean;
	SelectFirstBuildPieceAction->ValueType = EInputActionValueType::Boolean;
	SelectSecondBuildPieceAction->ValueType = EInputActionValueType::Boolean;
}

void AAuraPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAuraPlayerController, AvailableBuildWood);
}

void AAuraPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	CursorTrace();
	AutoRun();
	UpdateMagicCircleLocation();
	UpdateBuildPreviewLocation();
	UpdateBuildSupportVisualization();
}

void AAuraPlayerController::ShowMagicCircle(UMaterialInterface* DecalMaterial)
{
	if (!IsValid(MagicCircle))
	{
		MagicCircle = GetWorld()->SpawnActor<AMagicCircle>(MagicCircleClass);
		if (DecalMaterial)
		{
			MagicCircle->MagicCircleDecal->SetMaterial(0, DecalMaterial);
		}
	}
}

void AAuraPlayerController::HideMagicCircle()
{
	if (IsValid(MagicCircle))
	{
		MagicCircle->Destroy();
		MagicCircle = nullptr;
	}
}

void AAuraPlayerController::ShowDamageNumber_Implementation(float DamageAmount, ACharacter* TargetCharacter, bool bBlockedHit, bool bCriticalHit)
{
	if (IsValid(TargetCharacter) && DamageTextComponentClass && IsLocalController())
	{
		UDamageTextComponent* DamageText = NewObject<UDamageTextComponent>(TargetCharacter, DamageTextComponentClass);
		DamageText->RegisterComponent();
		DamageText->AttachToComponent(TargetCharacter->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		DamageText->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		DamageText->SetDamageText(DamageAmount, bBlockedHit, bCriticalHit);
	}
}

void AAuraPlayerController::AutoRun()
{
	if (!bAutoRunning) return;
	if (APawn* ControlledPawn = GetPawn())
	{
		const FVector LocationOnSpline = Spline->FindLocationClosestToWorldLocation(ControlledPawn->GetActorLocation(), ESplineCoordinateSpace::World);
		const FVector Direction = Spline->FindDirectionClosestToWorldLocation(LocationOnSpline, ESplineCoordinateSpace::World);
		ControlledPawn->AddMovementInput(Direction);

		const float DistanceToDestination = (LocationOnSpline - CachedDestination).Length();
		if (DistanceToDestination <= AutoRunAcceptanceRadius)
		{
			bAutoRunning = false;
		}
	}
}

void AAuraPlayerController::UpdateMagicCircleLocation()
{
	if (IsValid(MagicCircle))
	{
		MagicCircle->SetActorLocation(CursorHit.ImpactPoint);
	}
}

void AAuraPlayerController::ToggleBuildMode()
{
	if (bBuildModeActive)
	{
		ExitBuildMode();
		return;
	}

	EnterBuildMode();
}

void AAuraPlayerController::EnterBuildMode()
{
	if (bBuildModeActive)
	{
		return;
	}

	bBuildModeActive = true;
	bAutoRunning = false;
	FollowTime = 0.f;
	TargetingStatus = ETargetingStatus::NotTargeting;
	if (!IsValid(BuildModeWidget))
	{
		BuildModeWidget = CreateWidget<UBuildModeWidget>(this, UBuildModeWidget::StaticClass());
	}
	if (IsValid(BuildModeWidget) && !BuildModeWidget->IsInViewport())
	{
		BuildModeWidget->AddToViewport(20);
	}
	RefreshBuildPreview();
	UpdateBuildModeWidget();
}

void AAuraPlayerController::ExitBuildMode()
{
	bBuildModeActive = false;
	bCanPlaceCurrentBuildPiece = false;
	CurrentBuildSupportStrength = 0;
	ClearBuildSupportVisualization();

	if (IsValid(BuildPreviewActor))
	{
		BuildPreviewActor->Destroy();
		BuildPreviewActor = nullptr;
	}

	if (IsValid(BuildModeWidget))
	{
		BuildModeWidget->RemoveFromParent();
	}
}

void AAuraPlayerController::CycleBuildPieceForward()
{
	if (!bBuildModeActive || StarterBuildPieces.Num() == 0)
	{
		return;
	}

	SelectedBuildPieceIndex = (SelectedBuildPieceIndex + 1) % StarterBuildPieces.Num();
	RefreshBuildPreview();
}

void AAuraPlayerController::CycleBuildPieceBackward()
{
	if (!bBuildModeActive || StarterBuildPieces.Num() == 0)
	{
		return;
	}

	SelectedBuildPieceIndex = (SelectedBuildPieceIndex - 1 + StarterBuildPieces.Num()) % StarterBuildPieces.Num();
	RefreshBuildPreview();
}

void AAuraPlayerController::SelectFirstBuildPiece()
{
	SelectBuildPieceByIndex(0);
}

void AAuraPlayerController::SelectSecondBuildPiece()
{
	SelectBuildPieceByIndex(1);
}

void AAuraPlayerController::SelectBuildPieceByIndex(const int32 PieceIndex)
{
	if (!bBuildModeActive || !StarterBuildPieces.IsValidIndex(PieceIndex) || SelectedBuildPieceIndex == PieceIndex)
	{
		return;
	}

	SelectedBuildPieceIndex = PieceIndex;
	RefreshBuildPreview();
}

void AAuraPlayerController::RotateBuildPiece()
{
	if (!bBuildModeActive)
	{
		return;
	}

	BuildPieceYaw = FMath::Fmod(BuildPieceYaw + 90.f, 360.f);
	UpdateBuildModeWidget();
}

void AAuraPlayerController::UpdateBuildPreviewLocation()
{
	if (!bBuildModeActive || !IsValid(BuildPreviewActor))
	{
		return;
	}

	FVector BuildLocation;
	FRotator BuildRotation;
	FString SnapDescription;
	if (!TryResolveBuildPlacement(BuildLocation, BuildRotation, &SnapDescription))
	{
		bCanPlaceCurrentBuildPiece = false;
		CurrentBuildSupportStrength = 0;
		CurrentBuildSnapDescription = TEXT("None");
		BuildPreviewActor->SetActorHiddenInGame(true);
		UpdateBuildModeWidget();
		return;
	}

	CurrentBuildSnapDescription = SnapDescription;
	CurrentBuildSupportStrength = GetBuildSupportStrength(BuildLocation, BuildRotation, GetSelectedBuildPieceType());
	bCanPlaceCurrentBuildPiece = CanPlaceBuildPieceAt(BuildLocation, BuildRotation, GetSelectedBuildPieceType());
	BuildPreviewActor->SetActorHiddenInGame(false);
	BuildPreviewActor->SetActorLocationAndRotation(BuildLocation, BuildRotation);
	BuildPreviewActor->SetBuildValid(bCanPlaceCurrentBuildPiece);
	UpdateBuildModeWidget();
}

void AAuraPlayerController::UpdateBuildModeWidget() const
{
	if (IsValid(BuildModeWidget))
	{
		const EAuraBuildPieceType SelectedPieceType = GetSelectedBuildPieceType();
		const bool bHasHoveredBuildPiece = IsValid(HoveredSupportBuildPiece);
		const EAuraBuildPieceType HoveredPieceType = bHasHoveredBuildPiece ? HoveredSupportBuildPiece->GetBuildPieceType() : EAuraBuildPieceType::Floor;
		const int32 HoveredSupportStrength = bHasHoveredBuildPiece
			? GetBuildSupportStrength(HoveredSupportBuildPiece->GetActorLocation(), HoveredSupportBuildPiece->GetActorRotation(), HoveredSupportBuildPiece->GetBuildPieceType())
			: 0;
		const bool bHoveredPieceIsPlayerBuilt = bHasHoveredBuildPiece && IsPlayerBuildPiece(HoveredSupportBuildPiece);
		const bool bCanRemoveHoveredPiece = bHasHoveredBuildPiece && CanRemoveBuildPiece(HoveredSupportBuildPiece);
		const bool bRemovingHoveredPieceIsRisky = bCanRemoveHoveredPiece && WouldRemovingBuildPieceDestabilize(HoveredSupportBuildPiece);

		BuildModeWidget->UpdateBuildState(
			StarterBuildPieces,
			SelectedBuildPieceIndex,
			bCanPlaceCurrentBuildPiece,
			CurrentBuildSnapDescription,
			CurrentBuildSupportStrength,
			MaxBuildSupportStrength,
			AvailableBuildWood,
			bHasHoveredBuildPiece,
			HoveredPieceType,
			HoveredSupportStrength,
			bHoveredPieceIsPlayerBuilt,
			bCanRemoveHoveredPiece,
			bRemovingHoveredPieceIsRisky,
			CurrentHoveredCollapsePreviewCount,
			CurrentHoveredCollapsePreviewWoodRefund);
	}
}

void AAuraPlayerController::RefreshBuildPreview()
{
	if (!bBuildModeActive || !BuildPieceClass)
	{
		return;
	}

	if (!IsValid(BuildPreviewActor))
	{
		BuildPreviewActor = GetWorld()->SpawnActor<AAuraBuildPiece>(BuildPieceClass);
	}

	if (IsValid(BuildPreviewActor))
	{
		BuildPreviewActor->ConfigureBuildPiece(GetSelectedBuildPieceType(), true);
	}
	UpdateBuildModeWidget();
}

void AAuraPlayerController::ClearBuildSupportVisualization()
{
	for (AAuraBuildPiece* BuildPiece : VisualizedSupportBuildPieces)
	{
		if (IsValid(BuildPiece))
		{
			BuildPiece->ClearStructuralSupportVisual();
		}
	}

	VisualizedSupportBuildPieces.Reset();
	HoveredSupportBuildPiece = nullptr;
	CurrentHoveredCollapsePreviewCount = 0;
	CurrentHoveredCollapsePreviewWoodRefund = 0;
}

void AAuraPlayerController::UpdateBuildSupportVisualization()
{
	AAuraBuildPiece* TargetBuildPiece = bBuildModeActive ? Cast<AAuraBuildPiece>(CursorHit.GetActor()) : nullptr;
	if (TargetBuildPiece == BuildPreviewActor)
	{
		TargetBuildPiece = nullptr;
	}

	if (HoveredSupportBuildPiece != TargetBuildPiece)
	{
		ClearBuildSupportVisualization();
		HoveredSupportBuildPiece = TargetBuildPiece;
	}

	if (!IsValid(HoveredSupportBuildPiece))
	{
		return;
	}

	TArray<AAuraBuildPiece*> CollapsePreviewPiecesArray;
	const bool bCanRemoveHoveredPiece = CanRemoveBuildPiece(HoveredSupportBuildPiece);
	if (bCanRemoveHoveredPiece)
	{
		CollectCollapsePreviewPieces(HoveredSupportBuildPiece, CollapsePreviewPiecesArray);
	}
	CurrentHoveredCollapsePreviewCount = CollapsePreviewPiecesArray.Num();
	CurrentHoveredCollapsePreviewWoodRefund = GetBuildWoodRefundForPiece(HoveredSupportBuildPiece) + GetBuildWoodRefundForPieces(CollapsePreviewPiecesArray);
	TSet<AAuraBuildPiece*> CollapsePreviewPieces;
	for (AAuraBuildPiece* BuildPiece : CollapsePreviewPiecesArray)
	{
		CollapsePreviewPieces.Add(BuildPiece);
	}

	TArray<AAuraBuildPiece*> PendingPieces;
	TSet<AAuraBuildPiece*> ProcessedPieces;
	PendingPieces.Add(HoveredSupportBuildPiece);

	for (int32 Index = 0; Index < PendingPieces.Num(); ++Index)
	{
		AAuraBuildPiece* BuildPiece = PendingPieces[Index];
		if (!IsValid(BuildPiece) || ProcessedPieces.Contains(BuildPiece))
		{
			continue;
		}

		ProcessedPieces.Add(BuildPiece);
		const int32 SupportStrength = GetBuildSupportStrength(
			BuildPiece->GetActorLocation(),
			BuildPiece->GetActorRotation(),
			BuildPiece->GetBuildPieceType());
		if (BuildPiece == HoveredSupportBuildPiece && CollapsePreviewPieces.Num() > 0)
		{
			BuildPiece->SetInspectionStencil(CUSTOM_DEPTH_TAN);
		}
		else if (CollapsePreviewPieces.Contains(BuildPiece))
		{
			BuildPiece->SetInspectionStencil(CUSTOM_DEPTH_RED);
		}
		else
		{
			BuildPiece->SetStructuralSupportVisual(SupportStrength, MaxBuildSupportStrength);
		}
		VisualizedSupportBuildPieces.AddUnique(BuildPiece);

		TArray<AAuraBuildPiece*> NearbyPieces;
		CollectNearbyBuildPieces(BuildPiece->GetActorLocation(), NearbyPieces, nullptr);
		for (AAuraBuildPiece* NearbyPiece : NearbyPieces)
		{
			if (IsValid(NearbyPiece) && !ProcessedPieces.Contains(NearbyPiece) && AreBuildPiecesConnected(BuildPiece, NearbyPiece))
			{
				PendingPieces.AddUnique(NearbyPiece);
			}
		}
	}

	UpdateBuildModeWidget();
}

void AAuraPlayerController::TryPlaceBuildPiece()
{
	if (!bBuildModeActive || !bCanPlaceCurrentBuildPiece)
	{
		return;
	}

	FVector BuildLocation;
	FRotator BuildRotation;
	if (!TryResolveBuildPlacement(BuildLocation, BuildRotation))
	{
		return;
	}

	ServerPlaceBuildPiece(GetSelectedBuildPieceType(), BuildLocation, BuildRotation);
}

void AAuraPlayerController::TryRemoveBuildPiece()
{
	if (!bBuildModeActive)
	{
		return;
	}

	if (AAuraBuildPiece* BuildPiece = Cast<AAuraBuildPiece>(CursorHit.GetActor()))
	{
		ServerDestroyBuildPiece(BuildPiece);
	}
}

bool AAuraPlayerController::TryResolveBuildPlacement(FVector& OutLocation, FRotator& OutRotation, FString* OutSnapDescription) const
{
	if (!CursorHit.bBlockingHit || StarterBuildPieces.Num() == 0)
	{
		return false;
	}

	if (!IsWithinBuildRange(CursorHit.ImpactPoint))
	{
		return false;
	}

	const EAuraBuildPieceType SelectedPieceType = GetSelectedBuildPieceType();
	const AAuraBuildPiece* HitBuildPiece = Cast<AAuraBuildPiece>(CursorHit.GetActor());

	OutRotation = FRotator(0.f, BuildPieceYaw, 0.f);
	OutLocation = CursorHit.ImpactPoint + FVector(0.f, 0.f, AAuraBuildPiece::GetPieceHalfExtent(SelectedPieceType).Z);

	if (!HitBuildPiece)
	{
		constexpr float GridSize = 200.f;
		OutLocation.X = FMath::GridSnap(OutLocation.X, GridSize);
		OutLocation.Y = FMath::GridSnap(OutLocation.Y, GridSize);
		OutLocation.Z = FMath::GridSnap(OutLocation.Z, 20.f);
		if (OutSnapDescription)
		{
			*OutSnapDescription = TEXT("Ground Grid");
		}
		return true;
	}

	const TArray<FAuraBuildSnapPoint> LocalSnapPoints = AAuraBuildPiece::GetSnapPoints(HitBuildPiece->GetBuildPieceType());
	const TArray<FTransform> SnapPoints = HitBuildPiece->GetWorldSnapPointTransforms();
	const TArray<FAuraBuildSnapPoint> SelectedPieceSnapPoints = AAuraBuildPiece::GetSnapPoints(SelectedPieceType);
	float BestSnapDistanceSq = TNumericLimits<float>::Max();
	bool bFoundSnapPoint = false;

	for (int32 TargetIndex = 0; TargetIndex < SnapPoints.Num(); ++TargetIndex)
	{
		if (!LocalSnapPoints.IsValidIndex(TargetIndex) || !IsSnapCompatible(SelectedPieceType, LocalSnapPoints[TargetIndex].SnapType))
		{
			continue;
		}

		const FTransform& TargetSnapPoint = SnapPoints[TargetIndex];
		const float DistanceSq = FVector::DistSquared(TargetSnapPoint.GetLocation(), CursorHit.ImpactPoint);
		if (DistanceSq > FMath::Square(MaxBuildSnapDistance) || DistanceSq >= BestSnapDistanceSq)
		{
			continue;
		}

		for (const FAuraBuildSnapPoint& SelectedSnapPoint : SelectedPieceSnapPoints)
		{
			const FRotator CandidateRotation = (TargetSnapPoint.GetRotation().Rotator() + FRotator(0.f, BuildPieceYaw, 0.f)).GetNormalized();
			if (!IsSnapRotationCompatible(
				SelectedSnapPoint,
				CandidateRotation,
				LocalSnapPoints[TargetIndex],
				TargetSnapPoint.GetRotation().Rotator()))
			{
				continue;
			}

			BestSnapDistanceSq = DistanceSq;
			OutRotation = CandidateRotation;
			OutLocation = TargetSnapPoint.GetLocation() - CandidateRotation.RotateVector(SelectedSnapPoint.LocalLocation);
			if (OutSnapDescription)
			{
				*OutSnapDescription = FString::Printf(
					TEXT("%s -> %s / %s"),
					*GetPieceTypeLabel(HitBuildPiece->GetBuildPieceType()),
					*GetSnapTypeLabel(LocalSnapPoints[TargetIndex].SnapType),
					*GetSnapTypeLabel(SelectedSnapPoint.SnapType));
			}
			bFoundSnapPoint = true;
			break;
		}
	}

	return bFoundSnapPoint;
}

bool AAuraPlayerController::IsBuildPieceTypeUnlocked(const EAuraBuildPieceType PieceType) const
{
	return StarterBuildPieces.Contains(PieceType);
}

int32 AAuraPlayerController::GetBuildWoodCost(const EAuraBuildPieceType PieceType) const
{
	return AAuraBuildPiece::GetPieceWoodCost(PieceType);
}

bool AAuraPlayerController::HasBuildResources(const EAuraBuildPieceType PieceType) const
{
	return AvailableBuildWood >= GetBuildWoodCost(PieceType);
}

bool AAuraPlayerController::IsWithinBuildRange(const FVector& TargetLocation) const
{
	const APawn* ControlledPawn = GetPawn();
	return ControlledPawn
		&& FVector::DistSquared(ControlledPawn->GetActorLocation(), TargetLocation) <= FMath::Square(MaxBuildDistance);
}

bool AAuraPlayerController::IsPlayerBuildPiece(const AAuraBuildPiece* BuildPiece) const
{
	return IsValid(BuildPiece) && BuildPiece->IsRuntimePlaced();
}

bool AAuraPlayerController::CanRemoveBuildPiece(const AAuraBuildPiece* BuildPiece) const
{
	return IsPlayerBuildPiece(BuildPiece) && IsWithinBuildRange(BuildPiece->GetActorLocation());
}

bool AAuraPlayerController::WouldRemovingBuildPieceDestabilize(const AAuraBuildPiece* BuildPiece) const
{
	if (!IsValid(BuildPiece))
	{
		return false;
	}

	TArray<AAuraBuildPiece*> CollapsePreviewPieces;
	CollectCollapsePreviewPieces(BuildPiece, CollapsePreviewPieces);
	return CollapsePreviewPieces.Num() > 0;
}

bool AAuraPlayerController::CanPlaceBuildPieceAt(const FVector& Location, const FRotator& Rotation, const EAuraBuildPieceType PieceType) const
{
	if (!HasBuildResources(PieceType))
	{
		return false;
	}

	if (GetBuildSupportStrength(Location, Rotation, PieceType) <= 0)
	{
		return false;
	}

	const FVector PieceHalfExtent = AAuraBuildPiece::GetPieceHalfExtent(PieceType) * 0.95f;
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraBuildPlacement), false);
	QueryParams.AddIgnoredActor(GetPawn());
	if (IsValid(BuildPreviewActor))
	{
		QueryParams.AddIgnoredActor(BuildPreviewActor);
	}

	TArray<FOverlapResult> Overlaps;
	const bool bHasOverlap = GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		Location,
		Rotation.Quaternion(),
		ObjectQueryParams,
		FCollisionShape::MakeBox(PieceHalfExtent),
		QueryParams);

	if (!bHasOverlap)
	{
		return true;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (!Overlap.GetActor())
		{
			continue;
		}

		if (Overlap.GetActor() == GetPawn() || Overlap.GetActor() == BuildPreviewActor)
		{
			continue;
		}

		if (Overlap.GetActor()->IsA<AAuraBuildPiece>() || Overlap.GetActor()->IsA<APawn>())
		{
			return false;
		}
	}

	return true;
}

void AAuraPlayerController::SaveBuildStateToCurrentSlot() const
{
	if (!HasAuthority())
	{
		return;
	}

	const UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GetGameInstance());
	AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(GetWorld()->GetAuthGameMode());
	if (!IsValid(AuraGameMode) || !IsValid(AuraGameInstance) || AuraGameInstance->LoadSlotName.IsEmpty())
	{
		return;
	}

	ULoadScreenSaveGame* SaveData = AuraGameMode->RetrieveInGameSaveData();
	if (!IsValid(SaveData))
	{
		return;
	}

	SaveBuildProgress(SaveData);
	SaveData->bFirstTimeLoadIn = false;
	AuraGameMode->SaveInGameProgressData(SaveData);
	AuraGameMode->SaveWorldState(GetWorld());
}

int32 AAuraPlayerController::GetBuildWoodRefundForPiece(const AAuraBuildPiece* BuildPiece) const
{
	if (!IsValid(BuildPiece) || !BuildPiece->IsRuntimePlaced())
	{
		return 0;
	}

	return GetBuildWoodCost(BuildPiece->GetBuildPieceType());
}

int32 AAuraPlayerController::GetBuildWoodRefundForPieces(const TArray<AAuraBuildPiece*>& BuildPieces) const
{
	int32 TotalRefund = 0;
	for (const AAuraBuildPiece* BuildPiece : BuildPieces)
	{
		TotalRefund += GetBuildWoodRefundForPiece(BuildPiece);
	}

	return TotalRefund;
}

int32 AAuraPlayerController::GetBuildSupportStrength(const FVector& Location, const FRotator& Rotation, const EAuraBuildPieceType PieceType, const AAuraBuildPiece* IgnoredPiece) const
{
	if (MaxBuildSupportStrength <= 0)
	{
		return 0;
	}

	if (IsBuildPieceGrounded(Location, Rotation, PieceType, IgnoredPiece))
	{
		return MaxBuildSupportStrength;
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraBuildSupport), false);
	QueryParams.AddIgnoredActor(GetPawn());
	if (IsValid(BuildPreviewActor))
	{
		QueryParams.AddIgnoredActor(BuildPreviewActor);
	}
	if (IsValid(IgnoredPiece))
	{
		QueryParams.AddIgnoredActor(IgnoredPiece);
	}

	TArray<TPair<const AAuraBuildPiece*, int32>> PendingPieces;
	TSet<const AAuraBuildPiece*> VisitedPieces;

	auto EnqueueConnectedPieces = [&](const FVector& Origin, const AAuraBuildPiece* SourcePiece, const int32 Strength)
	{
		if (Strength <= 0)
		{
			return;
		}

		TArray<FOverlapResult> Overlaps;
		const bool bFoundOverlaps = GetWorld()->OverlapMultiByObjectType(
			Overlaps,
			Origin,
			FQuat::Identity,
			ObjectQueryParams,
			FCollisionShape::MakeSphere(BuildConnectionSearchRadius),
			QueryParams);

		if (!bFoundOverlaps)
		{
			return;
		}

		for (const FOverlapResult& Overlap : Overlaps)
		{
			const AAuraBuildPiece* NearbyPiece = Cast<AAuraBuildPiece>(Overlap.GetActor());
			if (!IsValid(NearbyPiece) || NearbyPiece == IgnoredPiece || NearbyPiece == SourcePiece || VisitedPieces.Contains(NearbyPiece))
			{
				continue;
			}

			const int32 ConnectionCost = SourcePiece
				? GetSnapPointSetsConnectionCost(
					AAuraBuildPiece::GetSnapPoints(SourcePiece->GetBuildPieceType()),
					SourcePiece->GetWorldSnapPointTransforms(),
					AAuraBuildPiece::GetSnapPoints(NearbyPiece->GetBuildPieceType()),
					NearbyPiece->GetWorldSnapPointTransforms())
				: GetSnapPointSetsConnectionCost(
					AAuraBuildPiece::GetSnapPoints(PieceType),
					GetPlacementSnapPointTransforms(Location, Rotation, PieceType),
					AAuraBuildPiece::GetSnapPoints(NearbyPiece->GetBuildPieceType()),
					NearbyPiece->GetWorldSnapPointTransforms());
			if (ConnectionCost == INDEX_NONE)
			{
				continue;
			}

			const int32 NextStrength = Strength - ConnectionCost;
			if (NextStrength <= 0)
			{
				continue;
			}

			VisitedPieces.Add(NearbyPiece);
			PendingPieces.Emplace(NearbyPiece, NextStrength);
		}
	};

	EnqueueConnectedPieces(Location, nullptr, MaxBuildSupportStrength);

	for (int32 Index = 0; Index < PendingPieces.Num(); ++Index)
	{
		const TPair<const AAuraBuildPiece*, int32>& PendingPiece = PendingPieces[Index];
		if (!IsValid(PendingPiece.Key) || PendingPiece.Value <= 0)
		{
			continue;
		}

		if (IsBuildPieceGrounded(PendingPiece.Key->GetActorLocation(), PendingPiece.Key->GetActorRotation(), PendingPiece.Key->GetBuildPieceType(), PendingPiece.Key))
		{
			return PendingPiece.Value;
		}

		EnqueueConnectedPieces(PendingPiece.Key->GetActorLocation(), PendingPiece.Key, PendingPiece.Value);
	}

	return 0;
}

bool AAuraPlayerController::IsBuildPieceGrounded(const FVector& Location, const FRotator& /*Rotation*/, const EAuraBuildPieceType PieceType, const AAuraBuildPiece* IgnoredPiece) const
{
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraBuildGrounded), false);
	QueryParams.AddIgnoredActor(GetPawn());
	if (IsValid(BuildPreviewActor))
	{
		QueryParams.AddIgnoredActor(BuildPreviewActor);
	}
	if (IsValid(IgnoredPiece))
	{
		QueryParams.AddIgnoredActor(IgnoredPiece);
	}

	const FVector PieceHalfExtent = AAuraBuildPiece::GetPieceHalfExtent(PieceType);
	const FVector Start = Location + FVector(0.f, 0.f, FMath::Max(PieceHalfExtent.Z + 40.f, 60.f));
	const FVector End = Location - FVector(0.f, 0.f, BuildSupportCheckDistance);

	FHitResult DownHit;
	if (!GetWorld()->LineTraceSingleByChannel(DownHit, Start, End, ECC_Visibility, QueryParams))
	{
		return false;
	}

	return !DownHit.GetActor() || !DownHit.GetActor()->IsA<AAuraBuildPiece>();
}

bool AAuraPlayerController::IsPieceConnectedToPlacement(const FVector& Location, const FRotator& Rotation, const EAuraBuildPieceType PieceType, const AAuraBuildPiece* OtherPiece) const
{
	if (!IsValid(OtherPiece))
	{
		return false;
	}

	return AreSnapPointSetsConnected(
		AAuraBuildPiece::GetSnapPoints(PieceType),
		GetPlacementSnapPointTransforms(Location, Rotation, PieceType),
		AAuraBuildPiece::GetSnapPoints(OtherPiece->GetBuildPieceType()),
		OtherPiece->GetWorldSnapPointTransforms());
}

bool AAuraPlayerController::AreBuildPiecesConnected(const AAuraBuildPiece* FirstPiece, const AAuraBuildPiece* SecondPiece) const
{
	if (!IsValid(FirstPiece) || !IsValid(SecondPiece))
	{
		return false;
	}

	return AreSnapPointSetsConnected(
		AAuraBuildPiece::GetSnapPoints(FirstPiece->GetBuildPieceType()),
		FirstPiece->GetWorldSnapPointTransforms(),
		AAuraBuildPiece::GetSnapPoints(SecondPiece->GetBuildPieceType()),
		SecondPiece->GetWorldSnapPointTransforms());
}

void AAuraPlayerController::CollectCollapsePreviewPieces(const AAuraBuildPiece* RemovedPiece, TArray<AAuraBuildPiece*>& OutCollapsePieces) const
{
	OutCollapsePieces.Reset();
	if (!IsValid(RemovedPiece))
	{
		return;
	}

	TArray<AAuraBuildPiece*> PendingPieces;
	CollectNearbyBuildPieces(RemovedPiece->GetActorLocation(), PendingPieces, RemovedPiece);

	TSet<AAuraBuildPiece*> ProcessedPieces;

	for (int32 Index = 0; Index < PendingPieces.Num(); ++Index)
	{
		AAuraBuildPiece* BuildPiece = PendingPieces[Index];
		if (!IsValid(BuildPiece)
			|| ProcessedPieces.Contains(BuildPiece)
			|| !AreBuildPiecesConnected(RemovedPiece, BuildPiece))
		{
			continue;
		}

		ProcessedPieces.Add(BuildPiece);
		if (GetBuildSupportStrength(
			BuildPiece->GetActorLocation(),
			BuildPiece->GetActorRotation(),
			BuildPiece->GetBuildPieceType(),
			RemovedPiece) > 0)
		{
			continue;
		}

		OutCollapsePieces.AddUnique(BuildPiece);

		TArray<AAuraBuildPiece*> NearbyPieces;
		CollectNearbyBuildPieces(BuildPiece->GetActorLocation(), NearbyPieces, RemovedPiece);
		for (AAuraBuildPiece* NearbyPiece : NearbyPieces)
		{
			if (IsValid(NearbyPiece) && IsPieceConnectedToPlacement(
				BuildPiece->GetActorLocation(),
				BuildPiece->GetActorRotation(),
				BuildPiece->GetBuildPieceType(),
				NearbyPiece))
			{
				PendingPieces.AddUnique(NearbyPiece);
			}
		}
	}
}

void AAuraPlayerController::CollectNearbyBuildPieces(const FVector& Origin, TArray<AAuraBuildPiece*>& OutBuildPieces, const AActor* IgnoredActor) const
{
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AuraBuildConnectionSearch), false);
	QueryParams.AddIgnoredActor(GetPawn());
	if (IsValid(BuildPreviewActor))
	{
		QueryParams.AddIgnoredActor(BuildPreviewActor);
	}
	if (IsValid(IgnoredActor))
	{
		QueryParams.AddIgnoredActor(IgnoredActor);
	}

	TArray<FOverlapResult> Overlaps;
	const bool bFoundOverlaps = GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(BuildConnectionSearchRadius),
		QueryParams);

	if (!bFoundOverlaps)
	{
		return;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AAuraBuildPiece* NearbyPiece = Cast<AAuraBuildPiece>(Overlap.GetActor()))
		{
			OutBuildPieces.AddUnique(NearbyPiece);
		}
	}
}

EAuraBuildPieceType AAuraPlayerController::GetSelectedBuildPieceType() const
{
	return StarterBuildPieces.IsValidIndex(SelectedBuildPieceIndex) ? StarterBuildPieces[SelectedBuildPieceIndex] : EAuraBuildPieceType::Floor;
}

void AAuraPlayerController::HighlightActor(AActor* InActor)
{
	if (IsValid(InActor) && InActor->Implements<UHighlightInterface>())
	{
		IHighlightInterface::Execute_HighlightActor(InActor);
	}
}

void AAuraPlayerController::UnHighlightActor(AActor* InActor)
{
	if (IsValid(InActor) && InActor->Implements<UHighlightInterface>())
	{
		IHighlightInterface::Execute_UnHighlightActor(InActor);
	}
}

void AAuraPlayerController::CursorTrace()
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_CursorTrace))
	{
		UnHighlightActor(LastActor);
		UnHighlightActor(ThisActor);
		if (IsValid(ThisActor) && ThisActor->Implements<UHighlightInterface>())

		LastActor = nullptr;
		ThisActor = nullptr;
		return;
	}
	const ECollisionChannel TraceChannel = (IsValid(MagicCircle) || IsValid(BuildPreviewActor)) ? ECC_ExcludePlayers : ECC_Visibility;
	GetHitResultUnderCursor(TraceChannel, false, CursorHit);
	if (!CursorHit.bBlockingHit) return;

	LastActor = ThisActor;
	if (IsValid(CursorHit.GetActor()) && CursorHit.GetActor()->Implements<UHighlightInterface>())
	{
		ThisActor = CursorHit.GetActor();
	}
	else
	{
		ThisActor = nullptr;
	}

	if (LastActor != ThisActor)
	{
		UnHighlightActor(LastActor);
		HighlightActor(ThisActor);
	}
}

void AAuraPlayerController::AbilityInputTagPressed(FGameplayTag InputTag)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputPressed))
	{
		return;
	}

	if (bBuildModeActive)
	{
		if (InputTag.MatchesTagExact(FAuraGameplayTags::Get().InputTag_LMB))
		{
			TryPlaceBuildPiece();
		}
		return;
	}

	if (InputTag.MatchesTagExact(FAuraGameplayTags::Get().InputTag_LMB))
	{
		if (IsValid(ThisActor))
		{
			TargetingStatus = ThisActor->Implements<UEnemyInterface>() ? ETargetingStatus::TargetingEnemy : ETargetingStatus::TargetingNonEnemy;
		}
		else
		{
			TargetingStatus = ETargetingStatus::NotTargeting;
		}
		bAutoRunning = false;
	}
	if (GetASC()) GetASC()->AbilityInputTagPressed(InputTag);
}

void AAuraPlayerController::AbilityInputTagReleased(FGameplayTag InputTag)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputReleased))
	{
		return;
	}

	if (bBuildModeActive)
	{
		return;
	}

	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().InputTag_LMB))
	{
		if (GetASC()) GetASC()->AbilityInputTagReleased(InputTag);
		return;
	}

	if (GetASC()) GetASC()->AbilityInputTagReleased(InputTag);
	
	if (TargetingStatus != ETargetingStatus::TargetingEnemy && !bShiftKeyDown)
	{
		const APawn* ControlledPawn = GetPawn();
		if (FollowTime <= ShortPressThreshold && ControlledPawn)
		{
			if (IsValid(ThisActor) && ThisActor->Implements<UHighlightInterface>())
			{
				IHighlightInterface::Execute_SetMoveToLocation(ThisActor, CachedDestination);
			}
			else if (GetASC() && !GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputPressed))
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ClickNiagaraSystem, CachedDestination);
			}
			if (UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(this, ControlledPawn->GetActorLocation(), CachedDestination))
			{
				Spline->ClearSplinePoints();
				for (const FVector& PointLoc : NavPath->PathPoints)
				{
					Spline->AddSplinePoint(PointLoc, ESplineCoordinateSpace::World);
				}
				if (NavPath->PathPoints.Num() > 0)
				{
					CachedDestination = NavPath->PathPoints[NavPath->PathPoints.Num() - 1];
					bAutoRunning = true;
				}
			}
		}
		FollowTime = 0.f;
		TargetingStatus = ETargetingStatus::NotTargeting;
	}
}

void AAuraPlayerController::AbilityInputTagHeld(FGameplayTag InputTag)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputHeld))
	{
		return;
	}

	if (bBuildModeActive)
	{
		return;
	}

	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().InputTag_LMB))
	{
		if (GetASC()) GetASC()->AbilityInputTagHeld(InputTag);
		return;
	}

	if (TargetingStatus == ETargetingStatus::TargetingEnemy || bShiftKeyDown)
	{
		if (GetASC()) GetASC()->AbilityInputTagHeld(InputTag);
	}
	else
	{
		FollowTime += GetWorld()->GetDeltaSeconds();
		if (CursorHit.bBlockingHit) CachedDestination = CursorHit.ImpactPoint;

		if (APawn* ControlledPawn = GetPawn())
		{
			const FVector WorldDirection = (CachedDestination - ControlledPawn->GetActorLocation()).GetSafeNormal();
			ControlledPawn->AddMovementInput(WorldDirection);
		}
	}
}

UAuraAbilitySystemComponent* AAuraPlayerController::GetASC()
{
	if (AuraAbilitySystemComponent == nullptr)
	{
		AuraAbilitySystemComponent = Cast<UAuraAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn<APawn>()));
	}
	return AuraAbilitySystemComponent;
}

void AAuraPlayerController::BeginPlay()
{
	Super::BeginPlay();
	check(AuraContext);

	if (HasAuthority())
	{
		AvailableBuildWood = StartingBuildWood;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (Subsystem)
	{
		Subsystem->AddMappingContext(AuraContext, 0);
		BuildModeContext->MapKey(ToggleBuildModeAction, EKeys::B);
		BuildModeContext->MapKey(RotateBuildPieceAction, EKeys::R);
		BuildModeContext->MapKey(PreviousBuildPieceAction, EKeys::Q);
		BuildModeContext->MapKey(PreviousBuildPieceAction, EKeys::MouseScrollDown);
		BuildModeContext->MapKey(NextBuildPieceAction, EKeys::E);
		BuildModeContext->MapKey(NextBuildPieceAction, EKeys::MouseScrollUp);
		BuildModeContext->MapKey(CancelBuildModeAction, EKeys::Escape);
		BuildModeContext->MapKey(RemoveBuildPieceAction, EKeys::RightMouseButton);
		BuildModeContext->MapKey(SelectFirstBuildPieceAction, EKeys::One);
		BuildModeContext->MapKey(SelectSecondBuildPieceAction, EKeys::Two);
		Subsystem->AddMappingContext(BuildModeContext, 1);
	}

	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;

	FInputModeGameAndUI InputModeData;
	InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputModeData.SetHideCursorDuringCapture(false);
	SetInputMode(InputModeData);
}

void AAuraPlayerController::LoadBuildProgress(const ULoadScreenSaveGame* SaveData)
{
	if (!HasAuthority())
	{
		return;
	}

	AvailableBuildWood = IsValid(SaveData) ? FMath::Max(0, SaveData->BuildWood) : StartingBuildWood;
	OnRep_AvailableBuildWood();
}

void AAuraPlayerController::SaveBuildProgress(ULoadScreenSaveGame* SaveData) const
{
	if (!IsValid(SaveData))
	{
		return;
	}

	SaveData->BuildWood = FMath::Max(0, AvailableBuildWood);
}

void AAuraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UAuraInputComponent* AuraInputComponent = CastChecked<UAuraInputComponent>(InputComponent);
	AuraInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAuraPlayerController::Move);
	AuraInputComponent->BindAction(ShiftAction, ETriggerEvent::Started, this, &AAuraPlayerController::ShiftPressed);
	AuraInputComponent->BindAction(ShiftAction, ETriggerEvent::Completed, this, &AAuraPlayerController::ShiftReleased);
	AuraInputComponent->BindAbilityActions(InputConfig, this, &ThisClass::AbilityInputTagPressed, &ThisClass::AbilityInputTagReleased, &ThisClass::AbilityInputTagHeld);
	AuraInputComponent->BindAction(ToggleBuildModeAction, ETriggerEvent::Started, this, &ThisClass::ToggleBuildMode);
	AuraInputComponent->BindAction(RotateBuildPieceAction, ETriggerEvent::Started, this, &ThisClass::RotateBuildPiece);
	AuraInputComponent->BindAction(PreviousBuildPieceAction, ETriggerEvent::Started, this, &ThisClass::CycleBuildPieceBackward);
	AuraInputComponent->BindAction(NextBuildPieceAction, ETriggerEvent::Started, this, &ThisClass::CycleBuildPieceForward);
	AuraInputComponent->BindAction(CancelBuildModeAction, ETriggerEvent::Started, this, &ThisClass::ExitBuildMode);
	AuraInputComponent->BindAction(RemoveBuildPieceAction, ETriggerEvent::Started, this, &ThisClass::TryRemoveBuildPiece);
	AuraInputComponent->BindAction(SelectFirstBuildPieceAction, ETriggerEvent::Started, this, &ThisClass::SelectFirstBuildPiece);
	AuraInputComponent->BindAction(SelectSecondBuildPieceAction, ETriggerEvent::Started, this, &ThisClass::SelectSecondBuildPiece);
}

void AAuraPlayerController::Move(const FInputActionValue& InputActionValue)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputPressed))
	{
		return;
	}
	const FVector2D InputAxisVector = InputActionValue.Get<FVector2D>();
	const FRotator Rotation = GetControlRotation();
	const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	if (APawn* ControlledPawn = GetPawn<APawn>())
	{
		ControlledPawn->AddMovementInput(ForwardDirection, InputAxisVector.Y);
		ControlledPawn->AddMovementInput(RightDirection, InputAxisVector.X);
	}
}

void AAuraPlayerController::ServerPlaceBuildPiece_Implementation(EAuraBuildPieceType PieceType, FVector_NetQuantize Location, FRotator Rotation)
{
	if (!BuildPieceClass
		|| !IsBuildPieceTypeUnlocked(PieceType)
		|| !HasBuildResources(PieceType)
		|| !IsWithinBuildRange(Location)
		|| !CanPlaceBuildPieceAt(Location, Rotation, PieceType))
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AAuraBuildPiece* BuildPiece = GetWorld()->SpawnActor<AAuraBuildPiece>(BuildPieceClass, Location, Rotation, SpawnParameters);
	if (IsValid(BuildPiece))
	{
		BuildPiece->ConfigureBuildPiece(PieceType, false);
		BuildPiece->SetRuntimePlaced(true);
		AvailableBuildWood = FMath::Max(0, AvailableBuildWood - GetBuildWoodCost(PieceType));
		OnRep_AvailableBuildWood();
		SaveBuildStateToCurrentSlot();
	}
}

void AAuraPlayerController::ServerDestroyBuildPiece_Implementation(AAuraBuildPiece* BuildPiece)
{
	if (!CanRemoveBuildPiece(BuildPiece))
	{
		return;
	}

	TArray<AAuraBuildPiece*> CollapsePreviewPieces;
	CollectCollapsePreviewPieces(BuildPiece, CollapsePreviewPieces);
	AvailableBuildWood += GetBuildWoodRefundForPiece(BuildPiece) + GetBuildWoodRefundForPieces(CollapsePreviewPieces);
	OnRep_AvailableBuildWood();
	BuildPiece->Destroy();
	for (AAuraBuildPiece* CollapsedPiece : CollapsePreviewPieces)
	{
		if (IsValid(CollapsedPiece))
		{
			CollapsedPiece->Destroy();
		}
	}
	SaveBuildStateToCurrentSlot();
}

void AAuraPlayerController::OnRep_AvailableBuildWood()
{
	UpdateBuildModeWidget();
}
