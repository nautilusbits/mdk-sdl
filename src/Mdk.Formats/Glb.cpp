#include "Mdk.Formats/Glb.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <json.hpp>

#include "Mdk.Formats/BinReader.h"

namespace Mdk::Formats
{
    namespace
    {
        using Json = nlohmann::json;
        using OrderedJson = nlohmann::ordered_json;

        // System.Text.Json's JsonElement reads over nlohmann: null stands for an undefined element.

        const Json* Get(const Json* element, const char* name)
        {
            if (element == nullptr || !element->is_object())
            {
                return nullptr;
            }

            auto found = element->find(name);
            return found == element->end() ? nullptr : &*found;
        }

        std::vector<const Json*> Array(const Json* element, const char* name)
        {
            std::vector<const Json*> items;
            auto* array = Get(element, name);
            if (array == nullptr || !array->is_array())
            {
                return items;
            }

            for (auto& item : *array)
            {
                items.push_back(&item);
            }

            return items;
        }

        // JsonElement.GetInt32: InvalidOperationException on a non-number, FormatException when
        // the number isn't an Int32 (fractions, exponents, out of range).
        int GetInt32(const Json& element)
        {
            if (!element.is_number())
            {
                throw InvalidOperationException("The requested operation requires an element of type 'Number'.");
            }

            if (element.is_number_unsigned())
            {
                auto value = element.get<std::uint64_t>();
                if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                {
                    throw FormatException("One of the identified items was in an invalid format.");
                }

                return static_cast<int>(value);
            }

            if (element.is_number_integer())
            {
                auto value = element.get<std::int64_t>();
                if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                {
                    throw FormatException("One of the identified items was in an invalid format.");
                }

                return static_cast<int>(value);
            }

            throw FormatException("One of the identified items was in an invalid format.");
        }

        float GetSingle(const Json& element)
        {
            if (!element.is_number())
            {
                throw InvalidOperationException("The requested operation requires an element of type 'Number'.");
            }

            return static_cast<float>(element.get<double>());
        }

        std::optional<int> Int(const Json* element, const char* name)
        {
            auto* number = Get(element, name);
            if (number == nullptr || !number->is_number())
            {
                return std::nullopt;
            }

            return GetInt32(*number);
        }

        std::optional<std::string> Text(const Json* element, const char* name)
        {
            auto* text = Get(element, name);
            if (text == nullptr || !text->is_string())
            {
                return std::nullopt;
            }

            return text->get<std::string>();
        }

        std::vector<float> Floats(const Json* element, const char* name)
        {
            std::vector<float> floats;
            for (auto* item : Array(element, name))
            {
                floats.push_back(GetSingle(*item));
            }

            return floats;
        }

        // Enumerable.ElementAt: ArgumentOutOfRangeException past the end.
        const Json* ElementAt(const std::vector<const Json*>& items, int index)
        {
            if (index < 0 || index >= static_cast<int>(items.size()))
            {
                throw ArgumentException("Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
            }

            return items[index];
        }

        // float written as Utf8JsonWriter does: the shortest text reading back the same float.
        double JsonFloat(float value)
        {
            if (!std::isfinite(value))
            {
                throw ArgumentException(".NET number values such as positive and negative infinity cannot be written as valid JSON.");
            }

            double shortest = value;
            Str::TryParseDouble(Str::FromFloat(value), shortest);
            return shortest;
        }

        void WriteUInt32(std::vector<byte>& bytes, int at, uint value) { std::memcpy(bytes.data() + at, &value, sizeof(uint)); }

        template <typename T>
        void Append(std::vector<byte>& bytes, T value)
        {
            byte raw[sizeof(T)];
            std::memcpy(raw, &value, sizeof(T));
            bytes.insert(bytes.end(), raw, raw + sizeof(T));
        }
    }

    class Glb::Reader
    {
    public:
        Reader(const Json& root, const std::vector<byte>& bin) : _root(root), _bin(bin) {}

        // Every node under the scene's roots, with its transform to the scene.
        std::vector<std::pair<const Json*, Matrix4x4>> Nodes() const
        {
            std::vector<std::pair<const Json*, Matrix4x4>> result;
            auto nodes = Array(&_root, "nodes");
            std::vector<std::pair<int, Matrix4x4>> stack;
            auto roots = Roots(nodes);
            for (auto it = roots.rbegin(); it != roots.rend(); ++it)
            {
                stack.emplace_back(*it, Matrix4x4::Identity());
            }

            std::unordered_set<int> seen;
            while (!stack.empty())
            {
                auto [index, parent] = stack.back();
                stack.pop_back();
                if (index < 0 || index >= static_cast<int>(nodes.size()) || !seen.insert(index).second)
                {
                    continue;
                }

                auto* node = nodes[index];
                auto world = Local(*node) * parent;
                result.emplace_back(node, world);
                auto children = Array(node, "children");
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    stack.emplace_back(GetInt32(**it), world);
                }
            }

            return result;
        }

        Glb::Primitive Primitive(const Json& primitive, const Matrix4x4& world) const
        {
            if (Int(&primitive, "mode").value_or(Triangles) != Triangles)
            {
                throw InvalidDataException("glTF primitive not of triangles");
            }

            auto* attributes = Get(&primitive, "attributes");
            auto position = Int(attributes, "POSITION");
            if (!position)
            {
                throw InvalidDataException("glTF primitive without POSITION");
            }

            auto points = Vectors(*position, 3);
            std::vector<Vector3> positions(points.size());
            for (size_t i = 0; i < points.size(); i++)
            {
                positions[i] = Vector3::Transform(Vector3(points[i][0], points[i][1], points[i][2]), world);
            }

            std::vector<Vector2> uvs(positions.size());
            if (auto texcoord = Int(attributes, "TEXCOORD_0"))
            {
                auto read = Vectors(*texcoord, 2);
                for (size_t i = 0; i < Math::Min(read.size(), uvs.size()); i++)
                {
                    uvs[i] = Vector2(read[i][0], read[i][1]);
                }
            }

            std::vector<int> indices;
            if (auto accessor = Int(&primitive, "indices"))
            {
                for (auto& v : Vectors(*accessor, 1))
                {
                    indices.push_back(ToInt(v[0]));
                }
            }
            else
            {
                for (size_t i = 0; i < positions.size(); i++)
                {
                    indices.push_back(static_cast<int>(i));
                }
            }

            auto outside = std::any_of(indices.begin(), indices.end(), [&](int i) { return i < 0 || i >= static_cast<int>(positions.size()); });
            if (outside || indices.size() % 3 != 0)
            {
                throw InvalidDataException("glTF indices out of range");
            }

            return Glb::Primitive{std::move(positions), std::move(uvs), std::move(indices), Int(&primitive, "material").value_or(-1)};
        }

        Glb::Material Material(const Json& material) const
        {
            auto* pbr = Get(&material, "pbrMetallicRoughness");
            auto factor = Floats(pbr, "baseColorFactor");
            auto colour = factor.size() == 4 ? Vector4(factor[0], factor[1], factor[2], factor[3]) : Vector4::One();
            std::optional<std::vector<byte>> png;
            if (auto texture = Int(Get(pbr, "baseColorTexture"), "index"))
            {
                if (auto source = Int(ElementAt(Array(&_root, "textures"), *texture), "source"))
                {
                    png = Image(*ElementAt(Array(&_root, "images"), *source));
                }
            }

            return Glb::Material{Text(&material, "name").value_or(""), colour, std::move(png)};
        }

    private:
        const Json& _root;
        const std::vector<byte>& _bin;

        // The default scene's nodes, else every node no other one holds.
        std::vector<int> Roots(const std::vector<const Json*>& nodes) const
        {
            std::vector<int> roots;
            auto scenes = Array(&_root, "scenes");
            if (!scenes.empty())
            {
                auto* scene = scenes[Math::Clamp(Int(&_root, "scene").value_or(0), 0, static_cast<int>(scenes.size()) - 1)];
                for (auto* n : Array(scene, "nodes"))
                {
                    roots.push_back(GetInt32(*n));
                }

                return roots;
            }

            std::unordered_set<int> children;
            for (auto* n : nodes)
            {
                for (auto* c : Array(n, "children"))
                {
                    children.insert(GetInt32(*c));
                }
            }

            for (auto i = 0; i < static_cast<int>(nodes.size()); i++)
            {
                if (!children.contains(i))
                {
                    roots.push_back(i);
                }
            }

            return roots;
        }

        // A node's transform: its matrix (column-major: System.Numerics' row-vector layout), else scale, rotation, translation.
        static Matrix4x4 Local(const Json& node)
        {
            auto m = Floats(&node, "matrix");
            if (m.size() == 16)
            {
                return Matrix4x4(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
            }

            auto s = Floats(&node, "scale");
            auto r = Floats(&node, "rotation");
            auto t = Floats(&node, "translation");
            auto scale = s.size() == 3 ? Matrix4x4::CreateScale(s[0], s[1], s[2]) : Matrix4x4::Identity();
            auto rotation = r.size() == 4 ? Matrix4x4::CreateFromQuaternion(Quaternion(r[0], r[1], r[2], r[3])) : Matrix4x4::Identity();
            auto translation = t.size() == 3 ? Matrix4x4::CreateTranslation(t[0], t[1], t[2]) : Matrix4x4::Identity();
            return scale * rotation * translation;
        }

        // An embedded image's bytes (external files aren't read).
        std::vector<byte> Image(const Json& image) const
        {
            auto view = Int(&image, "bufferView");
            if (!view)
            {
                throw InvalidDataException("glTF image not embedded");
            }

            auto type = Text(&image, "mimeType");
            if (type && *type != PngType)
            {
                throw InvalidDataException(std::format("glTF image of {}, not PNG", *type));
            }

            auto [offset, length, stride] = View(*view);
            return Bin::Slice(_bin, offset, length);
        }

        std::tuple<int, int, int> View(int index) const
        {
            auto* view = ElementAt(Array(&_root, "bufferViews"), index);
            if (Int(view, "buffer").value_or(0) != 0)
            {
                throw InvalidDataException("glTF data outside the GLB's buffer");
            }

            auto offset = Int(view, "byteOffset").value_or(0);
            auto length = Int(view, "byteLength").value_or(0);
            if (offset < 0 || length < 0 || static_cast<long long>(offset) + length > static_cast<long long>(_bin.size()))
            {
                throw InvalidDataException("glTF buffer view past the end");
            }

            return {offset, length, Int(view, "byteStride").value_or(0)};
        }

        // An accessor's elements of `components` numbers: floats, or
        // unsigned integers (normalized when the accessor says so).
        std::vector<std::vector<float>> Vectors(int index, int components) const
        {
            auto* accessor = ElementAt(Array(&_root, "accessors"), index);
            if (Get(accessor, "sparse") != nullptr)
            {
                throw InvalidDataException("glTF sparse accessors aren't read");
            }

            auto count = Int(accessor, "count").value_or(0);
            auto type = Int(accessor, "componentType").value_or(Float);
            auto* normalizedElement = Get(accessor, "normalized");
            auto normalized = normalizedElement != nullptr && normalizedElement->is_boolean() && normalizedElement->get<bool>();
            int size;
            switch (type)
            {
                case UnsignedByte: size = 1; break;
                case UnsignedShort: size = 2; break;
                case UnsignedInt:
                case Float: size = 4; break;
                default: throw InvalidDataException(std::format("glTF component type {}", type));
            }

            int offset = 0;
            int stride = 0;
            if (auto view = Int(accessor, "bufferView"))
            {
                int length;
                std::tie(offset, length, stride) = View(*view);
            }

            auto sum = static_cast<long long>(offset) + Int(accessor, "byteOffset").value_or(0);
            if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
            {
                throw std::overflow_error("Arithmetic operation resulted in an overflow.");
            }

            offset = static_cast<int>(sum);
            stride = stride != 0 ? stride : size * components;
            if (count < 0 || offset < 0 || stride < 0
                || (count > 0 && offset + static_cast<long long>(count - 1) * stride + size * components > static_cast<long long>(_bin.size())))
            {
                throw InvalidDataException("glTF accessor past the end");
            }

            std::vector<std::vector<float>> result(count);
            for (auto i = 0; i < count; i++)
            {
                result[i].resize(components);
                for (auto c = 0; c < components; c++)
                {
                    auto at = offset + i * stride + c * size;
                    float value;
                    switch (type)
                    {
                        case Float:
                            value = Bin::F32(_bin, at);
                            break;
                        case UnsignedByte:
                            value = normalized ? _bin.at(at) / 255.0f : _bin.at(at);
                            break;
                        case UnsignedShort:
                            value = normalized ? Bin::U16(_bin, at) / 65535.0f : Bin::U16(_bin, at);
                            break;
                        default:
                            value = static_cast<float>(Bin::U32(_bin, at));
                            break;
                    }

                    result[i][c] = value;
                }
            }

            return result;
        }
    };

    Glb::Scene Glb::Read(std::span<const byte> bytes)
    {
        try
        {
            return Parse(bytes);
        }
        catch (const InvalidDataException&)
        {
            throw;
        }
        catch (const std::invalid_argument& e)
        {
            throw InvalidDataException(std::format("Broken glTF: {}", e.what()));
        }
        catch (const FormatException& e)
        {
            throw InvalidDataException(std::format("Broken glTF: {}", e.what()));
        }
        catch (const std::logic_error& e)
        {
            // InvalidOperationException, IndexOutOfRangeException (std::out_of_range).
            throw InvalidDataException(std::format("Broken glTF: {}", e.what()));
        }
        catch (const std::overflow_error& e)
        {
            throw InvalidDataException(std::format("Broken glTF: {}", e.what()));
        }
        catch (const nlohmann::json::exception& e)
        {
            throw InvalidDataException(std::format("Broken glTF: {}", e.what()));
        }
    }

    Glb::Scene Glb::Parse(std::span<const byte> bytes)
    {
        auto [jsonBytes, bin] = Chunks(bytes);
        Json root;
        try
        {
            root = Json::parse(jsonBytes.begin(), jsonBytes.end());
        }
        catch (const nlohmann::json::parse_error& e)
        {
            throw InvalidDataException(std::format("glTF JSON: {}", e.what()));
        }

        auto version = Text(Get(&root, "asset"), "version");
        if (!version || !Str::StartsWith(*version, "2."))
        {
            throw InvalidDataException("Not glTF 2.0");
        }

        Reader file(root, bin);
        std::vector<Glb::Material> materials;
        for (auto* material : Array(&root, "materials"))
        {
            materials.push_back(file.Material(*material));
        }

        std::vector<Mesh> meshes;
        for (auto& [node, world] : file.Nodes())
        {
            auto index = Int(node, "mesh");
            if (!index)
            {
                continue;
            }

            auto* mesh = ElementAt(Array(&root, "meshes"), *index);
            auto name = Text(node, "name");
            if (!name)
            {
                name = Text(mesh, "name");
            }

            std::vector<Glb::Primitive> primitives;
            for (auto* p : Array(mesh, "primitives"))
            {
                primitives.push_back(file.Primitive(*p, world));
            }

            meshes.push_back(Mesh{name.value_or(std::format("mesh{}", *index)), std::move(primitives)});
        }

        return Scene{std::move(meshes), std::move(materials)};
    }

    std::pair<std::vector<byte>, std::vector<byte>> Glb::Chunks(std::span<const byte> bytes)
    {
        if (bytes.size() < HeaderSize || Bin::U32(bytes, 0) != Magic)
        {
            throw InvalidDataException("Not a glTF binary file");
        }

        if (Bin::U32(bytes, 4) != Version)
        {
            throw InvalidDataException("Not glTF 2.0");
        }

        std::optional<std::vector<byte>> json;
        std::vector<byte> bin;
        auto total = static_cast<int>(bytes.size());
        auto at = HeaderSize;
        while (at + ChunkHeader <= total)
        {
            auto length = static_cast<int>(Bin::U32(bytes, at));
            auto type = Bin::U32(bytes, at + 4);
            if (length < 0 || length > total - at - ChunkHeader)
            {
                throw InvalidDataException("glTF chunk past the end");
            }

            auto data = Bin::Slice(bytes, at + ChunkHeader, length);
            if (type == JsonChunk)
            {
                json = std::move(data);
            }
            else if (type == BinChunk)
            {
                bin = std::move(data);
            }

            at += ChunkHeader + length;
        }

        if (!json)
        {
            throw InvalidDataException("glTF without JSON");
        }

        return {std::move(*json), std::move(bin)};
    }

    std::vector<byte> Glb::Write(const Scene& scene)
    {
        struct View
        {
            int Offset;
            int Length;
            std::optional<int> Target;
        };

        std::vector<byte> bin;
        std::vector<View> views;
        auto accessors = OrderedJson::array();

        auto addView = [&](std::span<const byte> data, std::optional<int> target)
        {
            while (bin.size() % Alignment != 0)
            {
                bin.push_back(0);
            }

            views.push_back(View{static_cast<int>(bin.size()), static_cast<int>(data.size()), target});
            bin.insert(bin.end(), data.begin(), data.end());
            return static_cast<int>(views.size()) - 1;
        };

        auto vector = [](Vector3 v) { return OrderedJson::array({JsonFloat(v.X), JsonFloat(v.Y), JsonFloat(v.Z)}); };

        auto addAccessor = [&](std::span<const byte> data, int count, int componentType, const char* type, std::optional<int> target,
                               std::optional<Vector3> min = std::nullopt, std::optional<Vector3> max = std::nullopt)
        {
            auto view = addView(data, target);
            OrderedJson accessor;
            accessor["bufferView"] = view;
            accessor["componentType"] = componentType;
            accessor["count"] = count;
            accessor["type"] = type;
            if (min && max)
            {
                accessor["min"] = vector(*min);
                accessor["max"] = vector(*max);
            }

            accessors.push_back(std::move(accessor));
            return static_cast<int>(accessors.size()) - 1;
        };

        OrderedJson root;
        root["asset"] = {{"version", "2.0"}, {"generator", "mdk-sdl"}};
        root["scene"] = 0;
        auto sceneNodes = OrderedJson::array();
        for (size_t i = 0; i < scene.Meshes.size(); i++)
        {
            sceneNodes.push_back(static_cast<int>(i));
        }

        OrderedJson sceneObject;
        sceneObject["nodes"] = std::move(sceneNodes);
        root["scenes"] = OrderedJson::array({std::move(sceneObject)});

        auto nodes = OrderedJson::array();
        for (size_t i = 0; i < scene.Meshes.size(); i++)
        {
            OrderedJson node;
            node["name"] = scene.Meshes[i].Name;
            node["mesh"] = static_cast<int>(i);
            nodes.push_back(std::move(node));
        }

        root["nodes"] = std::move(nodes);

        auto meshes = OrderedJson::array();
        for (auto& mesh : scene.Meshes)
        {
            OrderedJson meshObject;
            meshObject["name"] = mesh.Name;
            auto primitives = OrderedJson::array();
            for (auto& p : mesh.Primitives)
            {
                auto min = Vector3::Zero();
                auto max = Vector3::Zero();
                if (!p.Positions.empty())
                {
                    min = max = p.Positions[0];
                    for (size_t i = 1; i < p.Positions.size(); i++)
                    {
                        min = Vector3::Min(min, p.Positions[i]);
                        max = Vector3::Max(max, p.Positions[i]);
                    }
                }

                std::vector<byte> positionBytes;
                for (auto& v : p.Positions)
                {
                    Append(positionBytes, v.X);
                    Append(positionBytes, v.Y);
                    Append(positionBytes, v.Z);
                }

                std::vector<byte> uvBytes;
                for (auto& v : p.Uvs)
                {
                    Append(uvBytes, v.X);
                    Append(uvBytes, v.Y);
                }

                std::vector<byte> indexBytes;
                for (auto i : p.Indices)
                {
                    Append(indexBytes, static_cast<uint>(i));
                }

                auto position = addAccessor(positionBytes, static_cast<int>(p.Positions.size()), Float, "VEC3", ArrayBuffer, min, max);
                auto uv = addAccessor(uvBytes, static_cast<int>(p.Uvs.size()), Float, "VEC2", ArrayBuffer);
                auto indices = addAccessor(indexBytes, static_cast<int>(p.Indices.size()), UnsignedInt, "SCALAR", ElementArrayBuffer);
                OrderedJson primitive;
                primitive["attributes"] = {{"POSITION", position}, {"TEXCOORD_0", uv}};
                primitive["indices"] = indices;
                if (p.Material >= 0)
                {
                    primitive["material"] = p.Material;
                }

                primitive["mode"] = Triangles;
                primitives.push_back(std::move(primitive));
            }

            meshObject["primitives"] = std::move(primitives);
            meshes.push_back(std::move(meshObject));
        }

        root["meshes"] = std::move(meshes);

        std::vector<int> images;
        auto materials = OrderedJson::array();
        for (auto& material : scene.Materials)
        {
            OrderedJson materialObject;
            materialObject["name"] = material.Name;
            materialObject["doubleSided"] = true;
            OrderedJson pbr;
            pbr["baseColorFactor"] = OrderedJson::array({JsonFloat(material.Colour.X), JsonFloat(material.Colour.Y), JsonFloat(material.Colour.Z), JsonFloat(material.Colour.W)});
            if (material.Png)
            {
                pbr["baseColorTexture"] = {{"index", static_cast<int>(images.size())}};
                images.push_back(addView(*material.Png, std::nullopt));
            }

            pbr["metallicFactor"] = 0;
            pbr["roughnessFactor"] = 1;
            materialObject["pbrMetallicRoughness"] = std::move(pbr);
            materials.push_back(std::move(materialObject));
        }

        root["materials"] = std::move(materials);

        if (!images.empty())
        {
            auto textures = OrderedJson::array();
            for (size_t i = 0; i < images.size(); i++)
            {
                textures.push_back({{"source", static_cast<int>(i)}});
            }

            root["textures"] = std::move(textures);
            auto imageArray = OrderedJson::array();
            for (auto view : images)
            {
                OrderedJson image;
                image["bufferView"] = view;
                image["mimeType"] = PngType;
                imageArray.push_back(std::move(image));
            }

            root["images"] = std::move(imageArray);
        }

        root["accessors"] = std::move(accessors);

        while (bin.size() % Alignment != 0)
        {
            bin.push_back(0);
        }

        auto bufferViews = OrderedJson::array();
        for (auto& view : views)
        {
            OrderedJson viewObject;
            viewObject["buffer"] = 0;
            viewObject["byteOffset"] = view.Offset;
            viewObject["byteLength"] = view.Length;
            if (view.Target)
            {
                viewObject["target"] = *view.Target;
            }

            bufferViews.push_back(std::move(viewObject));
        }

        root["bufferViews"] = std::move(bufferViews);
        OrderedJson buffer;
        buffer["byteLength"] = static_cast<long long>(bin.size());
        root["buffers"] = OrderedJson::array({std::move(buffer)});

        auto text = root.dump();
        return Pack(std::vector<byte>(text.begin(), text.end()), bin);
    }

    std::vector<byte> Glb::Pack(const std::vector<byte>& json, const std::vector<byte>& bin)
    {
        auto jsonLength = Aligned(static_cast<int>(json.size()));
        auto binLength = static_cast<int>(bin.size());
        auto total = HeaderSize + ChunkHeader + jsonLength + (binLength > 0 ? ChunkHeader + binLength : 0);
        std::vector<byte> file(total);
        WriteUInt32(file, 0, Magic);
        WriteUInt32(file, 4, Version);
        WriteUInt32(file, 8, static_cast<uint>(total));
        WriteUInt32(file, HeaderSize, static_cast<uint>(jsonLength));
        WriteUInt32(file, HeaderSize + 4, JsonChunk);
        std::fill_n(file.begin() + HeaderSize + ChunkHeader, jsonLength, static_cast<byte>(' '));
        std::copy(json.begin(), json.end(), file.begin() + HeaderSize + ChunkHeader);
        if (binLength == 0)
        {
            return file;
        }

        auto at = HeaderSize + ChunkHeader + jsonLength;
        WriteUInt32(file, at, static_cast<uint>(binLength));
        WriteUInt32(file, at + 4, BinChunk);
        std::copy(bin.begin(), bin.end(), file.begin() + at + ChunkHeader);
        return file;
    }

    int Glb::Aligned(int length) { return (length + Alignment - 1) / Alignment * Alignment; }
}
