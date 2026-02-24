// ShopShelfItem.cpp
// NEW

#include "GUNSHOP_nogasp/Shop/ShopShelfItem.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"

AShopShelfItem::AShopShelfItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// Visible mesh (assign in BP or set a default cube)
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	RootComponent = ItemMesh;
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ItemMesh->SetCollisionResponseToAllChannels(ECR_Block);
	ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Interaction volume - slightly larger than the mesh for easy detection
	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(RootComponent);
	InteractionVolume->SetBoxExtent(FVector(40.f, 40.f, 40.f));
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionVolume->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	InteractionVolume->SetGenerateOverlapEvents(true);
}

void AShopShelfItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShopShelfItem, bIsAvailable);
}

void AShopShelfItem::BeginPlay()
{
	Super::BeginPlay();
	UpdateVisuals();
}

bool AShopShelfItem::TryPickUp()
{
	check(HasAuthority());

	if (!bIsAvailable) return false;

	bIsAvailable = false;
	UpdateVisuals();

	UE_LOG(LogTemp, Log, TEXT("ShelfItem %s (%s): Picked up, respawning in %.1fs"),
		*GetName(), *ItemID.ToString(), RespawnTime);

	// Set timer to respawn
	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this,
		&AShopShelfItem::Respawn, RespawnTime, false);

	return true;
}

void AShopShelfItem::Respawn()
{
	check(HasAuthority());

	bIsAvailable = true;
	UpdateVisuals();

	UE_LOG(LogTemp, Log, TEXT("ShelfItem %s (%s): Respawned"),
		*GetName(), *ItemID.ToString());
}

void AShopShelfItem::OnRep_IsAvailable()
{
	UpdateVisuals();
}

void AShopShelfItem::UpdateVisuals()
{
	if (ItemMesh)
	{
		ItemMesh->SetVisibility(bIsAvailable);
		// Keep collision off when picked up so overlap scan doesn't find it
		ItemMesh->SetCollisionEnabled(bIsAvailable
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	}
}