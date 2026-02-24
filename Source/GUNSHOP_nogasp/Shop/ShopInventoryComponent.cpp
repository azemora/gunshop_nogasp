// ShopInventoryComponent.cpp

#include "ShopInventoryComponent.h"
#include "Net/UnrealNetwork.h"

UShopInventoryComponent::UShopInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UShopInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UShopInventoryComponent, InventoryEntries);
}

void UShopInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// Server initializes inventory from defaults
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		InventoryEntries = DefaultInventory;
	}
}

// --- Queries ---

bool UShopInventoryComponent::HasItemInStock(FName ItemID) const
{
	return GetItemQuantity(ItemID) > 0;
}

int32 UShopInventoryComponent::GetItemQuantity(FName ItemID) const
{
	const int32 Idx = FindEntryIndex(ItemID);
	return (Idx != INDEX_NONE) ? InventoryEntries[Idx].Quantity : 0;
}

bool UShopInventoryComponent::GetItemInfo(FName ItemID, FShopItemInfo& OutInfo) const
{
	if (!ItemDataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopInventory: No ItemDataTable assigned!"));
		return false;
	}

	const FShopItemInfo* Row = ItemDataTable->FindRow<FShopItemInfo>(ItemID, TEXT("GetItemInfo"));
	if (Row)
	{
		OutInfo = *Row;
		return true;
	}
	return false;
}

FName UShopInventoryComponent::GetRandomAvailableItem() const
{
	TArray<FName> Available;
	for (const FShopInventoryEntry& Entry : InventoryEntries)
	{
		if (Entry.Quantity > 0)
		{
			Available.Add(Entry.ItemID);
		}
	}

	if (Available.Num() == 0)
	{
		return NAME_None;
	}

	const int32 RandomIndex = FMath::RandRange(0, Available.Num() - 1);
	return Available[RandomIndex];
}

// --- Mutations (server only) ---

bool UShopInventoryComponent::ConsumeItem(FName ItemID, int32 Amount)
{
	check(GetOwner() && GetOwner()->HasAuthority());

	const int32 Idx = FindEntryIndex(ItemID);
	if (Idx == INDEX_NONE || InventoryEntries[Idx].Quantity < Amount)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShopInventory: Cannot consume %d of '%s' - insufficient stock."),
			Amount, *ItemID.ToString());
		return false;
	}

	InventoryEntries[Idx].Quantity -= Amount;

	// Fetch price for the sold event
	FShopItemInfo Info;
	if (GetItemInfo(ItemID, Info))
	{
		OnItemSold.Broadcast(ItemID, Info.BasePrice * Amount);
	}

	OnInventoryChanged.Broadcast(ItemID, InventoryEntries[Idx].Quantity);

	return true;
}

void UShopInventoryComponent::AddStock(FName ItemID, int32 Amount)
{
	check(GetOwner() && GetOwner()->HasAuthority());

	const int32 Idx = FindEntryIndex(ItemID);
	if (Idx != INDEX_NONE)
	{
		InventoryEntries[Idx].Quantity += Amount;
		OnInventoryChanged.Broadcast(ItemID, InventoryEntries[Idx].Quantity);
	}
	else
	{
		// Item wasn't in inventory yet - add a new entry
		InventoryEntries.Add(FShopInventoryEntry(ItemID, Amount));
		OnInventoryChanged.Broadcast(ItemID, Amount);
	}
}

// --- Replication ---

void UShopInventoryComponent::OnRep_InventoryEntries()
{
	// Notify UI / visual systems that inventory changed on this client
	// We broadcast for each entry since we don't know which one changed
	// (TArray replication sends the whole array)
	for (const FShopInventoryEntry& Entry : InventoryEntries)
	{
		OnInventoryChanged.Broadcast(Entry.ItemID, Entry.Quantity);
	}
}

// --- Internal ---

int32 UShopInventoryComponent::FindEntryIndex(FName ItemID) const
{
	for (int32 i = 0; i < InventoryEntries.Num(); ++i)
	{
		if (InventoryEntries[i].ItemID == ItemID)
		{
			return i;
		}
	}
	return INDEX_NONE;
}