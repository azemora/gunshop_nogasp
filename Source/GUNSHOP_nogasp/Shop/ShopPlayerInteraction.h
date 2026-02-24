// ShopPlayerInteraction.h
// ALTERED - New interaction: click NPC to attend, interact with shelf to pick up, interact near checkout to place item

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GUNSHOP_nogasp/Shop/ShopItemData.h"
#include "ShopPlayerInteraction.generated.h"

class ANPCCustomer;
class AShopShelfItem;
class AShopCheckout;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCarriedItemChanged, FName, NewItemID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDeliveryResult, bool, bSuccess, FText, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractionPrompt, FText, PromptText);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UShopPlayerInteraction : public UActorComponent
{
	GENERATED_BODY()

public:
	UShopPlayerInteraction();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Actions (call from input bindings) ---

	/** Press E near shelf/checkout - context-sensitive interaction */
	UFUNCTION(BlueprintCallable, Category = "Shop|Player")
	void InteractPressed();

	/** Click on NPC to attend them (use with mouse click / line trace) */
	UFUNCTION(BlueprintCallable, Category = "Shop|Player")
	void TryAttendNPC(ANPCCustomer* NPC);

	// --- Getters ---

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Player")
	FName GetCarriedItem() const { return CarriedItemID; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Player")
	bool IsCarryingItem() const { return !CarriedItemID.IsNone(); }

	// --- Config ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|Player")
	float InteractionRadius = 200.f;

	// --- Events (bind these in Blueprint) ---

	/** Fires when the carried item changes. Use to show/hide item in player's hands. */
	UPROPERTY(BlueprintAssignable, Category = "Shop|Player")
	FOnCarriedItemChanged OnCarriedItemChanged;

	/** Fires with success/failure message after interactions. Use for HUD feedback text. */
	UPROPERTY(BlueprintAssignable, Category = "Shop|Player")
	FOnDeliveryResult OnDeliveryResult;

	/** Fires every 0.1s with context prompt text. Bind to a HUD text widget. */
	UPROPERTY(BlueprintAssignable, Category = "Shop|Player")
	FOnInteractionPrompt OnInteractionPrompt;

protected:
	// --- Server RPCs ---

	UFUNCTION(Server, Reliable)
	void Server_AttendNPC(ANPCCustomer* NPC);

	UFUNCTION(Server, Reliable)
	void Server_PickUpShelfItem(AShopShelfItem* ShelfItem);

	UFUNCTION(Server, Reliable)
	void Server_PlaceItemOnCheckout(AShopCheckout* Checkout);

	UFUNCTION(Server, Reliable)
	void Server_DropItem();

	UFUNCTION(Client, Reliable)
	void Client_Feedback(bool bSuccess, const FText& Message);

private:
	UPROPERTY(ReplicatedUsing = OnRep_CarriedItemID)
	FName CarriedItemID = NAME_None;

	UFUNCTION()
	void OnRep_CarriedItemID();

	void UpdateInteractionPrompt();

	AShopShelfItem* FindNearbyShelfItem() const;
	AShopCheckout* FindNearbyCheckout() const;

	// Cached scan results
	UPROPERTY()
	TObjectPtr<AShopShelfItem> NearestShelfItem;

	UPROPERTY()
	TObjectPtr<AShopCheckout> NearestCheckout;
};