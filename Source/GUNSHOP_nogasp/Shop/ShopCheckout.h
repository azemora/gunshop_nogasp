// ShopCheckout.h
// ALTERED - TargetPoint queue slots + QueueEntryPoint for smart routing
// NPCs go to entry point first, then walk to their slot from behind

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopCheckout.generated.h"

class ANPCCustomer;
class UShopInventoryComponent;
class UBoxComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCheckoutStateChanged, AShopCheckout*, Checkout, bool, bProcessing);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnSaleCompleted, AShopCheckout*, Checkout, FName, ItemID, int32, Price);

UCLASS()
class AShopCheckout : public AActor
{
	GENERATED_BODY()

public:
	AShopCheckout();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;

	// --- Slot System ---

	/** Claim the last free slot (closest to back of queue). Returns slot index or -1. */
	int32 ClaimNextFreeSlot(ANPCCustomer* Customer);

	/** Get world location of a slot */
	FVector GetSlotLocation(int32 SlotIndex) const;

	/** Get the entry point location (behind last slot) */
	FVector GetQueueEntryLocation() const;

	/** Does this NPC need to go to the entry point first? */
	bool ShouldUseEntryPoint(int32 SlotIndex) const;

	/** Get rotation to face the counter */
	FRotator GetFacingRotation() const;

	/** NPC arrived at their slot */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Checkout")
	void OnCustomerArrivedAtSlot(ANPCCustomer* Customer, int32 SlotIndex);

	/** Player places item on checkout */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Checkout")
	bool TryProcessItem(FName ItemID);

	void ReleaseSlot(int32 SlotIndex);

	// --- Getters ---

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Checkout")
	FVector GetItemPlacementLocation() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Checkout")
	bool IsProcessing() const { return bIsProcessing; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Checkout")
	ANPCCustomer* GetCurrentCustomer() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Checkout")
	FName GetExpectedItemID() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Checkout")
	int32 GetTotalLoad() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Checkout")
	int32 GetFreeSlotCount() const;

	int32 GetNumSlots() const { return QueueSlotPoints.Num(); }

	// --- Events ---

	UPROPERTY(BlueprintAssignable, Category = "Shop|Checkout")
	FOnCheckoutStateChanged OnCheckoutStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Shop|Checkout")
	FOnSaleCompleted OnSaleCompleted;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop|Checkout")
	TObjectPtr<UStaticMeshComponent> CheckoutMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop|Checkout")
	TObjectPtr<UBoxComponent> ItemPlacementZone;

	/** Queue slot positions - TargetPoints in the editor.
	 *  [0] = serving spot (closest to counter).
	 *  [1], [2], ... = queue behind, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Checkout")
	TArray<TObjectPtr<AActor>> QueueSlotPoints;

	/** Entry point BEHIND the last slot. NPCs go here first before
	 *  walking to their assigned slot. Place this behind the last
	 *  queue slot, offset to the side so NPCs approach from the back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Checkout")
	TObjectPtr<AActor> QueueEntryPoint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Checkout")
	TObjectPtr<AActor> ShopActor;

private:
	void TryServeSlotZero();
	void FinishCurrentCustomer(int32 Price);
	void ShiftQueueForward();

	/** Check if any NPC that arrived should swap with a slower NPC ahead */
	void CheckForOvertakes();

	UFUNCTION()
	void OnCustomerDestroyed(AActor* DestroyedActor);

	UShopInventoryComponent* GetShopInventory() const;

	UPROPERTY(Replicated)
	bool bIsProcessing = false;

	UPROPERTY()
	TArray<TObjectPtr<ANPCCustomer>> Slots;

	TArray<bool> SlotArrived;

	FTimerHandle ShiftTimerHandle;
	float OvertakeCheckTimer = 0.0f;
};