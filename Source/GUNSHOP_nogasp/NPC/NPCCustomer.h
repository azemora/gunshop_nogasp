// NPCCustomer.h
// ALTERED - Multiplayer-ready: replicated slot/order/snap for client sync

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GUNSHOP_nogasp/Shop/ShopItemData.h"
#include "NPCCustomer.generated.h"

class UShopInventoryComponent;
class UWidgetComponent;
class UNPCRequestWidget;
class AShopCustomerSpawner;
class AShopCheckout;

UENUM(BlueprintType)
enum class ENPCCustomerState : uint8
{
	Idle				UMETA(DisplayName = "Idle"),
	WalkingToShop		UMETA(DisplayName = "Walking To Shop"),
	Browsing			UMETA(DisplayName = "Browsing"),
	WaitingInShop		UMETA(DisplayName = "Waiting In Shop"),
	Attended			UMETA(DisplayName = "Attended"),
	WalkingToCheckout	UMETA(DisplayName = "Walking To Checkout"),
	WaitingAtCheckout	UMETA(DisplayName = "Waiting At Checkout"),
	Paying				UMETA(DisplayName = "Paying"),
	Leaving				UMETA(DisplayName = "Leaving"),
	Finished			UMETA(DisplayName = "Finished")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCustomerStateChanged, ANPCCustomer*, Customer, ENPCCustomerState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCustomerPaid, ANPCCustomer*, Customer, FName, ItemID, int32, AmountPaid);

UCLASS()
class ANPCCustomer : public ACharacter
{
	GENERATED_BODY()

public:
	ANPCCustomer();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;

	// --- Setup (Server) ---
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|NPC")
	void InitializeCustomer(FName InRequestedItemID, UShopInventoryComponent* InShopInventory);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|NPC")
	void SetSpawnerReference(AShopCustomerSpawner* InSpawner);

	// --- Flow (Server) ---

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|NPC")
	void BeginCustomerFlow();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|NPC")
	void AttendCustomer();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|NPC")
	void CompletePurchase(int32 PricePaid);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|NPC")
	void OnReachedDestination();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|NPC")
	void OnMoveToFailed();

	// --- Getters ---
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|NPC")
	ENPCCustomerState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|NPC")
	FName GetRequestedItemID() const { return RequestedItemID; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|NPC")
	FText GetRequestedItemDisplayName() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|NPC")
	bool CanBeAttended() const { return CurrentState == ENPCCustomerState::WaitingInShop; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|NPC")
	int32 GetAttendOrder() const { return AttendOrder; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|NPC")
	int32 GetAssignedSlotIndex() const { return Rep_SlotIndex; }

	void ReleaseWaitingSpot();
	void NavigateToLocation(FVector Destination);

	/** Called by checkout when this NPC is shifted to a new slot */
	void SetAssignedSlotIndex(int32 NewIndex);

	// --- Events ---
	UPROPERTY(BlueprintAssignable, Category = "Shop|NPC")
	FOnCustomerStateChanged OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Shop|NPC")
	FOnCustomerPaid OnCustomerPaid;

protected:
	virtual void BeginPlay() override;
	void SetState(ENPCCustomerState NewState);

	UFUNCTION()
	void OnRep_CurrentState();

	UFUNCTION()
	void OnRep_RequestedItemID();

	UFUNCTION()
	void OnRep_WidgetData();

	UFUNCTION(BlueprintNativeEvent, Category = "Shop|NPC")
	void HandleStateChanged(ENPCCustomerState NewState);

	void UpdateRequestWidget();
	void SetWidgetText(const FText& Text);
	void UpdateAnimationState();
	void TryClaimWaitingSpot();
	void SendToCheckout(AShopCheckout* Checkout);
	void CheckSlotProximity();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop|NPC")
	float AttendedDuration = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop|NPC")
	float PayingDuration = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop|NPC")
	float BrowsingRetryInterval = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop|NPC")
	float WalkSpeed = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop|NPC")
	float SlotArrivalRadius = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop|NPC")
	float SlotRetryInterval = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop|NPC")
	float SnapSpeed = 200.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop|NPC")
	TObjectPtr<UWidgetComponent> RequestTextWidget;

private:
	UPROPERTY(ReplicatedUsing = OnRep_CurrentState)
	ENPCCustomerState CurrentState = ENPCCustomerState::Idle;

	UPROPERTY(ReplicatedUsing = OnRep_RequestedItemID)
	FName RequestedItemID = NAME_None;

	UPROPERTY(Replicated)
	bool bHasBeenAttended = false;

	/** Replicated attend order + slot index for widget display on clients */
	UPROPERTY(ReplicatedUsing = OnRep_WidgetData)
	int32 AttendOrder = 0;

	UPROPERTY(ReplicatedUsing = OnRep_WidgetData)
	int32 Rep_SlotIndex = -1;

	/** Replicated snap state so clients can animate smoothly */
	UPROPERTY(Replicated)
	bool Rep_IsSnapping = false;

	UPROPERTY(Replicated)
	FVector Rep_SnapTarget = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<UShopInventoryComponent> ShopInventory;

	UPROPERTY()
	TObjectPtr<AShopCustomerSpawner> OwningSpawner;

	UPROPERTY()
	TObjectPtr<AShopCheckout> AssignedCheckout;

	int32 ClaimedWaitingSpotIndex = -1;
	bool bHasArrivedAtSlot = false;
	bool bGoingToEntryPoint = false;

	// Local snap state (server drives, client reads replicated version)
	bool bIsSnapping = false;
	FVector SnapTargetLocation = FVector::ZeroVector;

	static int32 GlobalAttendCounter;

	float TimeSinceLastSlotRetry = 0.0f;

	FTimerHandle StateTimerHandle;
	FTimerHandle BrowsingTimerHandle;
};