// [DeferredToon] Presentation-only layout for the existing volume properties.
#pragma once

#include "IDetailCustomization.h"

class FDeferredToonVolumeDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
