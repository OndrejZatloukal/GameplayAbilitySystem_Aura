// Copyright Druid Mechanics


#include "UI/Widget/BuildModeWidget.h"

#include "Actor/AuraBuildPiece.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"

FString UBuildModeWidget::GetPieceLabel(const EAuraBuildPieceType PieceType) const
{
	return PieceType == EAuraBuildPieceType::Wall ? TEXT("Wall") : TEXT("Floor");
}

int32 UBuildModeWidget::GetPieceWoodCost(const EAuraBuildPieceType PieceType) const
{
	return AAuraBuildPiece::GetPieceWoodCost(PieceType);
}

int32 UBuildModeWidget::GetAffordablePieceCount(const EAuraBuildPieceType PieceType, const int32 AvailableWood) const
{
	const int32 PieceWoodCost = GetPieceWoodCost(PieceType);
	return PieceWoodCost > 0 ? AvailableWood / PieceWoodCost : 0;
}

FString UBuildModeWidget::GetSupportLabel(const int32 SupportStrength, const int32 MaxSupportStrength) const
{
	if (SupportStrength >= MaxSupportStrength)
	{
		return TEXT("Grounded");
	}
	if (SupportStrength > 3)
	{
		return TEXT("Strong");
	}
	if (SupportStrength > 0)
	{
		return TEXT("Weak");
	}

	return TEXT("Unsupported");
}

void UBuildModeWidget::RefreshPieceCards(const TArray<EAuraBuildPieceType>& AvailablePieces, const int32 SelectedPieceIndex, const int32 AvailableWood)
{
	if (!IsValid(PieceSelectorBox))
	{
		return;
	}

	PieceSelectorBox->ClearChildren();
	PieceCards.Reset();
	PieceCardLabels.Reset();

	for (int32 Index = 0; Index < AvailablePieces.Num(); ++Index)
	{
		const int32 PieceWoodCost = GetPieceWoodCost(AvailablePieces[Index]);
		const int32 AffordableCount = GetAffordablePieceCount(AvailablePieces[Index], AvailableWood);
		const bool bIsAffordable = AvailableWood >= PieceWoodCost;
		UBorder* PieceBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString::Printf(TEXT("PieceCard_%d"), Index));
		const bool bIsSelected = Index == SelectedPieceIndex;
		const FLinearColor CardColor = bIsSelected
			? (bIsAffordable ? FLinearColor(0.28f, 0.21f, 0.08f, 0.95f) : FLinearColor(0.33f, 0.13f, 0.09f, 0.95f))
			: (bIsAffordable ? FLinearColor(0.11f, 0.11f, 0.09f, 0.92f) : FLinearColor(0.17f, 0.08f, 0.08f, 0.92f));
		PieceBorder->SetBrushColor(CardColor);
		PieceBorder->SetPadding(FMargin(12.f, 10.f));

		UTextBlock* PieceLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("PieceCardLabel_%d"), Index));
		PieceLabel->SetText(FText::FromString(FString::Printf(TEXT("%d  %s  %dW  x%d"), Index + 1, *GetPieceLabel(AvailablePieces[Index]), PieceWoodCost, AffordableCount)));
		const FLinearColor LabelColor = !bIsAffordable
			? FLinearColor(0.96f, 0.58f, 0.58f)
			: (bIsSelected ? FLinearColor(0.97f, 0.91f, 0.73f) : FLinearColor(0.72f, 0.72f, 0.72f));
		PieceLabel->SetColorAndOpacity(FSlateColor(LabelColor));
		PieceBorder->SetContent(PieceLabel);

		if (UHorizontalBoxSlot* PieceSlot = PieceSelectorBox->AddChildToHorizontalBox(PieceBorder))
		{
			PieceSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
			PieceSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		PieceCards.Add(PieceBorder);
		PieceCardLabels.Add(PieceLabel);
	}
}

void UBuildModeWidget::UpdateBuildState(
	const TArray<EAuraBuildPieceType>& AvailablePieces,
	const int32 SelectedPieceIndex,
	const bool bCanPlacePiece,
	const FString& SnapDescription,
	const int32 SupportStrength,
	const int32 MaxSupportStrength,
	const int32 AvailableWood,
	const bool bHasHoveredPiece,
	const EAuraBuildPieceType HoveredPieceType,
	const int32 HoveredSupportStrength,
	const bool bHoveredPieceIsPlayerBuilt,
	const bool bCanRemoveHoveredPiece,
	const bool bRemovingHoveredPieceIsRisky,
	const int32 CollapsePreviewCount,
	const int32 CollapsePreviewWoodRefund)
{
	RefreshPieceCards(AvailablePieces, SelectedPieceIndex, AvailableWood);
	const int32 SelectedPieceWoodCost = AvailablePieces.IsValidIndex(SelectedPieceIndex)
		? GetPieceWoodCost(AvailablePieces[SelectedPieceIndex])
		: 0;
	const bool bHasSelectedPieceResources = AvailableWood >= SelectedPieceWoodCost;
	const FString PlacementLabel = bHasSelectedPieceResources
		? (bCanPlacePiece ? TEXT("Placement: Valid") : TEXT("Placement: Invalid"))
		: TEXT("Placement: Missing Wood");
	StatusText->SetText(FText::FromString(PlacementLabel));
	StatusText->SetColorAndOpacity(
		bCanPlacePiece && bHasSelectedPieceResources
			? FSlateColor(FLinearColor(0.55f, 0.9f, 0.7f))
			: FSlateColor(FLinearColor(1.f, 0.45f, 0.45f)));
	SnapText->SetText(FText::FromString(FString::Printf(TEXT("Snap: %s"), SnapDescription.IsEmpty() ? TEXT("None") : *SnapDescription)));

	const FString SupportLabel = GetSupportLabel(SupportStrength, MaxSupportStrength);
	FLinearColor SupportColor(1.f, 0.45f, 0.45f);
	if (SupportStrength >= MaxSupportStrength)
	{
		SupportColor = FLinearColor(0.7f, 0.95f, 0.75f);
	}
	else if (SupportStrength > 3)
	{
		SupportColor = FLinearColor(0.88f, 0.86f, 0.58f);
	}
	else if (SupportStrength > 0)
	{
		SupportColor = FLinearColor(1.f, 0.7f, 0.45f);
	}

	SupportText->SetText(FText::FromString(FString::Printf(TEXT("Support: %s (%d/%d)"), *SupportLabel, SupportStrength, MaxSupportStrength)));
	SupportText->SetColorAndOpacity(FSlateColor(SupportColor));
	const int32 MissingWood = FMath::Max(SelectedPieceWoodCost - AvailableWood, 0);
	const FString ResourceLine = MissingWood > 0
		? FString::Printf(TEXT("Wood: %d  Cost: %d  Missing: %d"), AvailableWood, SelectedPieceWoodCost, MissingWood)
		: FString::Printf(TEXT("Wood: %d  Cost: %d"), AvailableWood, SelectedPieceWoodCost);
	ResourceText->SetText(FText::FromString(ResourceLine));
	ResourceText->SetColorAndOpacity(FSlateColor(
		AvailableWood >= SelectedPieceWoodCost
			? FLinearColor(0.82f, 0.9f, 0.74f)
			: FLinearColor(1.f, 0.58f, 0.58f)));

	if (bHasHoveredPiece)
	{
		HoveredSupportText->SetVisibility(ESlateVisibility::Visible);
		const FString RemovalLabel = !bHoveredPieceIsPlayerBuilt
			? TEXT("Locked")
			: (!bCanRemoveHoveredPiece
				? TEXT("Out of Range")
				: (bRemovingHoveredPieceIsRisky ? TEXT("Risky") : TEXT("Ready")));
		const FString CollapseLabel = bRemovingHoveredPieceIsRisky
			? FString::Printf(TEXT("  Collapse: %d  Refund: %dW"), CollapsePreviewCount, CollapsePreviewWoodRefund)
			: FString();
		HoveredSupportText->SetText(FText::FromString(FString::Printf(
			TEXT("Target: %s  %s (%d/%d)  Remove: %s%s"),
			*GetPieceLabel(HoveredPieceType),
			*GetSupportLabel(HoveredSupportStrength, MaxSupportStrength),
			HoveredSupportStrength,
			MaxSupportStrength,
			*RemovalLabel,
			*CollapseLabel)));
		HoveredSupportText->SetColorAndOpacity(FSlateColor(
			!bHoveredPieceIsPlayerBuilt
				? FLinearColor(0.68f, 0.68f, 0.68f)
				: (!bCanRemoveHoveredPiece
					? FLinearColor(0.92f, 0.58f, 0.58f)
					: (bRemovingHoveredPieceIsRisky ? FLinearColor(1.f, 0.76f, 0.45f) : FLinearColor(0.95f, 0.86f, 0.72f)))));
	}
	else
	{
		HoveredSupportText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UBuildModeWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UBorder* RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RootBorder"));
	RootBorder->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.02f, 0.82f));

	UVerticalBox* ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	RootBorder->SetContent(ContentBox);

	HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeaderText"));
	HeaderText->SetText(FText::FromString(TEXT("Build Mode")));
	HeaderText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.89f, 0.65f)));

	PieceSelectorBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PieceSelectorBox"));

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));

	SnapText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SnapText"));
	SnapText->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.84f, 0.94f)));

	SupportText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SupportText"));

	ResourceText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResourceText"));
	ResourceText->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.9f, 0.74f)));

	HoveredSupportText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HoveredSupportText"));
	HoveredSupportText->SetColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.86f, 0.86f)));
	HoveredSupportText->SetVisibility(ESlateVisibility::Collapsed);

	ControlsText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ControlsText"));
	ControlsText->SetText(FText::FromString(TEXT("1/2 Select  Q/E or Wheel Switch  R Rotate  LMB Place  RMB Remove  Esc Exit")));
	ControlsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.82f, 0.82f)));
	ControlsText->SetAutoWrapText(true);

	ContentBox->AddChildToVerticalBox(HeaderText);
	ContentBox->AddChildToVerticalBox(PieceSelectorBox);
	ContentBox->AddChildToVerticalBox(StatusText);
	ContentBox->AddChildToVerticalBox(SnapText);
	ContentBox->AddChildToVerticalBox(SupportText);
	ContentBox->AddChildToVerticalBox(ResourceText);
	ContentBox->AddChildToVerticalBox(HoveredSupportText);
	ContentBox->AddChildToVerticalBox(ControlsText);

	WidgetTree->RootWidget = RootBorder;
	PanelBorder = RootBorder;

	if (UPanelSlot* RootSlot = RootBorder->Slot)
	{
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(RootSlot))
		{
			VerticalSlot->SetPadding(FMargin(12.f));
		}
	}

	UpdateBuildState({EAuraBuildPieceType::Floor, EAuraBuildPieceType::Wall}, 0, true, TEXT("Ground Grid"), 6, 6, 80, false, EAuraBuildPieceType::Floor, 0, false, false, false, 0, 0);
}
