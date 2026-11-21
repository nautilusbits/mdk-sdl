#include "shared/GameData.h"

namespace Mdk::Tests
{
    const std::shared_ptr<Formats::MdkData>& GameData::Data()
    {
        static const auto data = Formats::MdkData::Find();
        return data;
    }

    const std::shared_ptr<Formats::BetaDemo>& BetaData::Demo()
    {
        static const auto demo = Formats::BetaDemo::Find(GameData::Data());
        return demo;
    }
}
