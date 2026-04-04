#pragma once
#include <cstdint>

namespace Off {
    // Verified UE5 offsets for The Isle Evrima
    constexpr uintptr_t GWorld = 0xB488B20;
    constexpr uintptr_t GNames = 0xB69E600;

    // UWorld -> UGameInstance -> ULocalPlayers[0] -> APlayerController
    constexpr uint32_t GameInstance = 0x228;
    constexpr uint32_t LocalPlayers = 0x38;
    
    // UWorld -> AGameStateBase -> TArray<APlayerState*>
    constexpr uint32_t GameState = 0x1B0;
    constexpr uint32_t PlayerArray = 0x2C0;

    // AActor -> USceneComponent (RootComponent) -> FVector (RelativeLocation)
    constexpr uint32_t RootComponent = 0x1B8;
    constexpr uint32_t RelativeLocation = 0x140;

    // APawn -> PlayerState/Controller
    constexpr uint32_t PlayerState = 0x2C8;
    constexpr uint32_t Score = 0x2A8;
    constexpr uint32_t PlayerId = 0x2AC;
    constexpr uint32_t CompressedPing = 0x2B0;
    constexpr uint32_t bIsBitfield = 0x2B2; // Bit 1 = Spec, Bit 3 = Bot
    constexpr uint32_t PlayerNamePrivate = 0x340; // On PlayerState -> FString
    constexpr uint32_t PawnPrivate = 0x320; // On PlayerState -> Pawn pointer

    // APlayerController -> APlayerCameraManager
    constexpr uint32_t CameraManager = 0x360;
    
    // APlayerCameraManager camera values (FCameraCacheEntry)
    // PCM + 0x1530 + 0x10 (FCameraCacheEntry POV)
    constexpr uint32_t CamLoc = 0x1530 + 0x10;
    constexpr uint32_t CamRot = 0x1530 + 0x10 + 0x18;
    constexpr uint32_t CamFOV = 0x1530 + 0x10 + 0x30;

    // APawn attributes
    constexpr uint32_t AttributeSet = 0x1378;
    constexpr uint32_t Attr_Health = 0x30 + 0xC;
    constexpr uint32_t Attr_MaxHealth = 0x40 + 0xC;
    constexpr uint32_t Attr_Stamina = 0x50 + 0xC;
    constexpr uint32_t Attr_MaxStamina = 0x60 + 0xC;
    
    // Other Pawn stats
    constexpr uint32_t Growth = 0x1D98;
    constexpr uint32_t bIsDead = 0x18B8;

    // Skeleton / Bones
    constexpr uint32_t SkeletalMeshComp = 0x328;       // ACharacter -> USkeletalMeshComponent* Mesh
    constexpr uint32_t CachedCompTransforms = 0x9B8;    // USkeletalMeshComponent -> TArray<FTransform>
    constexpr uint32_t RelativeRotation = 0x158;        // USceneComponent -> FRotator (3 doubles)
    constexpr uint32_t FTransformStride = 0x60;         // Size of one FTransform (doubles)
    constexpr uint32_t BoneTranslation = 0x30;          // Corrected from 0x20 to 0x30 (Matches Working 4.6)
}
