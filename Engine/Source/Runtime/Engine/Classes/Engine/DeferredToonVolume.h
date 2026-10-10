#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "DeferredToonVolume.generated.h"

class UDeferredToonVolumeComponent;
class UBoxComponent;

// [DeferredToon] Actor wrapper that owns the deferred toon volume component.
UCLASS(showcategories=(Rendering, Transformation, DataLayers), hidecategories=(Info, Object, Input), MinimalAPI)
class ADeferredToonVolume : public AInfo
{
    GENERATED_UCLASS_BODY()

public:
    UDeferredToonVolumeComponent* GetComponent() const { return DeferredToonVolumeComponent; }

#if WITH_EDITORONLY_DATA
    /** Keep the editor-only visualization box in sync with the authored component extent. */
    void SyncEditorBounds();
#endif

private:
    UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Deferred Toon Volume", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UDeferredToonVolumeComponent> DeferredToonVolumeComponent;

#if WITH_EDITORONLY_DATA
    /** Editor-only bounds visualization; it is disabled from all runtime rendering passes. */
    UPROPERTY(VisibleAnywhere, Transient, Category="Deferred Toon Volume", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UBoxComponent> VolumeBounds;
#endif
};
