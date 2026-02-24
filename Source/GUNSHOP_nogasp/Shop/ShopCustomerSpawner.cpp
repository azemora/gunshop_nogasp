// ShopCustomerSpawner.cpp
// ALTERED - Multiple waiting spots, browsing area, claim/release system

#include "GUNSHOP_nogasp/Shop/ShopCustomerSpawner.h"
#include "GUNSHOP_nogasp/NPC/NPCCustomer.h"
#include "GUNSHOP_nogasp/Shop/ShopInventoryComponent.h"
#include "GUNSHOP_nogasp/Shop/ShopCheckout.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"

AShopCustomerSpawner::AShopCustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
}

void AShopCustomerSpawner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void AShopCustomerSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (!SpawnPoint || !ExitPoint)
		{
			UE_LOG(LogTemp, Error, TEXT("CustomerSpawner: Missing SpawnPoint or ExitPoint!"));
			return;
		}

		if (WaitingSpots.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("CustomerSpawner: No WaitingSpots assigned! NPCs will browse forever."));
		}

		if (Checkouts.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("CustomerSpawner: No checkouts assigned!"));
		}

		// Initialize spot tracking
		SpotOccupied.SetNum(WaitingSpots.Num());
		for (int32 i = 0; i < SpotOccupied.Num(); ++i)
		{
			SpotOccupied[i] = false;
		}

		UE_LOG(LogTemp, Log, TEXT("CustomerSpawner: %d waiting spots available"), WaitingSpots.Num());

		StartSpawning();
	}
}

void AShopCustomerSpawner::StartSpawning()
{
	check(HasAuthority());
	GetWorldTimerManager().SetTimer(SpawnTimerHandle, this,
		&AShopCustomerSpawner::OnSpawnTimer, SpawnInterval, true);
	UE_LOG(LogTemp, Log, TEXT("CustomerSpawner: Started (interval=%.1fs, max=%d)"), SpawnInterval, MaxCustomers);
}

void AShopCustomerSpawner::StopSpawning()
{
	check(HasAuthority());
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);
}

void AShopCustomerSpawner::OnSpawnTimer()
{
	ActiveCustomers.RemoveAll([](const TObjectPtr<ANPCCustomer>& C) { return !C; });
	if (ActiveCustomers.Num() >= MaxCustomers) return;

	UShopInventoryComponent* Inventory = GetShopInventory();
	if (!Inventory) return;

	FName RandomItem = Inventory->GetRandomAvailableItem();
	if (RandomItem.IsNone()) return;

	SpawnCustomer();
}

ANPCCustomer* AShopCustomerSpawner::SpawnCustomer()
{
	check(HasAuthority());

	if (!NPCClass) { UE_LOG(LogTemp, Error, TEXT("CustomerSpawner: No NPC class!")); return nullptr; }

	UShopInventoryComponent* Inventory = GetShopInventory();
	if (!Inventory) return nullptr;

	FName RequestedItem = Inventory->GetRandomAvailableItem();
	if (RequestedItem.IsNone()) return nullptr;

	FVector SpawnLoc = GetSpawnLocation();
	FRotator SpawnRot = SpawnPoint ? SpawnPoint->GetActorRotation() : FRotator::ZeroRotator;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ANPCCustomer* NewCustomer = GetWorld()->SpawnActor<ANPCCustomer>(NPCClass, SpawnLoc, SpawnRot, SpawnParams);
	if (!NewCustomer) return nullptr;

	NewCustomer->InitializeCustomer(RequestedItem, Inventory);
	NewCustomer->SetSpawnerReference(this);

	ActiveCustomers.Add(NewCustomer);
	NewCustomer->OnDestroyed.AddDynamic(this, &AShopCustomerSpawner::OnCustomerDestroyed);

	NewCustomer->BeginCustomerFlow();

	UE_LOG(LogTemp, Log, TEXT("CustomerSpawner: Spawned NPC wanting '%s' (%d/%d)"),
		*RequestedItem.ToString(), ActiveCustomers.Num(), MaxCustomers);

	return NewCustomer;
}

void AShopCustomerSpawner::OnCustomerDestroyed(AActor* DestroyedActor)
{
	ActiveCustomers.RemoveAll([DestroyedActor](const TObjectPtr<ANPCCustomer>& C) { return C == DestroyedActor; });
}

// --- Waiting Spot System ---

bool AShopCustomerSpawner::ClaimWaitingSpot(int32& OutIndex, FVector& OutLocation)
{
	// Gather all free spots
	TArray<int32> FreeSpots;
	for (int32 i = 0; i < WaitingSpots.Num(); ++i)
	{
		if (WaitingSpots[i] && !SpotOccupied[i])
		{
			FreeSpots.Add(i);
		}
	}

	if (FreeSpots.Num() == 0)
	{
		return false;
	}

	// Pick a random free spot
	const int32 RandomPick = FreeSpots[FMath::RandRange(0, FreeSpots.Num() - 1)];
	SpotOccupied[RandomPick] = true;
	OutIndex = RandomPick;
	OutLocation = WaitingSpots[RandomPick]->GetActorLocation();

	UE_LOG(LogTemp, Log, TEXT("CustomerSpawner: Spot %d claimed (%d/%d occupied)"),
		RandomPick, SpotOccupied.FilterByPredicate([](bool b){ return b; }).Num(), WaitingSpots.Num());

	return true;
}

void AShopCustomerSpawner::ReleaseWaitingSpot(int32 SpotIndex)
{
	if (SpotIndex >= 0 && SpotIndex < SpotOccupied.Num())
	{
		SpotOccupied[SpotIndex] = false;

		UE_LOG(LogTemp, Log, TEXT("CustomerSpawner: Spot %d released"), SpotIndex);
	}
}

// --- Getters ---

FVector AShopCustomerSpawner::GetSpawnLocation() const
{
	return SpawnPoint ? SpawnPoint->GetActorLocation() : GetActorLocation();
}

FVector AShopCustomerSpawner::GetExitLocation() const
{
	return ExitPoint ? ExitPoint->GetActorLocation() : GetActorLocation();
}

FVector AShopCustomerSpawner::GetBrowsingLocation() const
{
	FVector Center = BrowsingAreaCenter
		? BrowsingAreaCenter->GetActorLocation()
		: GetSpawnLocation();

	// Try to find a random navigable point in the browsing area
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NavSys)
	{
		FNavLocation NavLoc;
		if (NavSys->GetRandomReachablePointInRadius(Center, BrowsingAreaRadius, NavLoc))
		{
			return NavLoc.Location;
		}
	}

	// Fallback: random offset from center
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Dist = FMath::FRandRange(50.f, BrowsingAreaRadius);
	return Center + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
}

AShopCheckout* AShopCustomerSpawner::GetAvailableCheckout() const
{
	TArray<AShopCheckout*> Candidates;
	int32 LeastLoad = INT_MAX;

	for (const TObjectPtr<AShopCheckout>& Checkout : Checkouts)
	{
		if (!Checkout) continue;
		if (Checkout->GetFreeSlotCount() <= 0) continue;

		const int32 Load = Checkout->GetTotalLoad();
		if (Load < LeastLoad)
		{
			LeastLoad = Load;
			Candidates.Empty();
			Candidates.Add(Checkout);
		}
		else if (Load == LeastLoad)
		{
			Candidates.Add(Checkout);
		}
	}

	if (Candidates.Num() == 0) return nullptr;
	return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
}

UShopInventoryComponent* AShopCustomerSpawner::GetShopInventory() const
{
	if (!ShopActor) return nullptr;
	return ShopActor->FindComponentByClass<UShopInventoryComponent>();
}