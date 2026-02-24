// ShopInventoryComponent.h
// Replicated component that manages the shop's item stock.
// Attach this to your Shop actor. All stock changes go through the server.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShopItemData.h"
#include "ShopInventoryComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventoryChanged, FName, ItemID, int32, NewQuantity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemSold, FName, ItemID, int32, SalePrice);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UShopInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShopInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Queries (safe to call on any machine) ---

	/** Check if we have at least 1 of this item in stock */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Inventory")
	bool HasItemInStock(FName ItemID) const;

	/** Get the current quantity for an item */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Inventory")
	int32 GetItemQuantity(FName ItemID) const;

	/** Get the full item info from the DataTable. Returns false if not found. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Inventory")
	bool GetItemInfo(FName ItemID, FShopItemInfo& OutInfo) const;

	/** Get a copy of the full inventory (for UI, etc.) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Inventory")
	TArray<FShopInventoryEntry> GetAllItems() const { return InventoryEntries; }

	// --- Mutations (server-authoritative) ---

	/** Remove one item from stock when sold to a customer. Returns true if successful. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Inventory")
	bool ConsumeItem(FName ItemID, int32 Amount = 1);

	/** Add stock (from restock orders via the computer). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Inventory")
	void AddStock(FName ItemID, int32 Amount);

	/** Pick a random item that is currently in stock. Returns NAME_None if empty. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Inventory")
	FName GetRandomAvailableItem() const;

	// --- Events ---

	/** Fires on all machines when inventory changes (via RepNotify) */
	UPROPERTY(BlueprintAssignable, Category = "Shop|Inventory")
	FOnInventoryChanged OnInventoryChanged;

	/** Fires on server when an item is sold */
	UPROPERTY(BlueprintAssignable, Category = "Shop|Inventory")
	FOnItemSold OnItemSold;

protected:
	virtual void BeginPlay() override;

	/** DataTable containing all item definitions (FShopItemInfo rows) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Inventory")
	TObjectPtr<UDataTable> ItemDataTable;

	/** Starting inventory - set in the editor to define initial stock */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Inventory")
	TArray<FShopInventoryEntry> DefaultInventory;

private:
	/** The actual runtime inventory - replicated to all clients */
	UPROPERTY(ReplicatedUsing = OnRep_InventoryEntries)
	TArray<FShopInventoryEntry> InventoryEntries;

	UFUNCTION()
	void OnRep_InventoryEntries();

	/** Find the index of an item in the array, or INDEX_NONE */
	int32 FindEntryIndex(FName ItemID) const;
};