// [DeferredToon] Editor-only presentation language; never serialized into scene assets.
#pragma once

namespace DeferredToonDetailsLanguage
{
    bool IsChinese();
    void SetChinese(bool bChinese);
    void Apply(bool bChinese);
}
