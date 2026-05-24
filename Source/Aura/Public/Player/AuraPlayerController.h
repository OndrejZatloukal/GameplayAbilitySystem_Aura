// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "AuraPlayerController.generated.h"


class IHighlightInterface;
class UNiagaraSystem;
class UDamageTextComponent;
class UInputMappingContext;
class UInputAction;
enum class EInputActionValueType : uint8;
struct FInputActionValue;
class UAuraInputConfig;
class UAuraAbilitySystemComponent;
class ULoadScreenSaveGame;
class USplineComponent;
class AMagicCircle;
class AAuraBuildPiece;
class UBuildModeWidget;

enum class ETargetingStatus : uint8
{
	TargetingEnemy,
	TargetingNonEnemy,
	NotTargeting
};

/**
 * 
 */
UCLASS()
class AURA_API AAuraPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AAuraPlayerController();
	virtual void PlayerTick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void LoadBuildProgress(const ULoadScreenSaveGame* SaveData);
	void SaveBuildProgress(ULoadScreenSaveGame* SaveData) const;

	UFUNCTION(Client, Reliable)
	void ShowDamageNumber(float DamageAmount, ACharacter* TargetCharacter, bool bBlockedHit, bool bCriticalHit);

	UFUNCTION(BlueprintCallable)
	void ShowMagicCircle(UMaterialInterface* DecalMaterial = nullptr);

	UFUNCTION(BlueprintCallable)
	void HideMagicCircle();

	UFUNCTION(BlueprintCallable)
	void ToggleBuildMode();

	
protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
private:
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> AuraContext;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ShiftAction;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> BuildModeContext;

	UPROPERTY()
	TObjectPtr<UInputAction> ToggleBuildModeAction;

	UPROPERTY()
	TObjectPtr<UInputAction> RotateBuildPieceAction;

	UPROPERTY()
	TObjectPtr<UInputAction> PreviousBuildPieceAction;

	UPROPERTY()
	TObjectPtr<UInputAction> NextBuildPieceAction;

	UPROPERTY()
	TObjectPtr<UInputAction> CancelBuildModeAction;

	UPROPERTY()
	TObjectPtr<UInputAction> RemoveBuildPieceAction;

	UPROPERTY()
	TObjectPtr<UInputAction> SelectFirstBuildPieceAction;

	UPROPERTY()
	TObjectPtr<UInputAction> SelectSecondBuildPieceAction;

	void ShiftPressed() { bShiftKeyDown = true; };
	void ShiftReleased() { bShiftKeyDown = false; };
	bool bShiftKeyDown = false;

	void Move(const FInputActionValue& InputActionValue);

	void CursorTrace();
	TObjectPtr<AActor> LastActor;
	TObjectPtr<AActor> ThisActor;
	FHitResult CursorHit;
	static void HighlightActor(AActor* InActor);
	static void UnHighlightActor(AActor* InActor);

	void AbilityInputTagPressed(FGameplayTag InputTag);
	void AbilityInputTagReleased(FGameplayTag InputTag);
	void AbilityInputTagHeld(FGameplayTag InputTag);

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UAuraInputConfig> InputConfig;

	UPROPERTY()
	TObjectPtr<UAuraAbilitySystemComponent> AuraAbilitySystemComponent;

	UAuraAbilitySystemComponent* GetASC();

	
	FVector CachedDestination = FVector::ZeroVector;
	float FollowTime = 0.f;
	float ShortPressThreshold = 0.5f;
	bool bAutoRunning = false;
	ETargetingStatus TargetingStatus = ETargetingStatus::NotTargeting;

	UPROPERTY(EditDefaultsOnly)
	float AutoRunAcceptanceRadius = 50.f;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UNiagaraSystem> ClickNiagaraSystem;

	void AutoRun();

	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<UDamageTextComponent> DamageTextComponentClass;

	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<AMagicCircle> MagicCircleClass;

	UPROPERTY()
	TObjectPtr<AMagicCircle> MagicCircle;

	void UpdateMagicCircleLocation();

	UPROPERTY(EditDefaultsOnly, Category="Build")
	TSubclassOf<AAuraBuildPiece> BuildPieceClass;

	UPROPERTY(EditDefaultsOnly, Category="Build")
	float MaxBuildDistance = 650.f;

	UPROPERTY(EditDefaultsOnly, Category="Build")
	float MaxBuildSnapDistance = 175.f;

	UPROPERTY(EditDefaultsOnly, Category="Build")
	float BuildSupportCheckDistance = 250.f;

	UPROPERTY(EditDefaultsOnly, Category="Build")
	float BuildConnectionSearchRadius = 240.f;

	UPROPERTY(EditDefaultsOnly, Category="Build")
	int32 MaxBuildSupportStrength = 6;

	UPROPERTY(EditDefaultsOnly, Category="Build")
	int32 StartingBuildWood = 80;

	UPROPERTY(ReplicatedUsing=OnRep_AvailableBuildWood)
	int32 AvailableBuildWood = 80;

	UPROPERTY()
	TObjectPtr<AAuraBuildPiece> BuildPreviewActor;

	UPROPERTY()
	TObjectPtr<AAuraBuildPiece> HoveredSupportBuildPiece;

	UPROPERTY()
	TArray<TObjectPtr<AAuraBuildPiece>> VisualizedSupportBuildPieces;

	UPROPERTY()
	TObjectPtr<UBuildModeWidget> BuildModeWidget;

	TArray<EAuraBuildPieceType> StarterBuildPieces;

	int32 SelectedBuildPieceIndex = 0;
	int32 CurrentBuildSupportStrength = 0;
	int32 CurrentHoveredCollapsePreviewCount = 0;
	int32 CurrentHoveredCollapsePreviewWoodRefund = 0;
	FString CurrentBuildSnapDescription;
	float BuildPieceYaw = 0.f;
	bool bBuildModeActive = false;
	bool bCanPlaceCurrentBuildPiece = false;

	void EnterBuildMode();
	void ExitBuildMode();
	void CycleBuildPieceForward();
	void CycleBuildPieceBackward();
	void SelectFirstBuildPiece();
	void SelectSecondBuildPiece();
	void SelectBuildPieceByIndex(int32 PieceIndex);
	void RotateBuildPiece();
	void UpdateBuildPreviewLocation();
	void ClearBuildSupportVisualization();
	void UpdateBuildSupportVisualization();
	void UpdateBuildModeWidget() const;
	void RefreshBuildPreview();
	void TryPlaceBuildPiece();
	void TryRemoveBuildPiece();
	bool TryResolveBuildPlacement(FVector& OutLocation, FRotator& OutRotation, FString* OutSnapDescription = nullptr) const;
	int32 GetBuildWoodCost(EAuraBuildPieceType PieceType) const;
	bool HasBuildResources(EAuraBuildPieceType PieceType) const;
	bool IsBuildPieceTypeUnlocked(EAuraBuildPieceType PieceType) const;
	bool IsWithinBuildRange(const FVector& TargetLocation) const;
	bool IsPlayerBuildPiece(const AAuraBuildPiece* BuildPiece) const;
	bool CanRemoveBuildPiece(const AAuraBuildPiece* BuildPiece) const;
	bool WouldRemovingBuildPieceDestabilize(const AAuraBuildPiece* BuildPiece) const;
	bool CanPlaceBuildPieceAt(const FVector& Location, const FRotator& Rotation, EAuraBuildPieceType PieceType) const;
	int32 GetBuildWoodRefundForPiece(const AAuraBuildPiece* BuildPiece) const;
	int32 GetBuildWoodRefundForPieces(const TArray<AAuraBuildPiece*>& BuildPieces) const;
	int32 GetBuildSupportStrength(const FVector& Location, const FRotator& Rotation, EAuraBuildPieceType PieceType, const AAuraBuildPiece* IgnoredPiece = nullptr) const;
	bool IsBuildPieceGrounded(const FVector& Location, const FRotator& Rotation, EAuraBuildPieceType PieceType, const AAuraBuildPiece* IgnoredPiece = nullptr) const;
	bool IsPieceConnectedToPlacement(const FVector& Location, const FRotator& Rotation, EAuraBuildPieceType PieceType, const AAuraBuildPiece* OtherPiece) const;
	bool AreBuildPiecesConnected(const AAuraBuildPiece* FirstPiece, const AAuraBuildPiece* SecondPiece) const;
	void CollectCollapsePreviewPieces(const AAuraBuildPiece* RemovedPiece, TArray<AAuraBuildPiece*>& OutCollapsePieces) const;
	void CollectNearbyBuildPieces(const FVector& Origin, TArray<AAuraBuildPiece*>& OutBuildPieces, const AActor* IgnoredActor = nullptr) const;
	void SaveBuildStateToCurrentSlot() const;
	EAuraBuildPieceType GetSelectedBuildPieceType() const;

	UFUNCTION()
	void OnRep_AvailableBuildWood();

	UFUNCTION(Server, Reliable)
	void ServerPlaceBuildPiece(EAuraBuildPieceType PieceType, FVector_NetQuantize Location, FRotator Rotation);

	UFUNCTION(Server, Reliable)
	void ServerDestroyBuildPiece(AAuraBuildPiece* BuildPiece);
};
