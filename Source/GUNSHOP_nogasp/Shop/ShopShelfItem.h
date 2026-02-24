// ShopShelfItem.h
// NEW - Item on a shelf that the player can pick up with E

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShopShelfItem.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

UCLASS()
class AShopShelfItem : public AActor
{
	GENERATED_BODY()

public:
	AShopShelfItem();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Interface for ShopPlayerInteraction ---

	/** Can this item be picked up right now? */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shop|Shelf")
	bool CanBePickedUp() const { return bIsAvailable; }

	/** Try to pick up this item. Returns true if successful. (Server only) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Shop|Shelf")
	bool TryPickUp();

	/** The item ID this shelf slot provides */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Shelf")
	FName ItemID;

	/** Time in seconds before the item respawns on the shelf */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Shelf", meta = (ClampMin = "1.0"))
	float RespawnTime = 5.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop|Shelf")
	TObjectPtr<UStaticMeshComponent> ItemMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop|Shelf")
	TObjectPtr<UBoxComponent> InteractionVolume;

private:
	UPROPERTY(ReplicatedUsing = OnRep_IsAvailable)
	bool bIsAvailable = true;

	UFUNCTION()
	void OnRep_IsAvailable();

	void Respawn();
	void UpdateVisuals();

	FTimerHandle RespawnTimerHandle;
};