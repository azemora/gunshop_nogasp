// ShopCustomerSpawner.h
// ALTERED - Multiple waiting spots with claim/release system, browsing location

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopCustomerSpawner.generated.h"

class ANPCCustomer;
class UShopInventoryComponent;
class AShopCheckout;

UCLASS()
class AShopCustomerSpawner : public AActor
{
	GENERATED_BODY()

public:
	AShopCustomerSpawner();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Spawner")
	ANPCCustomer* SpawnCustomer();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Spawner")
	void StartSpawning();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Spawner")
	void StopSpawning();

	// --- Location Getters ---
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Spawner")
	FVector GetSpawnLocation() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Spawner")
	FVector GetExitLocation() const;

	/** Get a random browsing location for NPCs that have no waiting spot */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Spawner")
	FVector GetBrowsingLocation() const;

	/** Get an available checkout for an NPC */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Spawner")
	AShopCheckout* GetAvailableCheckout() const;

	// --- Waiting Spot System ---

	/** Try to claim a waiting spot. Returns true if successful, fills OutIndex and OutLocation. */
	bool ClaimWaitingSpot(int32& OutIndex, FVector& OutLocation);

	/** Release a previously claimed waiting spot. */
	void ReleaseWaitingSpot(int32 SpotIndex);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Spawner")
	TSubclassOf<ANPCCustomer> NPCClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Spawner")
	TObjectPtr<AActor> ShopActor;

	// --- Location Markers ---

	/** Where NPCs spawn (outside the shop) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Locations")
	TObjectPtr<AActor> SpawnPoint;

	/** Multiple waiting spots inside the shop (TargetPoints).
	 *  NPCs will claim one spot each and stand there until attended.
	 *  Place these around the shop at positions where NPCs should wait. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Locations")
	TArray<TObjectPtr<AActor>> WaitingSpots;

	/** Where NPCs walk to when leaving */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Locations")
	TObjectPtr<AActor> ExitPoint;

	/** Area where NPCs browse when all spots are taken.
	 *  If not set, uses the spawn point area. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Locations")
	TObjectPtr<AActor> BrowsingAreaCenter;

	/** Radius around the browsing center for random browsing positions */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Locations", meta = (ClampMin = "50.0"))
	float BrowsingAreaRadius = 300.f;

	/** Available checkout actors in the level */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Locations")
	TArray<TObjectPtr<AShopCheckout>> Checkouts;

	// --- Config ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Spawner", meta = (ClampMin = "1.0"))
	float SpawnInterval = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Spawner", meta = (ClampMin = "1"))
	int32 MaxCustomers = 5;

private:
	void OnSpawnTimer();

	UFUNCTION()
	void OnCustomerDestroyed(AActor* DestroyedActor);

	UShopInventoryComponent* GetShopInventory() const;

	UPROPERTY()
	TArray<TObjectPtr<ANPCCustomer>> ActiveCustomers;

	/** Tracks which spots are occupied (true = occupied) */
	TArray<bool> SpotOccupied;

	FTimerHandle SpawnTimerHandle;
};