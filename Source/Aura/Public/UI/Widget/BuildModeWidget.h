// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BuildModeWidget.generated.h"

enum class EAuraBuildPieceType : uint8;

UCLASS()
class AURA_API UBuildModeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateBuildState(
		const TArray<EAuraBuildPieceType>& AvailablePieces,
		int32 SelectedPieceIndex,
		bool bCanPlacePiece,
		const FString& SnapDescription,
		int32 SupportStrength,
		int32 MaxSupportStrength,
		int32 AvailableWood,
		bool bHasHoveredPiece,
		EAuraBuildPieceType HoveredPieceType,
		int32 HoveredSupportStrength,
		bool bHoveredPieceIsPlayerBuilt,
		bool bCanRemoveHoveredPiece,
		bool bRemovingHoveredPieceIsRisky,
		int32 CollapsePreviewCount,
		int32 CollapsePreviewWoodRefund);

protected:
	virtual void NativeConstruct() override;

private:
	FString GetPieceLabel(EAuraBuildPieceType PieceType) const;
	int32 GetPieceWoodCost(EAuraBuildPieceType PieceType) const;
	int32 GetAffordablePieceCount(EAuraBuildPieceType PieceType, int32 AvailableWood) const;
	FString GetSupportLabel(int32 SupportStrength, int32 MaxSupportStrength) const;
	void RefreshPieceCards(const TArray<EAuraBuildPieceType>& AvailablePieces, int32 SelectedPieceIndex, int32 AvailableWood);

	UPROPERTY()
	TObjectPtr<class UBorder> PanelBorder;

	UPROPERTY()
	TObjectPtr<class UTextBlock> HeaderText;

	UPROPERTY()
	TObjectPtr<class UHorizontalBox> PieceSelectorBox;

	UPROPERTY()
	TObjectPtr<class UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<class UTextBlock> SnapText;

	UPROPERTY()
	TObjectPtr<class UTextBlock> SupportText;

	UPROPERTY()
	TObjectPtr<class UTextBlock> ResourceText;

	UPROPERTY()
	TObjectPtr<class UTextBlock> HoveredSupportText;

	UPROPERTY()
	TObjectPtr<class UTextBlock> ControlsText;

	UPROPERTY()
	TArray<TObjectPtr<class UBorder>> PieceCards;

	UPROPERTY()
	TArray<TObjectPtr<class UTextBlock>> PieceCardLabels;
};
