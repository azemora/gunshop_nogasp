// ShopItemData.h
// ALTERED - Removed redundant ItemID field. The DataTable Row Name IS the ItemID everywhere.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShopItemData.generated.h"

/**
 * Categories for shop items - expand as needed
 */
UENUM(BlueprintType)
enum class EShopItemCategory : uint8
{
	None       UMETA(DisplayName = "None"),
	Weapon     UMETA(DisplayName = "Weapon"),
	Ammo       UMETA(DisplayName = "Ammo"),
	Accessory  UMETA(DisplayName = "Accessory"),
	Misc       UMETA(DisplayName = "Misc")
};

/**
 * Core item definition - lives in a DataTable so designers can add items without code changes.
 * 
 * IMPORTANT: The DataTable Row Name IS the ItemID used everywhere in the system.
 * For example, row name "glock" means ItemID = "glock" throughout NPCs, inventory, shelves, etc.
 */
USTRUCT(BlueprintType)
struct FShopItemInfo : public FTableRowBase
{
	GENERATED_BODY()

	/** Display name shown to players and on NPC request text */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText DisplayName;

	/** Item category for filtering/sorting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EShopItemCategory Category = EShopItemCategory::None;

	/** Base price the customer pays */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0"))
	int32 BasePrice = 0;

	/** Cost to restock this item via the shop computer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "0"))
	int32 RestockCost = 0;

	/** Mesh to display on shelves / when carrying */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSoftObjectPtr<UStaticMesh> ItemMesh;

	/** Icon for UI (computer screen, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSoftObjectPtr<UTexture2D> ItemIcon;
};

/**
 * Represents an item stack in the inventory (runtime data, replicated).
 * ItemID here must match a Row Name in the DataTable.
 */
USTRUCT(BlueprintType)
struct FShopInventoryEntry
{
	GENERATED_BODY()

	/** Which item this entry refers to (must match a Row Name in the DataTable) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemID = NAME_None;

	/** Current quantity in stock */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = "0"))
	int32 Quantity = 0;

	FShopInventoryEntry() {}
	FShopInventoryEntry(FName InID, int32 InQty) : ItemID(InID), Quantity(InQty) {}

	bool operator==(const FShopInventoryEntry& Other) const
	{
		return ItemID == Other.ItemID;
	}
};