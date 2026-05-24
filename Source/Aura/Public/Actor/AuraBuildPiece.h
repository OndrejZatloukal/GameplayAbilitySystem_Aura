// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/SaveInterface.h"
#include "AuraBuildPiece.generated.h"

UENUM(BlueprintType)
enum class EAuraBuildPieceType : uint8
{
	Floor,
	Wall
};

UENUM()
enum class EAuraBuildSnapType : uint8
{
	FloorEdge,
	FloorTop,
	FloorBottom,
	WallSide,
	WallTop,
	WallBottom
};

USTRUCT()
struct FAuraBuildSnapPoint
{
	GENERATED_BODY()

	UPROPERTY()
	FVector LocalLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY()
	EAuraBuildSnapType SnapType = EAuraBuildSnapType::FloorEdge;
};

USTRUCT()
struct FAuraBuildPieceDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	int32 WoodCost = 0;

	UPROPERTY()
	TObjectPtr<class UStaticMesh> Mesh = nullptr;

	UPROPERTY()
	FVector MeshScale = FVector::OneVector;

	UPROPERTY()
	FVector MeshOffset = FVector::ZeroVector;

	UPROPERTY()
	FVector HalfExtent = FVector::ZeroVector;

	UPROPERTY()
	FVector PreviewLabelOffset = FVector::ZeroVector;

	UPROPERTY()
	TArray<FAuraBuildSnapPoint> SnapPoints;
};

UCLASS()
class AURA_API AAuraBuildPiece : public AActor, public ISaveInterface
{
	GENERATED_BODY()

public:
	AAuraBuildPiece();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void ConfigureBuildPiece(EAuraBuildPieceType InPieceType, bool bInPreviewMode);
	void SetBuildValid(bool bIsValidPlacement) const;
	void SetStructuralSupportVisual(int32 SupportStrength, int32 MaxSupportStrength) const;
	void SetInspectionStencil(int32 StencilValue) const;
	void ClearStructuralSupportVisual() const;
	void SetRuntimePlaced(bool bInRuntimePlaced) { bRuntimePlaced = bInRuntimePlaced; }

	EAuraBuildPieceType GetBuildPieceType() const { return PieceType; }
	bool IsRuntimePlaced() const { return bRuntimePlaced; }
	bool IsPreviewMode() const { return bPreviewMode; }

	virtual bool ShouldLoadTransform_Implementation() override { return true; }
	virtual void LoadActor_Implementation() override;

	static const FAuraBuildPieceDefinition& GetPieceDefinition(EAuraBuildPieceType InPieceType);
	static int32 GetPieceWoodCost(EAuraBuildPieceType InPieceType);
	static FVector GetPieceHalfExtent(EAuraBuildPieceType InPieceType);
	static TArray<FAuraBuildSnapPoint> GetSnapPoints(EAuraBuildPieceType InPieceType);
	static int32 GetSnapConnectionSupportCost(EAuraBuildSnapType FirstSnapType, EAuraBuildSnapType SecondSnapType);
	static bool AreSnapTypesConnected(EAuraBuildSnapType FirstSnapType, EAuraBuildSnapType SecondSnapType);
	TArray<FTransform> GetWorldSnapPointTransforms() const;

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMeshComponent> BuildMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<class UTextRenderComponent> PreviewLabel;

	UPROPERTY(ReplicatedUsing=OnRep_PieceType, SaveGame)
	EAuraBuildPieceType PieceType = EAuraBuildPieceType::Floor;

	UPROPERTY(SaveGame)
	bool bRuntimePlaced = false;

	UFUNCTION()
	void OnRep_PieceType();

private:
	void ApplyPieceDefinition() const;
	void ApplyPreviewState() const;
	FString GetPieceDisplayName() const;

	bool bPreviewMode = false;
};
