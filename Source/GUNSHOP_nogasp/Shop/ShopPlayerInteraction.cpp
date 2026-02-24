// ShopPlayerInteraction.cpp
// ALTERED

#include "GUNSHOP_nogasp/Shop/ShopPlayerInteraction.h"
#include "GUNSHOP_nogasp/NPC/NPCCustomer.h"
#include "GUNSHOP_nogasp/Shop/ShopShelfItem.h"
#include "GUNSHOP_nogasp/Shop/ShopCheckout.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"

UShopPlayerInteraction::UShopPlayerInteraction()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

void UShopPlayerInteraction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShopPlayerInteraction, CarriedItemID);
}

void UShopPlayerInteraction::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) return;

	UpdateInteractionPrompt();
}

// --- Actions ---

void UShopPlayerInteraction::InteractPressed()
{
	if (!IsCarryingItem())
	{
		// Not carrying -> try to pick up from shelf
		if (NearestShelfItem)
		{
			Server_PickUpShelfItem(NearestShelfItem);
		}
	}
	else
	{
		// Carrying -> try to place on checkout, or drop
		if (NearestCheckout)
		{
			Server_PlaceItemOnCheckout(NearestCheckout);
		}
		else
		{
			Server_DropItem();
		}
	}
}

void UShopPlayerInteraction::TryAttendNPC(ANPCCustomer* NPC)
{
	if (!NPC) return;
	Server_AttendNPC(NPC);
}

// --- Server RPCs ---

void UShopPlayerInteraction::Server_AttendNPC_Implementation(ANPCCustomer* NPC)
{
	if (!NPC)
	{
		Client_Feedback(false, FText::FromString(TEXT("No customer found!")));
		return;
	}

	if (!NPC->CanBeAttended())
	{
		Client_Feedback(false, FText::FromString(TEXT("This customer doesn't need help right now.")));
		return;
	}

	// Validate distance
	const float Dist = FVector::Dist(GetOwner()->GetActorLocation(), NPC->GetActorLocation());
	if (Dist > InteractionRadius * 2.f)
	{
		Client_Feedback(false, FText::FromString(TEXT("Too far away!")));
		return;
	}

	NPC->AttendCustomer();
	Client_Feedback(true, FText::Format(
		NSLOCTEXT("Shop", "AttendOK", "Customer wants: {0}"),
		NPC->GetRequestedItemDisplayName()));
}

void UShopPlayerInteraction::Server_PickUpShelfItem_Implementation(AShopShelfItem* ShelfItem)
{
	if (IsCarryingItem())
	{
		Client_Feedback(false, FText::FromString(TEXT("Already carrying an item!")));
		return;
	}

	if (!ShelfItem || !ShelfItem->CanBePickedUp())
	{
		Client_Feedback(false, FText::FromString(TEXT("Item not available!")));
		return;
	}

	const float Dist = FVector::Dist(GetOwner()->GetActorLocation(), ShelfItem->GetActorLocation());
	if (Dist > InteractionRadius * 1.5f)
	{
		Client_Feedback(false, FText::FromString(TEXT("Too far away!")));
		return;
	}

	if (ShelfItem->TryPickUp())
	{
		CarriedItemID = ShelfItem->ItemID;
		Client_Feedback(true, FText::Format(
			NSLOCTEXT("Shop", "PickedUp", "Picked up {0}"),
			FText::FromName(CarriedItemID)));
	}
}

void UShopPlayerInteraction::Server_PlaceItemOnCheckout_Implementation(AShopCheckout* Checkout)
{
	if (!IsCarryingItem())
	{
		Client_Feedback(false, FText::FromString(TEXT("Not carrying any item!")));
		return;
	}

	if (!Checkout)
	{
		Client_Feedback(false, FText::FromString(TEXT("No checkout nearby!")));
		return;
	}

	const float Dist = FVector::Dist(GetOwner()->GetActorLocation(), Checkout->GetActorLocation());
	if (Dist > InteractionRadius * 1.5f)
	{
		Client_Feedback(false, FText::FromString(TEXT("Too far away!")));
		return;
	}

	if (!Checkout->IsProcessing())
	{
		Client_Feedback(false, FText::FromString(TEXT("No customer at checkout!")));
		return;
	}

	const FName PlacedItem = CarriedItemID;
	const bool bSuccess = Checkout->TryProcessItem(PlacedItem);

	if (bSuccess)
	{
		CarriedItemID = NAME_None;
		Client_Feedback(true, FText::Format(
			NSLOCTEXT("Shop", "SaleOK", "Sale complete! Sold {0}"),
			FText::FromName(PlacedItem)));
	}
	else
	{
		Client_Feedback(false, FText::FromString(TEXT("Wrong item for this customer!")));
	}
}

void UShopPlayerInteraction::Server_DropItem_Implementation()
{
	if (!IsCarryingItem()) return;
	CarriedItemID = NAME_None;
	Client_Feedback(true, FText::FromString(TEXT("Item dropped.")));
}

// --- Client RPC ---

void UShopPlayerInteraction::Client_Feedback_Implementation(bool bSuccess, const FText& Message)
{
	OnDeliveryResult.Broadcast(bSuccess, Message);
}

// --- Replication ---

void UShopPlayerInteraction::OnRep_CarriedItemID()
{
	OnCarriedItemChanged.Broadcast(CarriedItemID);
}

// --- Scanning ---

void UShopPlayerInteraction::UpdateInteractionPrompt()
{
	NearestShelfItem = FindNearbyShelfItem();
	NearestCheckout = FindNearbyCheckout();

	FText Prompt = FText::GetEmpty();

	if (!IsCarryingItem())
	{
		if (NearestShelfItem)
		{
			Prompt = FText::Format(
				NSLOCTEXT("Shop", "PickupPrompt", "[E] Pick up {0}"),
				FText::FromName(NearestShelfItem->ItemID));
		}
	}
	else
	{
		if (NearestCheckout && NearestCheckout->IsProcessing())
		{
			Prompt = NSLOCTEXT("Shop", "PlacePrompt", "[E] Place item on checkout");
		}
		else
		{
			Prompt = NSLOCTEXT("Shop", "DropPrompt", "[E] Drop item");
		}
	}

	OnInteractionPrompt.Broadcast(Prompt);
}

AShopShelfItem* UShopPlayerInteraction::FindNearbyShelfItem() const
{
	if (!GetOwner()) return nullptr;
	const FVector Loc = GetOwner()->GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Shape = FCollisionShape::MakeSphere(InteractionRadius);
	GetWorld()->OverlapMultiByChannel(Overlaps, Loc, FQuat::Identity, ECC_WorldDynamic, Shape);

	AShopShelfItem* Closest = nullptr;
	float ClosestDist = InteractionRadius;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AShopShelfItem* Item = Cast<AShopShelfItem>(Overlap.GetActor());
		if (Item && Item->CanBePickedUp())
		{
			float Dist = FVector::Dist(Loc, Item->GetActorLocation());
			if (Dist < ClosestDist) { ClosestDist = Dist; Closest = Item; }
		}
	}
	return Closest;
}

AShopCheckout* UShopPlayerInteraction::FindNearbyCheckout() const
{
	if (!GetOwner()) return nullptr;
	const FVector Loc = GetOwner()->GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Shape = FCollisionShape::MakeSphere(InteractionRadius);
	GetWorld()->OverlapMultiByChannel(Overlaps, Loc, FQuat::Identity, ECC_WorldDynamic, Shape);

	AShopCheckout* Closest = nullptr;
	float ClosestDist = InteractionRadius;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AShopCheckout* CO = Cast<AShopCheckout>(Overlap.GetActor());
		if (CO)
		{
			float Dist = FVector::Dist(Loc, CO->GetActorLocation());
			if (Dist < ClosestDist) { ClosestDist = Dist; Closest = CO; }
		}
	}
	return Closest;
}