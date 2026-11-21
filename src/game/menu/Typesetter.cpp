#include "game/menu/Typesetter.h"

#include <string>

namespace Mdk::Game::Menu
{
    std::tuple<std::vector<Typesetter::Line>, int> Typesetter::Layout(std::span<const byte> text, float y)
    {
        std::vector<Line> lines;
        std::vector<byte> line;
        float x = 0.f;
        bool centred = false;
        int start = 0;
        int count = 0;
        bool cost = true;
        size_t i = 0;
        while (i < text.size())
        {
            auto c = text[i++];
            if (c != Escape)
            {
                line.push_back(c);
                count += cost ? 1 : 0;
                continue;
            }

            auto j = i;
            while (j < text.size() && ((text[j] >= '0' && text[j] <= '9') || text[j] == '-'))
            {
                j++;
            }

            int number = 0;
            auto hasNumber = j > i && Str::TryParseInt(std::string(text.begin() + i, text.begin() + j), number);
            if (!hasNumber)
            {
                number = 0;
            }

            auto code = j < text.size() ? static_cast<char>(text[j]) : '\0';
            i = j + 1;
            switch (code)
            {
                case 'c':
                case 'n':
                case 'x':
                case 'y':
                    lines.push_back(Line{line, x, y, centred, start});
                    start = count;
                    line.clear();
                    switch (code)
                    {
                        case 'c':
                            x = hasNumber ? static_cast<float>(number) : Centre;
                            centred = true;
                            break;
                        case 'x':
                            x = hasNumber ? static_cast<float>(number) : 0.f;
                            centred = false;
                            break;
                        case 'n':
                            x = 0.f;
                            centred = false;
                            y = y + LineHeight + static_cast<float>(number);
                            break;
                        default:
                            x = 0.f;
                            centred = false;
                            y = y + (hasNumber ? static_cast<float>(number) : LineHeight);
                            break;
                    }
                    break;
                case 'p':
                    count += number;
                    break;
                case 'd':
                    cost = true;
                    break;
                case 'i':
                    cost = false;
                    break;
            }
        }

        lines.push_back(Line{line, x, y, centred, start});
        return {lines, count};
    }
}
