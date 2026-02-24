// NPCCustomer.cpp
// ALTERED - Attend order, proximity slot arrival, collision-resilient queue

#include "GUNSHOP_nogasp/NPC/NPCCustomer.h"
#include "GUNSHOP_nogasp/NPC/NPCCustomerAIController.h"
#include "GUNSHOP_nogasp/NPC/NPCRequestWidget.h"
#include "GUNSHOP_nogasp/Shop/ShopInventoryComponent.h"
#include "GUNSHOP_nogasp/Shop/ShopCustomerSpawner.h"
#include "GUNSHOP_nogasp/Shop/ShopCheckout.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

// Static counter for attend order (persists across PIE sessions, numbers may not start at 1)
int32 ANPCCustomer::GlobalAttendCounter = 0;

ANPCCustomer::ANPCCustomer()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	AIControllerClass = ANPCCustomerAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	RequestTextWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("RequestTextWidget"));
	RequestTextWidget->SetupAttachment(GetCapsuleComponent());
	RequestTextWidget->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	RequestTextWidget->SetWidgetSpace(EWidgetSpace::Screen);
	RequestTextWidget->SetDrawSize(FVector2D(300.f, 60.f));
	RequestTextWidget->SetVisibility(false);

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed = 200.f;
		CMC->bUseRVOAvoidance = true;
		CMC->AvoidanceConsiderationRadius = 150.f;
		CMC->AvoidanceWeight = 0.5f;
		CMC->SetAvoidanceGroup(1);
		CMC->SetGroupsToAvoid(1);
	}
}

void ANPCCustomer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANPCCustomer, CurrentState);
	DOREPLIFETIME(ANPCCustomer, RequestedItemID);
	DOREPLIFETIME(ANPCCustomer, Rep_DisplayName);
	DOREPLIFETIME(ANPCCustomer, bHasBeenAttended);
	DOREPLIFETIME(ANPCCustomer, AttendOrder);
	DOREPLIFETIME(ANPCCustomer, Rep_SlotIndex);
	DOREPLIFETIME(ANPCCustomer, Rep_IsSnapping);
	DOREPLIFETIME(ANPCCustomer, Rep_SnapTarget);
}

void ANPCCustomer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateAnimationState();

	if (HasAuthority())
	{
		CheckSlotProximity();

		// Server: smooth interpolation to slot position
		if (bIsSnapping)
		{
			const FVector Current = GetActorLocation();
			const float Dist = FVector::Dist2D(Current, SnapTargetLocation);
			if (Dist < 2.0f)
			{
				SetActorLocation(SnapTargetLocation);
				bIsSnapping = false; Rep_IsSnapping = false;
				if (AssignedCheckout)
				{
					FRotator FaceRot = AssignedCheckout->GetFacingRotation();
					SetActorRotation(FRotator(0.f, FaceRot.Yaw, 0.f));
				}
			}
			else
			{
				const FVector Dir = (SnapTargetLocation - Current).GetSafeNormal2D();
				const FVector NewLoc = Current + Dir * FMath::Min(SnapSpeed * DeltaTime, Dist);
				SetActorLocation(FVector(NewLoc.X, NewLoc.Y, Current.Z));

				const FRotator MoveRot = Dir.Rotation();
				const FRotator CurrentRot = GetActorRotation();
				const FRotator SmoothedRot = FMath::RInterpTo(CurrentRot, MoveRot, DeltaTime, 8.0f);
				SetActorRotation(FRotator(0.f, SmoothedRot.Yaw, 0.f));
			}
		}
	}
}

void ANPCCustomer::BeginPlay()
{
	Super::BeginPlay();
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	}

	// Late joiner fix: schedule widget update after a short delay
	// so replicated values have time to arrive on clients
	if (!HasAuthority())
	{
		FTimerHandle WidgetInitTimer;
		GetWorldTimerManager().SetTimer(WidgetInitTimer, [this]()
		{
			HandleStateChanged(CurrentState);
			UpdateRequestWidget();
		}, 0.5f, false);
	}
}

// --- Animation ---

void ANPCCustomer::UpdateAnimationState()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp) return;
	UAnimInstance* AnimInst = MeshComp->GetAnimInstance();
	if (!AnimInst) return;

	const FVector Vel = GetVelocity();
	float Speed = Vel.Size2D();
	bool bMoving = Speed > 10.0f;

	// During snap, force walk animation using replicated snap state
	const bool bCurrentlySnapping = HasAuthority() ? bIsSnapping : Rep_IsSnapping;
	const FVector CurrentSnapTarget = HasAuthority() ? SnapTargetLocation : Rep_SnapTarget;

	if (bCurrentlySnapping)
	{
		const float DistToTarget = FVector::Dist2D(GetActorLocation(), CurrentSnapTarget);
		if (DistToTarget > 2.0f)
		{
			Speed = SnapSpeed;
			bMoving = true;
		}
	}

	// Client fallback: if position is changing but velocity reports zero,
	// use position delta to drive animation (covers pathfinding replication)
	if (!bMoving && !HasAuthority())
	{
		const FVector CurrentPos = GetActorLocation();
		const float PosDelta = FVector::Dist2D(CurrentPos, LastClientPosition);
		if (PosDelta > 5.0f)
		{
			Speed = PosDelta / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.001f);
			bMoving = Speed > 10.0f;
		}
		LastClientPosition = CurrentPos;
	}

	const bool bFalling = GetCharacterMovement() ? GetCharacterMovement()->IsFalling() : false;

	// UE5 BP "Float" variables are actually double internally
	auto SetDouble = [&](const TCHAR* Name, double Value)
	{
		if (FProperty* P = AnimInst->GetClass()->FindPropertyByName(Name))
		{
			if (P->IsA<FDoubleProperty>())
			{
				if (double* Ptr = P->ContainerPtrToValuePtr<double>(AnimInst))
					*Ptr = Value;
			}
			else if (P->IsA<FFloatProperty>())
			{
				if (float* Ptr = P->ContainerPtrToValuePtr<float>(AnimInst))
					*Ptr = static_cast<float>(Value);
			}
		}
	};

	auto SetBool = [&](const TCHAR* Name, bool Value)
	{
		if (FProperty* P = AnimInst->GetClass()->FindPropertyByName(Name))
			if (bool* Ptr = P->ContainerPtrToValuePtr<bool>(AnimInst))
				*Ptr = Value;
	};

	auto SetVector = [&](const TCHAR* Name, FVector Value)
	{
		if (FProperty* P = AnimInst->GetClass()->FindPropertyByName(Name))
			if (FVector* Ptr = P->ContainerPtrToValuePtr<FVector>(AnimInst))
				*Ptr = Value;
	};

	SetDouble(TEXT("GroundSpeed"), bMoving ? static_cast<double>(Speed) : 0.0);
	SetDouble(TEXT("Direction"), 0.0);
	SetBool(TEXT("ShouldMove"), bMoving);
	SetBool(TEXT("IsFalling"), bFalling);
	SetVector(TEXT("Velocity"), bCurrentlySnapping && bMoving
		? (CurrentSnapTarget - GetActorLocation()).GetSafeNormal() * Speed
		: Vel);
}

// --- Slot Proximity Check ---

void ANPCCustomer::CheckSlotProximity()
{
	// Only check when heading to actual slot (not entry point) and not yet arrived
	if (bHasArrivedAtSlot) return;
	if (bGoingToEntryPoint) return;
	if (CurrentState != ENPCCustomerState::WalkingToCheckout &&
		CurrentState != ENPCCustomerState::WaitingAtCheckout) return;
	if (!AssignedCheckout || Rep_SlotIndex < 0) return;

	const FVector SlotLoc = AssignedCheckout->GetSlotLocation(Rep_SlotIndex);
	const float Dist2D = FVector::Dist2D(GetActorLocation(), SlotLoc);

	if (Dist2D <= SlotArrivalRadius)
	{
		// Close enough - start smooth snap
		bHasArrivedAtSlot = true;
		bIsSnapping = true; Rep_IsSnapping = true;
		Rep_SnapTarget = SnapTargetLocation = FVector(SlotLoc.X, SlotLoc.Y, GetActorLocation().Z);
		TimeSinceLastSlotRetry = 0.0f;

		// Stop pathfinding movement
		if (ANPCCustomerAIController* AIC = Cast<ANPCCustomerAIController>(GetController()))
		{
			AIC->StopMovement();
		}

		if (CurrentState == ENPCCustomerState::WalkingToCheckout)
		{
			SetState(ENPCCustomerState::WaitingAtCheckout);
		}

		// DON'T set rotation here - Tick will face toward snap target
		// and face the counter only when snap finishes
		AssignedCheckout->OnCustomerArrivedAtSlot(this, Rep_SlotIndex);

		UE_LOG(LogTemp, Log, TEXT("NPC %s: Arriving at slot %d (smooth)"),
			*GetName(), Rep_SlotIndex);
	}
	else
	{
		// Retry pathfinding if stuck
		TimeSinceLastSlotRetry += GetWorld()->GetDeltaSeconds();
		if (TimeSinceLastSlotRetry >= SlotRetryInterval)
		{
			TimeSinceLastSlotRetry = 0.0f;
			UE_LOG(LogTemp, Log, TEXT("NPC %s: Retrying path to slot %d (dist=%.0f)"),
				*GetName(), Rep_SlotIndex, Dist2D);
			NavigateToLocation(SlotLoc);
		}
	}
}

// --- Setup ---

void ANPCCustomer::InitializeCustomer(FName InRequestedItemID, UShopInventoryComponent* InShopInventory)
{
	check(HasAuthority());
	RequestedItemID = InRequestedItemID;
	ShopInventory = InShopInventory;

	// Cache display name for replication to clients
	if (ShopInventory)
	{
		FShopItemInfo Info;
		if (ShopInventory->GetItemInfo(RequestedItemID, Info))
		{
			Rep_DisplayName = Info.DisplayName.ToString();
		}
	}
	if (Rep_DisplayName.IsEmpty())
	{
		Rep_DisplayName = RequestedItemID.ToString();
	}

	UE_LOG(LogTemp, Log, TEXT("NPC %s initialized - wants: %s"), *GetName(), *RequestedItemID.ToString());
}

void ANPCCustomer::SetSpawnerReference(AShopCustomerSpawner* InSpawner)
{
	check(HasAuthority());
	OwningSpawner = InSpawner;
}

// --- State Machine ---

void ANPCCustomer::SetState(ENPCCustomerState NewState)
{
	check(HasAuthority());
	if (CurrentState == NewState) return;

	UE_LOG(LogTemp, Log, TEXT("NPC %s: %s -> %s (vel=%.1f)"),
		*GetName(),
		*UEnum::GetValueAsString(CurrentState),
		*UEnum::GetValueAsString(NewState),
		GetVelocity().Size2D());

	CurrentState = NewState;
	HandleStateChanged(NewState);
	OnStateChanged.Broadcast(this, NewState);
}

void ANPCCustomer::OnRep_CurrentState()
{
	HandleStateChanged(CurrentState);
	OnStateChanged.Broadcast(this, CurrentState);
}

void ANPCCustomer::OnRep_RequestedItemID()
{
	UpdateRequestWidget();
}

void ANPCCustomer::OnRep_WidgetData()
{
	// AttendOrder or Rep_SlotIndex changed on client
	UpdateRequestWidget();
}

void ANPCCustomer::HandleStateChanged_Implementation(ENPCCustomerState NewState)
{
	switch (NewState)
	{
	case ENPCCustomerState::Browsing:
		if (RequestTextWidget) RequestTextWidget->SetVisibility(true);
		SetWidgetText(NSLOCTEXT("Shop", "Browsing", "Looking around..."));
		break;

	case ENPCCustomerState::WaitingInShop:
		if (RequestTextWidget) RequestTextWidget->SetVisibility(false);
		break;

	case ENPCCustomerState::Attended:
		if (RequestTextWidget) RequestTextWidget->SetVisibility(true);
		UpdateRequestWidget();
		break;

	case ENPCCustomerState::WalkingToCheckout:
		if (RequestTextWidget) RequestTextWidget->SetVisibility(true);
		UpdateRequestWidget();
		break;

	case ENPCCustomerState::WaitingAtCheckout:
		if (RequestTextWidget) RequestTextWidget->SetVisibility(true);
		UpdateRequestWidget();
		break;

	case ENPCCustomerState::Paying:
		if (RequestTextWidget) RequestTextWidget->SetVisibility(false);
		break;

	case ENPCCustomerState::Leaving:
		if (RequestTextWidget) RequestTextWidget->SetVisibility(false);
		break;

	case ENPCCustomerState::Finished:
		if (HasAuthority()) SetLifeSpan(1.0f);
		break;

	default:
		break;
	}
}

// --- Flow ---

void ANPCCustomer::BeginCustomerFlow()
{
	check(HasAuthority());
	if (!OwningSpawner) return;
	TryClaimWaitingSpot();
}

void ANPCCustomer::TryClaimWaitingSpot()
{
	check(HasAuthority());
	if (!OwningSpawner) return;

	FVector SpotLocation;
	int32 SpotIndex = -1;

	if (OwningSpawner->ClaimWaitingSpot(SpotIndex, SpotLocation))
	{
		ClaimedWaitingSpotIndex = SpotIndex;
		SetState(ENPCCustomerState::WalkingToShop);
		NavigateToLocation(SpotLocation);
		GetWorldTimerManager().ClearTimer(BrowsingTimerHandle);
	}
	else
	{
		SetState(ENPCCustomerState::Browsing);
		FVector BrowseLocation = OwningSpawner->GetBrowsingLocation();
		NavigateToLocation(BrowseLocation);
		GetWorldTimerManager().SetTimer(BrowsingTimerHandle, this,
			&ANPCCustomer::TryClaimWaitingSpot, BrowsingRetryInterval, false);
	}
}

void ANPCCustomer::ReleaseWaitingSpot()
{
	if (OwningSpawner && ClaimedWaitingSpotIndex >= 0)
	{
		OwningSpawner->ReleaseWaitingSpot(ClaimedWaitingSpotIndex);
		ClaimedWaitingSpotIndex = -1;
	}
}

void ANPCCustomer::OnReachedDestination()
{
	check(HasAuthority());

	switch (CurrentState)
	{
	case ENPCCustomerState::WalkingToShop:
		SetState(ENPCCustomerState::WaitingInShop);
		break;

	case ENPCCustomerState::Browsing:
		// Reached browsing point, just idle
		break;

	case ENPCCustomerState::WalkingToCheckout:
		if (bGoingToEntryPoint)
		{
			// Arrived at entry point, now walk to actual slot
			bGoingToEntryPoint = false;
			if (AssignedCheckout && Rep_SlotIndex >= 0)
			{
				NavigateToLocation(AssignedCheckout->GetSlotLocation(Rep_SlotIndex));
				UE_LOG(LogTemp, Log, TEXT("NPC %s: Reached entry point, now heading to slot %d"),
					*GetName(), Rep_SlotIndex);
			}
		}
		else if (!bHasArrivedAtSlot && AssignedCheckout && Rep_SlotIndex >= 0)
		{
			// Arrived at actual slot - smooth snap to exact position
			bHasArrivedAtSlot = true;
			bIsSnapping = true; Rep_IsSnapping = true;
			Rep_SnapTarget = SnapTargetLocation = FVector(
				AssignedCheckout->GetSlotLocation(Rep_SlotIndex).X,
				AssignedCheckout->GetSlotLocation(Rep_SlotIndex).Y,
				GetActorLocation().Z);

			SetState(ENPCCustomerState::WaitingAtCheckout);
			// Rotation handled by snap Tick
			AssignedCheckout->OnCustomerArrivedAtSlot(this, Rep_SlotIndex);
		}
		break;

	case ENPCCustomerState::WaitingAtCheckout:
		// Shifted NPC arrived at new slot - smooth snap
		if (!bHasArrivedAtSlot && AssignedCheckout && Rep_SlotIndex >= 0)
		{
			bHasArrivedAtSlot = true;
			bIsSnapping = true; Rep_IsSnapping = true;
			Rep_SnapTarget = SnapTargetLocation = FVector(
				AssignedCheckout->GetSlotLocation(Rep_SlotIndex).X,
				AssignedCheckout->GetSlotLocation(Rep_SlotIndex).Y,
				GetActorLocation().Z);

			// Rotation handled by snap Tick
			AssignedCheckout->OnCustomerArrivedAtSlot(this, Rep_SlotIndex);
		}
		break;

	case ENPCCustomerState::Leaving:
		SetState(ENPCCustomerState::Finished);
		break;

	default:
		break;
	}
}

void ANPCCustomer::OnMoveToFailed()
{
	check(HasAuthority());

	UE_LOG(LogTemp, Warning, TEXT("NPC %s: MoveTo FAILED in %s (vel=%.1f pos=%s)"),
		*GetName(), *UEnum::GetValueAsString(CurrentState),
		GetVelocity().Size2D(), *GetActorLocation().ToCompactString());

	switch (CurrentState)
	{
	case ENPCCustomerState::WalkingToShop:
		ReleaseWaitingSpot();
		TryClaimWaitingSpot();
		break;

	case ENPCCustomerState::Browsing:
		// Failed to reach browse point, will retry on timer
		break;

	case ENPCCustomerState::WalkingToCheckout:
	case ENPCCustomerState::WaitingAtCheckout:
		// The proximity check in Tick will handle retries
		TimeSinceLastSlotRetry = 0.0f;
		break;

	case ENPCCustomerState::Leaving:
		SetState(ENPCCustomerState::Finished);
		break;

	default:
		break;
	}
}

void ANPCCustomer::AttendCustomer()
{
	check(HasAuthority());

	if (CurrentState != ENPCCustomerState::WaitingInShop)
	{
		UE_LOG(LogTemp, Warning, TEXT("NPC %s: Cannot attend, state is %s"),
			*GetName(), *UEnum::GetValueAsString(CurrentState));
		return;
	}

	bHasBeenAttended = true;

	// Assign attend order number
	GlobalAttendCounter++;
	AttendOrder = GlobalAttendCounter;

	SetState(ENPCCustomerState::Attended);
	ReleaseWaitingSpot();

	UE_LOG(LogTemp, Log, TEXT("NPC %s attended (#%d) - wants: %s"),
		*GetName(), AttendOrder, *RequestedItemID.ToString());

	GetWorldTimerManager().SetTimer(StateTimerHandle, [this]()
	{
		if (!OwningSpawner) return;

		AShopCheckout* Checkout = OwningSpawner->GetAvailableCheckout();
		if (Checkout)
		{
			SendToCheckout(Checkout);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("NPC %s: All checkouts full, retrying..."), *GetName());

			GetWorldTimerManager().SetTimer(StateTimerHandle, [this]()
			{
				if (!OwningSpawner) return;
				AShopCheckout* C = OwningSpawner->GetAvailableCheckout();
				if (C)
				{
					SendToCheckout(C);
				}
			}, 2.0f, true);
		}
	}, AttendedDuration, false);
}

void ANPCCustomer::SendToCheckout(AShopCheckout* Checkout)
{
	check(HasAuthority());

	// Stop retry timer to prevent double-claiming
	GetWorldTimerManager().ClearTimer(StateTimerHandle);

	int32 SlotIndex = Checkout->ClaimNextFreeSlot(this);
	if (SlotIndex < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("NPC %s: Checkout %s is full!"),
			*GetName(), *Checkout->GetName());
		return;
	}

	AssignedCheckout = Checkout;
	Rep_SlotIndex = SlotIndex;
	bHasArrivedAtSlot = false;
	bGoingToEntryPoint = false;
	TimeSinceLastSlotRetry = 0.0f;

	SetState(ENPCCustomerState::WalkingToCheckout);

	if (Checkout->ShouldUseEntryPoint(SlotIndex))
	{
		bGoingToEntryPoint = true;
		NavigateToLocation(Checkout->GetQueueEntryLocation());
		UE_LOG(LogTemp, Log, TEXT("NPC %s: Going to entry point first, then slot %d"),
			*GetName(), SlotIndex);
	}
	else
	{
		NavigateToLocation(Checkout->GetSlotLocation(SlotIndex));
	}
}

void ANPCCustomer::SetAssignedSlotIndex(int32 NewIndex)
{
	Rep_SlotIndex = NewIndex;
	bHasArrivedAtSlot = false;
	bGoingToEntryPoint = false;
	bIsSnapping = false;
	Rep_IsSnapping = false;
	TimeSinceLastSlotRetry = 0.0f;

	UpdateRequestWidget();
}

void ANPCCustomer::CompletePurchase(int32 PricePaid)
{
	check(HasAuthority());

	SetState(ENPCCustomerState::Paying);
	OnCustomerPaid.Broadcast(this, RequestedItemID, PricePaid);

	Rep_SlotIndex = -1;
	bHasArrivedAtSlot = false;

	GetWorldTimerManager().SetTimer(StateTimerHandle, [this]()
	{
		SetState(ENPCCustomerState::Leaving);
		if (OwningSpawner)
		{
			NavigateToLocation(OwningSpawner->GetExitLocation());
		}
		else
		{
			SetState(ENPCCustomerState::Finished);
		}
	}, PayingDuration, false);
}

// --- Helpers ---

void ANPCCustomer::NavigateToLocation(FVector Destination)
{
	ANPCCustomerAIController* AIC = Cast<ANPCCustomerAIController>(GetController());
	if (AIC)
	{
		AIC->MoveToDestination(Destination);
	}
}

void ANPCCustomer::UpdateRequestWidget()
{
	if (!RequestTextWidget) return;

	// States with custom widget text - don't overwrite
	if (CurrentState == ENPCCustomerState::Browsing)
	{
		SetWidgetText(NSLOCTEXT("Shop", "Browsing", "Looking around..."));
		return;
	}
	if (CurrentState == ENPCCustomerState::WaitingInShop)
	{
		// Widget hidden during waiting, but if called, show item
		FText ItemName = GetRequestedItemDisplayName();
		SetWidgetText(ItemName);
		return;
	}

	FText ItemName = GetRequestedItemDisplayName();

	// Build display text based on state
	FText DisplayText;
	if (AttendOrder > 0 && Rep_SlotIndex >= 0)
	{
		// "#3 Glock 19 [Slot 1]"
		DisplayText = FText::Format(
			NSLOCTEXT("Shop", "WidgetOrderSlot", "#{0} {1} [Slot {2}]"),
			FText::AsNumber(AttendOrder),
			ItemName,
			FText::AsNumber(Rep_SlotIndex)
		);
	}
	else if (AttendOrder > 0)
	{
		// "#3 Glock 19"
		DisplayText = FText::Format(
			NSLOCTEXT("Shop", "WidgetOrder", "#{0} {1}"),
			FText::AsNumber(AttendOrder),
			ItemName
		);
	}
	else
	{
		DisplayText = ItemName;
	}

	SetWidgetText(DisplayText);
}

void ANPCCustomer::SetWidgetText(const FText& Text)
{
	if (!RequestTextWidget) return;
	UNPCRequestWidget* Widget = Cast<UNPCRequestWidget>(RequestTextWidget->GetWidget());
	if (!Widget) return;
	Widget->SetRequestText(Text);
}

FText ANPCCustomer::GetRequestedItemDisplayName() const
{
	// Use replicated display name (works on both server and client)
	if (!Rep_DisplayName.IsEmpty())
	{
		return FText::FromString(Rep_DisplayName);
	}

	// Fallback: try ShopInventory (server only)
	if (ShopInventory)
	{
		FShopItemInfo Info;
		if (ShopInventory->GetItemInfo(RequestedItemID, Info))
		{
			return Info.DisplayName;
		}
	}
	return FText::FromName(RequestedItemID);
}