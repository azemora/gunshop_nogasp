// ShopCheckout.cpp
// ALTERED - Entry point routing, last-slot-first claiming

#include "GUNSHOP_nogasp/Shop/ShopCheckout.h"
#include "GUNSHOP_nogasp/NPC/NPCCustomer.h"
#include "GUNSHOP_nogasp/Shop/ShopInventoryComponent.h"
#include "GUNSHOP_nogasp/Shop/ShopItemData.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"

AShopCheckout::AShopCheckout()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	CheckoutMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CheckoutMesh"));
	RootComponent = CheckoutMesh;

	ItemPlacementZone = CreateDefaultSubobject<UBoxComponent>(TEXT("ItemPlacementZone"));
	ItemPlacementZone->SetupAttachment(RootComponent);
	ItemPlacementZone->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	ItemPlacementZone->SetBoxExtent(FVector(50.f, 50.f, 20.f));
	ItemPlacementZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ItemPlacementZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	ItemPlacementZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ItemPlacementZone->SetGenerateOverlapEvents(true);
}

void AShopCheckout::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShopCheckout, bIsProcessing);
}

void AShopCheckout::BeginPlay()
{
	Super::BeginPlay();

	const int32 NumSlots = QueueSlotPoints.Num();
	Slots.SetNum(NumSlots);
	SlotArrived.SetNum(NumSlots);
	for (int32 i = 0; i < NumSlots; ++i)
	{
		Slots[i] = nullptr;
		SlotArrived[i] = false;
	}

	UE_LOG(LogTemp, Log, TEXT("Checkout %s: Initialized with %d slots"), *GetName(), NumSlots);
	for (int32 i = 0; i < NumSlots; ++i)
	{
		if (QueueSlotPoints[i])
		{
			UE_LOG(LogTemp, Log, TEXT("  Slot %d: %s"), i, *QueueSlotPoints[i]->GetActorLocation().ToCompactString());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("  Slot %d: NULL!"), i);
		}
	}
	if (QueueEntryPoint)
	{
		UE_LOG(LogTemp, Log, TEXT("  EntryPoint: %s"), *QueueEntryPoint->GetActorLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("  EntryPoint: NOT SET"));
	}
}

// --- Tick: Overtake Check ---

void AShopCheckout::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (HasAuthority())
	{
		OvertakeCheckTimer += DeltaTime;
		if (OvertakeCheckTimer >= 1.0f)
		{
			OvertakeCheckTimer = 0.0f;
			CheckForOvertakes();
		}
	}
}

void AShopCheckout::CheckForOvertakes()
{
	// Look for cases where a later slot has an arrived NPC
	// but an earlier slot has a NPC that hasn't arrived yet.
	// The arrived NPC "overtakes" the slower one — they swap.
	// Skip slot 0 only if it's being actively served (bIsProcessing).

	int32 StartSlot = bIsProcessing ? 1 : 0;

	for (int32 i = StartSlot; i < Slots.Num() - 1; ++i)
	{
		if (!Slots[i] || SlotArrived[i]) continue;
		// Slot i has a NPC that hasn't arrived yet

		for (int32 j = i + 1; j < Slots.Num(); ++j)
		{
			if (!Slots[j] || !SlotArrived[j]) continue;
			// Slot j has arrived but slot i hasn't — swap!

			ANPCCustomer* SlowNPC = Slots[i];
			ANPCCustomer* FastNPC = Slots[j];

			UE_LOG(LogTemp, Log, TEXT("Checkout %s: OVERTAKE - %s (slot %d) passes %s (slot %d)"),
				*GetName(), *FastNPC->GetName(), j, *SlowNPC->GetName(), i);

			// Swap slots
			Slots[i] = FastNPC;
			Slots[j] = SlowNPC;
			SlotArrived[i] = true;
			SlotArrived[j] = false;

			// Update NPC slot indices (this resets bHasArrivedAtSlot/bIsSnapping)
			FastNPC->SetAssignedSlotIndex(i);
			SlowNPC->SetAssignedSlotIndex(j);

			// Fast NPC walks to closer slot
			FastNPC->NavigateToLocation(GetSlotLocation(i));
			// Slow NPC redirects to further slot
			SlowNPC->NavigateToLocation(GetSlotLocation(j));

			// If fast NPC took slot 0, try to serve
			if (i == 0 && !bIsProcessing)
			{
				// Give them a moment to snap into position
				FTimerHandle ServeTimer;
				GetWorldTimerManager().SetTimer(ServeTimer, [this]()
				{
					if (Slots.Num() > 0 && Slots[0] && SlotArrived[0] && !bIsProcessing)
					{
						TryServeSlotZero();
					}
				}, 1.0f, false);
			}

			// One swap per cycle
			return;
		}
	}
}

// --- Slot Positions ---

FVector AShopCheckout::GetSlotLocation(int32 SlotIndex) const
{
	if (SlotIndex >= 0 && SlotIndex < QueueSlotPoints.Num() && QueueSlotPoints[SlotIndex])
		return QueueSlotPoints[SlotIndex]->GetActorLocation();
	return GetActorLocation();
}

FVector AShopCheckout::GetQueueEntryLocation() const
{
	if (QueueEntryPoint)
		return QueueEntryPoint->GetActorLocation();
	// Fallback: behind last slot
	if (QueueSlotPoints.Num() > 0)
		return GetSlotLocation(QueueSlotPoints.Num() - 1);
	return GetActorLocation();
}

bool AShopCheckout::ShouldUseEntryPoint(int32 SlotIndex) const
{
	// Only use entry point if there's an entry point AND
	// the NPC isn't going to slot 0 on an empty queue
	return QueueEntryPoint != nullptr && SlotIndex > 0;
}

FRotator AShopCheckout::GetFacingRotation() const
{
	return GetActorForwardVector().Rotation();
}

// --- Slot Management ---

int32 AShopCheckout::ClaimNextFreeSlot(ANPCCustomer* Customer)
{
	check(HasAuthority());
	if (!Customer) return -1;

	// Find the last occupied slot, claim the one after it
	int32 LastOccupied = -1;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i])
		{
			// If this customer already has a slot, don't double-claim
			if (Slots[i] == Customer)
			{
				UE_LOG(LogTemp, Warning, TEXT("Checkout %s: %s already in slot %d!"),
					*GetName(), *Customer->GetName(), i);
				return -1;
			}
			LastOccupied = i;
		}
	}

	int32 TargetSlot = LastOccupied + 1;
	if (TargetSlot >= Slots.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Checkout %s: FULL - no slot for %s"), *GetName(), *Customer->GetName());
		return -1;
	}

	Slots[TargetSlot] = Customer;
	SlotArrived[TargetSlot] = false;

	// Only bind destroy delegate once
	if (!Customer->OnDestroyed.IsAlreadyBound(this, &AShopCheckout::OnCustomerDestroyed))
	{
		Customer->OnDestroyed.AddDynamic(this, &AShopCheckout::OnCustomerDestroyed);
	}

	UE_LOG(LogTemp, Log, TEXT("Checkout %s: %s claimed slot %d"),
		*GetName(), *Customer->GetName(), TargetSlot);
	return TargetSlot;
}

void AShopCheckout::ReleaseSlot(int32 SlotIndex)
{
	if (SlotIndex >= 0 && SlotIndex < Slots.Num())
	{
		Slots[SlotIndex] = nullptr;
		SlotArrived[SlotIndex] = false;
	}
}

void AShopCheckout::OnCustomerArrivedAtSlot(ANPCCustomer* Customer, int32 SlotIndex)
{
	check(HasAuthority());
	if (SlotIndex < 0 || SlotIndex >= Slots.Num()) return;

	if (Slots[SlotIndex] != Customer)
	{
		UE_LOG(LogTemp, Warning, TEXT("Checkout %s: %s says arrived at slot %d but slot has %s"),
			*GetName(), *Customer->GetName(), SlotIndex,
			Slots[SlotIndex] ? *Slots[SlotIndex]->GetName() : TEXT("nobody"));
		return;
	}

	SlotArrived[SlotIndex] = true;
	UE_LOG(LogTemp, Log, TEXT("Checkout %s: %s arrived at slot %d"), *GetName(), *Customer->GetName(), SlotIndex);

	if (SlotIndex == 0 && !bIsProcessing)
	{
		TryServeSlotZero();
	}
}

void AShopCheckout::TryServeSlotZero()
{
	if (Slots.Num() == 0 || !Slots[0] || !SlotArrived[0])
	{
		bIsProcessing = false;
		OnCheckoutStateChanged.Broadcast(this, false);
		return;
	}

	bIsProcessing = true;
	OnCheckoutStateChanged.Broadcast(this, true);

	UE_LOG(LogTemp, Log, TEXT("Checkout %s: Now serving %s (wants: %s)"),
		*GetName(), *Slots[0]->GetName(), *Slots[0]->GetRequestedItemID().ToString());
}

bool AShopCheckout::TryProcessItem(FName ItemID)
{
	check(HasAuthority());

	if (!bIsProcessing || Slots.Num() == 0 || !Slots[0] || !SlotArrived[0])
	{
		UE_LOG(LogTemp, Warning, TEXT("Checkout %s: Nobody at slot 0"), *GetName());
		return false;
	}

	FName Expected = Slots[0]->GetRequestedItemID();
	if (ItemID != Expected)
	{
		UE_LOG(LogTemp, Warning, TEXT("Checkout %s: Wrong item! Expected '%s', got '%s'"),
			*GetName(), *Expected.ToString(), *ItemID.ToString());
		return false;
	}

	UShopInventoryComponent* Inv = GetShopInventory();
	if (!Inv || !Inv->ConsumeItem(ItemID))
	{
		UE_LOG(LogTemp, Warning, TEXT("Checkout %s: Cannot consume '%s'"), *GetName(), *ItemID.ToString());
		return false;
	}

	int32 Price = 0;
	FShopItemInfo Info;
	if (Inv->GetItemInfo(ItemID, Info))
	{
		Price = Info.BasePrice;
	}

	FinishCurrentCustomer(Price);
	return true;
}

void AShopCheckout::FinishCurrentCustomer(int32 Price)
{
	check(HasAuthority());
	if (Slots.Num() == 0 || !Slots[0]) return;

	FName ItemID = Slots[0]->GetRequestedItemID();
	Slots[0]->CompletePurchase(Price);
	OnSaleCompleted.Broadcast(this, ItemID, Price);

	UE_LOG(LogTemp, Log, TEXT("Checkout %s: Sale complete - %s for %d"), *GetName(), *ItemID.ToString(), Price);

	Slots[0] = nullptr;
	SlotArrived[0] = false;
	bIsProcessing = false;

	// Wait for the NPC to start leaving before shifting
	GetWorldTimerManager().ClearTimer(ShiftTimerHandle);
	GetWorldTimerManager().SetTimer(ShiftTimerHandle, [this]()
	{
		ShiftQueueForward();
	}, 1.5f, false);
}

void AShopCheckout::ShiftQueueForward()
{
	check(HasAuthority());

	UE_LOG(LogTemp, Log, TEXT("Checkout %s: Shifting queue forward..."), *GetName());

	// Shift everyone one slot closer to counter
	// Important: iterate once and move each NPC at most once
	for (int32 i = 0; i < Slots.Num() - 1; ++i)
	{
		if (!Slots[i] && Slots[i + 1])
		{
			ANPCCustomer* Customer = Slots[i + 1];
			Slots[i] = Customer;
			SlotArrived[i] = false;
			Slots[i + 1] = nullptr;
			SlotArrived[i + 1] = false;

			Customer->SetAssignedSlotIndex(i);
			Customer->NavigateToLocation(GetSlotLocation(i));

			UE_LOG(LogTemp, Log, TEXT("  %s: slot %d -> slot %d"), *Customer->GetName(), i + 1, i);

			// Skip next slot since we just emptied it and the NPC
			// that was there is now at slot i (prevents double-shift)
		}
	}

	// Check if we can serve slot 0
	FTimerHandle ServeTimer;
	GetWorldTimerManager().SetTimer(ServeTimer, [this]()
	{
		if (Slots.Num() > 0 && Slots[0] && SlotArrived[0] && !bIsProcessing)
		{
			TryServeSlotZero();
		}
	}, 2.0f, false);
}

void AShopCheckout::OnCustomerDestroyed(AActor* DestroyedActor)
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i] == DestroyedActor)
		{
			Slots[i] = nullptr;
			SlotArrived[i] = false;
			if (i == 0)
			{
				bIsProcessing = false;
				GetWorldTimerManager().SetTimer(ShiftTimerHandle, [this]()
				{
					ShiftQueueForward();
				}, 0.5f, false);
			}
		}
	}
}

// --- Getters ---

FVector AShopCheckout::GetItemPlacementLocation() const
{
	return ItemPlacementZone ? ItemPlacementZone->GetComponentLocation() : GetActorLocation();
}

ANPCCustomer* AShopCheckout::GetCurrentCustomer() const
{
	return (Slots.Num() > 0) ? Slots[0].Get() : nullptr;
}

FName AShopCheckout::GetExpectedItemID() const
{
	ANPCCustomer* C = GetCurrentCustomer();
	return C ? C->GetRequestedItemID() : NAME_None;
}

int32 AShopCheckout::GetTotalLoad() const
{
	int32 N = 0;
	for (auto& S : Slots) { if (S) N++; }
	return N;
}

int32 AShopCheckout::GetFreeSlotCount() const
{
	int32 N = 0;
	for (auto& S : Slots) { if (!S) N++; }
	return N;
}

UShopInventoryComponent* AShopCheckout::GetShopInventory() const
{
	if (!ShopActor) return nullptr;
	return ShopActor->FindComponentByClass<UShopInventoryComponent>();
}