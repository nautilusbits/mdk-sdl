#include "engine/platform/TouchLayout.h"

#include <array>
#include <cctype>

#include "core/DotNet.h"
#include "engine/platform/TouchControls.h"

namespace Mdk::Engine::Platform
{
    namespace
    {
        // Enum.TryParse + Enum.IsDefined: a member's name (case-sensitive) or its number, spaces
        // around allowed.
        template <typename E, size_t N>
        bool TryParseEnum(const std::string& text, const std::array<const char*, N>& names, E& value)
        {
            std::string trimmed = Str::Trim(text);
            for (size_t i = 0; i < N; i++)
            {
                if (trimmed == names[i])
                {
                    value = static_cast<E>(i);
                    return true;
                }
            }

            int number = 0;
            bool numeric = !trimmed.empty() && (std::isdigit(static_cast<unsigned char>(trimmed[0])) || trimmed[0] == '-' || trimmed[0] == '+');
            if (!numeric || !Str::TryParseInt(trimmed, number) || number < 0 || number >= static_cast<int>(N))
            {
                return false;
            }

            value = static_cast<E>(number);
            return true;
        }

        constexpr std::array<const char*, 2> AcrossNames{"Left", "Right"};
        constexpr std::array<const char*, 2> AlongNames{"Top", "Bottom"};
    }

    std::string TouchPlace::ToString() const
    {
        std::string separator(1, Separator);
        return Str::Join(separator, {AcrossNames[static_cast<int>(X)], Number(GapX), AlongNames[static_cast<int>(Y)], Number(GapY),
                                     Number(WidthMm), Number(HeightMm)});
    }

    std::string TouchPlace::Number(float value) { return Str::FromFloat(value); }

    bool TouchPlace::TryParse(const std::string& text, TouchPlace& place)
    {
        place = {};
        auto parts = Str::Split(text, Separator);
        Across x{};
        Along y{};
        float gapX = 0;
        float gapY = 0;
        float width = 0;
        float height = 0;
        if (static_cast<int>(parts.size()) != Fields
            || !TryParseEnum(parts[0], AcrossNames, x)
            || !TryParseEnum(parts[2], AlongNames, y)
            || !Fraction(parts[1], gapX) || !Fraction(parts[3], gapY)
            || !Size(parts[4], width) || !Size(parts[5], height))
        {
            return false;
        }

        place = {x, gapX, y, gapY, width, height};
        return true;
    }

    bool TouchPlace::Fraction(const std::string& text, float& value)
    {
        return Str::TryParseFloat(text, value) && value >= 0.0f && value <= 1.0f;
    }

    bool TouchPlace::Size(const std::string& text, float& value)
    {
        return Str::TryParseFloat(text, value) && value > 0.0f && value <= MaxSizeMm;
    }

    const TouchLayout::Slot& TouchLayout::SlotOf(TouchPart part)
    {
        // The buttons by column from the right edge and row from the bottom; pause stands at the
        // top right, hit first (a button put over it can't hide it).
        static const std::array<Slot, 8> Slots{{
            {TouchPart::Pause, Key::Escape, "||", 0, 0},
            {TouchPart::Fire, Key::Fire, "FIRE", 0, 0},
            {TouchPart::Jump, Key::Jump, "JUMP", 1, 0},
            {TouchPart::Use, Key::UseItem, "USE", 0, 1},
            {TouchPart::Sniper, Key::Sniper, "SNIPE", 1, 1},
            {TouchPart::Item, Key::ItemNext, "ITEM", 0, 2},
            {TouchPart::ZoomIn, Key::ZoomIn, "+", 1, 2},
            {TouchPart::ZoomOut, Key::ZoomOut, "-", 2, 2},
        }};

        for (const auto& slot : Slots)
        {
            if (slot.Part == part)
            {
                return slot;
            }
        }

        throw InvalidOperationException("Sequence contains no matching element");
    }

    const std::vector<TouchPart>& TouchLayout::Parts()
    {
        static const std::vector<TouchPart> parts{
            TouchPart::Pause, TouchPart::Fire, TouchPart::Jump, TouchPart::Use, TouchPart::Sniper,
            TouchPart::Item, TouchPart::ZoomIn, TouchPart::ZoomOut, TouchPart::Stick,
        };
        return parts;
    }

    Key TouchLayout::KeyOf(TouchPart part) { return SlotOf(part).Key; }

    std::string TouchLayout::LabelOf(TouchPart part) { return part == TouchPart::Stick ? StickLabel : SlotOf(part).Label; }

    RectangleF TouchLayout::SafeOf(Vector2 screen, RectangleF safe)
    {
        return safe.Width > 0.0f && safe.Height > 0.0f ? safe : RectangleF(0.0f, 0.0f, screen.X, screen.Y);
    }

    float TouchLayout::UnitsPerMm(float unitsPerMm, Vector2 screen)
    {
        return unitsPerMm > 0.0f ? unitsPerMm : ButtonSize * screen.Y / ButtonMm;
    }

    RectangleF TouchLayout::AreaOf(TouchPart part, const TouchPlaces& places, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        auto found = places.find(part);
        return found != places.end()
                   ? Fit(part, Place(found->second, screen, safe, unitsPerMm), screen, safe, unitsPerMm)
                   : DefaultArea(part, screen, safe, unitsPerMm);
    }

    RectangleF TouchLayout::DefaultArea(TouchPart part, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        if (part == TouchPart::Stick)
        {
            return {0.0f, 0.0f, StickSide * screen.X, screen.Y};
        }

        float right = safe.Width > 0.0f ? MathF::Min(safe.Right(), screen.X) : screen.X;
        float top = safe.Height > 0.0f ? MathF::Max(safe.Top(), 0.0f) : 0.0f;
        float size = TouchControls::Physical(ButtonSize, ButtonMm, screen.Y, unitsPerMm);
        float margin = TouchControls::Physical(Margin, MarginMm, screen.Y, unitsPerMm);
        if (part == TouchPart::Pause)
        {
            return {right - margin - size, top + margin, size, size};
        }

        const Slot& slot = SlotOf(part);
        float step = size + margin;
        return {right - margin - size - slot.Column * step, screen.Y - RadarSpace * screen.Y - size - slot.Row * step, size, size};
    }

    RectangleF TouchLayout::Place(TouchPlace place, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        RectangleF area = SafeOf(screen, safe);
        float units = UnitsPerMm(unitsPerMm, screen);
        float width = place.WidthMm * units;
        float height = place.HeightMm * units;
        float x = place.X == Across::Left ? area.Left() + place.GapX * area.Width : area.Right() - place.GapX * area.Width - width;
        float y = place.Y == Along::Top ? area.Top() + place.GapY * area.Height : area.Bottom() - place.GapY * area.Height - height;
        return {x, y, width, height};
    }

    TouchPlace TouchLayout::PlaceOf(RectangleF area, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        RectangleF bounds = SafeOf(screen, safe);
        float units = UnitsPerMm(unitsPerMm, screen);
        Vector2 centre(area.X + area.Width / 2.0f, area.Y + area.Height / 2.0f);
        Across x = centre.X < bounds.X + bounds.Width / 2.0f ? Across::Left : Across::Right;
        Along y = centre.Y < bounds.Y + bounds.Height / 2.0f ? Along::Top : Along::Bottom;
        float gapX = x == Across::Left ? area.Left() - bounds.Left() : bounds.Right() - area.Right();
        float gapY = y == Along::Top ? area.Top() - bounds.Top() : bounds.Bottom() - area.Bottom();
        return {x, Math::Clamp(gapX / bounds.Width, 0.0f, 1.0f), y, Math::Clamp(gapY / bounds.Height, 0.0f, 1.0f), area.Width / units, area.Height / units};
    }

    RectangleF TouchLayout::Fit(TouchPart part, RectangleF area, Vector2 screen, RectangleF safe, float unitsPerMm)
    {
        RectangleF bounds = SafeOf(screen, safe);
        auto [width, height] = part == TouchPart::Stick ? ZoneSize(area, screen, bounds, unitsPerMm) : ButtonSide(area, screen, bounds, unitsPerMm);
        float x = Math::Clamp(area.X, bounds.Left(), MathF::Max(bounds.Right() - width, bounds.Left()));
        float y = Math::Clamp(area.Y, bounds.Top(), MathF::Max(bounds.Bottom() - height, bounds.Top()));
        return {x, y, width, height};
    }

    std::pair<float, float> TouchLayout::ButtonSide(RectangleF area, Vector2 screen, RectangleF bounds, float unitsPerMm)
    {
        float units = UnitsPerMm(unitsPerMm, screen);
        float max = MathF::Min(MaxMm * units, MaxPart * MathF::Min(bounds.Width, bounds.Height));
        float side = Math::Clamp(MathF::Max(area.Width, area.Height), MathF::Min(MinMm * units, max), max);
        return {side, side};
    }

    std::pair<float, float> TouchLayout::ZoneSize(RectangleF area, Vector2 screen, RectangleF bounds, float unitsPerMm)
    {
        float reach = 2.0f * TouchControls::StickReach(screen.Y, unitsPerMm);
        float max = MaxZone * bounds.Width;
        float width = Math::Clamp(area.Width, MathF::Min(reach, max), max);
        float height = Math::Clamp(area.Height, MathF::Min(reach, bounds.Height), bounds.Height);
        return {width, height};
    }

    bool TouchLayout::Overlaps(TouchPart part, const std::map<TouchPart, RectangleF>& areas)
    {
        auto found = areas.find(part);
        if (!IsButton(part) || found == areas.end())
        {
            return false;
        }

        RectangleF area = found->second;
        for (const auto& [other, rect] : areas)
        {
            if (other != part && IsButton(other) && rect.IntersectsWith(area))
            {
                return true;
            }
        }

        return false;
    }
}
