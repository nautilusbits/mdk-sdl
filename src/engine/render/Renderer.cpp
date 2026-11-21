#include "engine/render/Renderer.h"

#include "core/Console.h"
#include "core/Embedded.h"
#include "engine/platform/Window.h"
#include "engine/render/Bmp.h"
#include "engine/render/DepthPull.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>

namespace Mdk::Engine::Render
{
    namespace
    {
        // Stopwatch.GetElapsedTime(start): the time since a StopwatchTicks() reading.
        TimeSpan ElapsedSince(long long start)
        {
            return TimeSpan((StopwatchTicks() - start) * TicksPerSecond / StopwatchFrequency);
        }

        // MemoryMarshal.AsBytes: the vertices as the bytes the GPU reads.
        std::span<const byte> AsBytes(std::span<const Vertex> vertices)
        {
            return {reinterpret_cast<const byte*>(vertices.data()), vertices.size_bytes()};
        }

        // Pass.ToString().
        const char* PassName(Pass pass)
        {
            constexpr std::array names{"Solid", "DoubleSided", "Mirror", "Blended", "OnTop", "Overlay"};
            return names[static_cast<size_t>(pass)];
        }

        // C# hashes record fields together (HashCode.Combine).
        void Combine(size_t& seed, size_t value)
        {
            seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
    }

    Matrix4x4 ViewCamera::SkyDirections() const
    {
        const auto forward = Vector3::Normalize(Forward);
        const auto right = Vector3::Normalize(Vector3::Cross(forward, Up));
        const auto up = Vector3::Cross(right, forward);
        const auto across = right / Projection.M11;
        const auto upwards = up / Projection.M22;
        const auto centre = forward + right * (Projection.M31 / Projection.M11) + up * (Projection.M32 / Projection.M22);
        return Matrix4x4(
            across.X, across.Y, across.Z, 0.0f,
            upwards.X, upwards.Y, upwards.Z, 0.0f,
            centre.X, centre.Y, centre.Z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    View View::Eye(float side, float separation, float convergence) const
    {
        if (!Camera.has_value() || side == 0.0f)
        {
            return *this;
        }

        const auto& camera = *Camera;
        const auto right = Vector3::Normalize(Vector3::Cross(camera.Forward, camera.Up));
        const auto position = Position + right * (side * separation * 0.5f);
        const auto view = Matrix4x4::CreateLookAt(position, position + camera.Forward, camera.Up);
        auto projection = camera.Projection;
        if (convergence > 0.0f)
        {
            // Shifts the frustum sideways so the eyes' rays meet at the convergence distance
            // (the same off-centre projection the scope uses).
            const auto shift = -side * separation * camera.Near / (2.0f * convergence);
            projection.M31 += shift * projection.M11 / camera.Near;
        }

        // The sky's rays follow the eye's frustum too: its panorama stays at infinity (the eyes'
        // directions at a pixel differ as a camera axis' would, not as a finite surface's).
        const auto rotation = Matrix4x4::CreateLookAt(Vector3::Zero(), camera.Forward, camera.Up);
        Matrix4x4 clipToDirection;
        Matrix4x4::Invert(rotation * projection, clipToDirection);
        return View{view * projection, clipToDirection, position, camera};
    }

    size_t Renderer::PipelineKeyHash::operator()(const PipelineKey& key) const
    {
        auto seed = std::hash<std::string>{}(key.Program);
        Combine(seed, static_cast<size_t>(key.Pass));
        Combine(seed, static_cast<size_t>(key.Primitive));
        Combine(seed, static_cast<size_t>(key.Geometry));
        Combine(seed, key.Samples);
        Combine(seed, static_cast<size_t>(key.Output));
        Combine(seed, static_cast<size_t>(key.Blend));
        return seed;
    }

    Renderer::Renderer(Platform::Window& window, GpuBackend backend) : _window(window)
    {
        _requested = backend;
        _device = CreateDevice(backend, _shaderFormat);
        Check(_device != nullptr, "SDL_CreateGPUDevice");
        Check(SDL_ClaimWindowForGPUDevice(_device, window.Handle()), "SDL_ClaimWindowForGPUDevice");
        _gpu = ReadGpuInfo();
        _swapchainFormat = FormatName(SDL_GetGPUSwapchainTextureFormat(_device, window.Handle()));

        const SDL_GPUTextureUsageFlags depthUsage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        _depthFormat = SDL_GPUTextureSupportsFormat(_device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, depthUsage)
            ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
            : SDL_GPU_TEXTUREFORMAT_D24_UNORM;

        _repeatSampler = CreateSampler(SDL_GPU_SAMPLERADDRESSMODE_REPEAT);
        _clampSampler = CreateSampler(SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);

        // The enhanced look samples its depth buffers.
        const SDL_GPUTextureUsageFlags sampledDepth = depthUsage | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        _sampledDepthFormat = SDL_GPUTextureSupportsFormat(_device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, sampledDepth)
            ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
            : SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        _mipSampler = CreateSampler(SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, Sampling::Linear);
        _depthFormatName = FormatName(_depthFormat);

        // The stream's room is kept whole: spans Stream gave out stay valid through the frame.
        _stream.reserve(StreamVertices);
        _canvasMesh = CreateDynamicMesh(CanvasQuads * QuadVertices);
        _streamMesh = CreateDynamicMesh(StreamVertices);
        CreateColourSampler();

        // Texture 0 is bound for flat colours, so it must outlive every scope (Release).
        const std::array<byte, BytesPerPixel> blank{};
        CreateRgbaTexture(1, 1, blank);
    }

    Vector2 Renderer::CanvasPoint(float x, float y) const
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(_window.Handle(), &width, &height);
        return height <= 0 ? Vector2::Zero() : Vector2(x, y) * (CanvasHeight / static_cast<float>(height));
    }

    RectangleF Renderer::CanvasSafeArea() const
    {
        const RectangleF canvas{0.0f, 0.0f, CanvasWidth(), CanvasHeight};
        const auto [width, height] = _window.Size();
        const auto safe = _window.SafeArea();
        if (height <= 0 || safe == RectangleF{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)})
        {
            return canvas;
        }

        const auto scale = CanvasHeight / static_cast<float>(height);
        return RectangleF::FromLTRB(safe.Left() * scale, safe.Top() * scale,
            canvas.Right() - (static_cast<float>(width) - safe.Right()) * scale, canvas.Bottom() - (static_cast<float>(height) - safe.Bottom()) * scale);
    }

    void Renderer::Release(Scope scope)
    {
        FlushUploads();
        SDL_WaitForGPUIdle(_device);
        ReleaseColoursFrom(scope.Textures);
        for (int i = static_cast<int>(_textures.size()) - 1; i >= scope.Textures; i--)
        {
            SDL_ReleaseGPUTexture(_device, _textures[i]);
            _textures.erase(_textures.begin() + i);
        }

        for (int i = static_cast<int>(_buffers.size()) - 1; i >= scope.Buffers; i--)
        {
            SDL_ReleaseGPUBuffer(_device, _buffers[i]);
            if (const auto dynamic = _dynamic.find(i); dynamic != _dynamic.end())
            {
                SDL_ReleaseGPUTransferBuffer(_device, dynamic->second.Transfer);
                _dynamic.erase(dynamic);
            }

            _buffers.erase(_buffers.begin() + i);
        }

        Panorama.reset();
        Backdrop = Render::Backdrop::Sky;
        Lighting.reset();
        _lights.Clear();
    }

    void Renderer::DrawImage(int texture, int palette, Vector2 textureSize, RectangleF source, RectangleF target, Vector4 tint)
    {
        const auto uv0 = Vector2(source.Left(), source.Top()) / textureSize;
        const auto uv1 = Vector2(source.Right(), source.Bottom()) / textureSize;
        AddQuad(target, uv0, uv1, Material(texture, palette, tint, 1, Render::Pass::Overlay));
    }

    void Renderer::FillRect(RectangleF target, Vector4 colour)
    {
        AddQuad(target, Vector2::Zero(), Vector2::Zero(), Material::Flat(colour, Render::Pass::Overlay));
    }

    void Renderer::AddRect(RectangleF target, Vector4 colour)
    {
        auto material = Material::Flat(colour, Render::Pass::Overlay);
        material.Blend = Render::Blend::Add;
        AddQuad(target, Vector2::Zero(), Vector2::Zero(), material);
    }

    void Renderer::FrameRect(RectangleF target, float width, Vector4 colour)
    {
        FillRect(RectangleF{target.Left(), target.Top(), target.Width, width}, colour);
        FillRect(RectangleF{target.Left(), target.Bottom() - width, target.Width, width}, colour);
        FillRect(RectangleF{target.Left(), target.Top() + width, width, target.Height - 2 * width}, colour);
        FillRect(RectangleF{target.Right() - width, target.Top() + width, width, target.Height - 2 * width}, colour);
    }

    void Renderer::AddQuad(RectangleF target, Vector2 uv0, Vector2 uv1, const Render::Material& material)
    {
        if (static_cast<int>(_canvasVertices.size()) >= CanvasQuads * QuadVertices)
        {
            return;
        }

        const auto first = static_cast<int>(_canvasVertices.size());
        const auto corner = [](float x, float y, float u, float v) { return Vertex(Vector3(x, y, 0.0f), Vector2(u, v)); };
        _canvasVertices.push_back(corner(target.Left(), target.Top(), uv0.X, uv0.Y));
        _canvasVertices.push_back(corner(target.Right(), target.Top(), uv1.X, uv0.Y));
        _canvasVertices.push_back(corner(target.Right(), target.Bottom(), uv1.X, uv1.Y));
        _canvasVertices.push_back(corner(target.Left(), target.Top(), uv0.X, uv0.Y));
        _canvasVertices.push_back(corner(target.Right(), target.Bottom(), uv1.X, uv1.Y));
        _canvasVertices.push_back(corner(target.Left(), target.Bottom(), uv0.X, uv1.Y));

        // Neighbouring quads of one material draw together.
        if (!_canvasCommands.empty() && _canvasCommands.back().Material == material)
        {
            auto& last = _canvasCommands.back();
            last = {last.First, last.Count + QuadVertices, material};
            return;
        }

        _canvasCommands.push_back({first, QuadVertices, material});
    }

    void Renderer::Check(bool ok, const std::string& what)
    {
        if (!ok)
        {
            throw InvalidOperationException(std::format("{}: {}", what, SDL_GetError()));
        }
    }

    SDL_GPUDevice* Renderer::CreateDevice(GpuBackend backend, ShaderFormat& format)
    {
        if (backend != GpuBackend::Auto && GpuBackends::Resolve(backend, Platform::OsInfo::Current()) != backend)
        {
            Console::Error().WriteLine(std::format("GPU backend {} not on this OS: using SDL's choice", GpuBackends::Name(backend)));
        }

        for (const auto attempt : GpuBackends::Attempts(backend, Platform::OsInfo::Current()))
        {
            auto* device = attempt == GpuBackend::Auto ? CreateAuto(format) : CreateFor(attempt, format);
            if (device != nullptr)
            {
                return device;
            }
        }

        format = {};
        return nullptr;
    }

    Renderer::ShaderFormat Renderer::FormatOf(GpuBackend backend)
    {
        switch (backend)
        {
        case GpuBackend::Direct3D12:
            return ShaderFormats[0];
        case GpuBackend::Metal:
            return ShaderFormats[1];
        default:
            return ShaderFormats[2];
        }
    }

    bool Renderer::Embedded(ShaderFormat format)
    {
        return !::Mdk::Embedded::Find(std::string(ShaderProbe) + std::string(format.Extension)).empty();
    }

    SDL_GPUDevice* Renderer::CreateFor(GpuBackend backend, ShaderFormat& format)
    {
        format = FormatOf(backend);
        const auto name = GpuBackends::Name(backend);
        if (!Embedded(format))
        {
            Console::Error().WriteLine(std::format("GPU backend {}: no {} shaders in this build, using SDL's choice", name, format.Extension));
            return nullptr;
        }

        auto* device = CreateWith(GpuBackends::Driver(backend), format.Format);
        if (device == nullptr)
        {
            Console::Error().WriteLine(std::format("GPU backend {} failed ({}), using SDL's choice", name, SDL_GetError()));
        }

        return device;
    }

    SDL_GPUDevice* Renderer::CreateWith(const std::optional<std::string>& driver, SDL_GPUShaderFormat format)
    {
        const auto properties = SDL_CreateProperties();
        if (driver.has_value())
        {
            SDL_SetStringProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, driver->c_str());
        }

        if (format == SDL_GPU_SHADERFORMAT_DXIL)
        {
            SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
        }
        else if (format == SDL_GPU_SHADERFORMAT_MSL)
        {
            SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
        }
        else
        {
            SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        }

        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
        SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, GpuValidation);
        auto* device = SDL_CreateGPUDeviceWithProperties(properties);
        SDL_DestroyProperties(properties);
        return device;
    }

    std::vector<GpuBackend> Renderer::Backends()
    {
        std::vector<GpuBackend> backends;
        for (const auto b : GpuBackends::On(Platform::OsInfo::Current()))
        {
            const auto driver = GpuBackends::Driver(b);
            if (b == GpuBackend::Auto
                || (Embedded(FormatOf(b)) && SDL_GPUSupportsShaderFormats(FormatOf(b).Format, driver.has_value() ? driver->c_str() : nullptr)))
            {
                backends.push_back(b);
            }
        }

        return backends;
    }

    GpuInfo Renderer::ReadGpuInfo()
    {
        const auto properties = SDL_GetGPUDeviceProperties(_device);
        const auto orEmpty = [](const char* text) { return std::string(text != nullptr ? text : ""); };
        return GpuInfo{GpuBackends::Of(orEmpty(SDL_GetGPUDeviceDriver(_device))),
            orEmpty(SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "")),
            orEmpty(SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, ""))};
    }

    SDL_GPUDevice* Renderer::CreateAuto(ShaderFormat& format)
    {
        for (const auto& candidate : ShaderFormats)
        {
            if (!Embedded(candidate))
            {
                continue;
            }

            auto* device = CreateWith(std::nullopt, candidate.Format);
            if (device == nullptr)
            {
                continue;
            }

            format = candidate;
            return device;
        }

        format = {};
        return nullptr;
    }

    SDL_GPUSampler* Renderer::CreateSampler(SDL_GPUSamplerAddressMode mode, Sampling sampling)
    {
        const auto linear = sampling == Sampling::Linear;
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mag_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode = linear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = mode;
        info.address_mode_v = mode;
        info.address_mode_w = mode;
        info.max_lod = linear ? AllMips : 0.0f;
        return SDL_CreateGPUSampler(_device, &info);
    }

    SDL_GPUShader* Renderer::LoadShader(const std::string& name, SDL_GPUShaderStage stage, uint samplers)
    {
        const auto resource = "shaders/" + name + std::string(_shaderFormat.Extension);
        const auto code = ::Mdk::Embedded::Find(resource);
        if (code.empty())
        {
            throw InvalidOperationException(std::format("Shader {} not embedded: {}", name, resource));
        }

        const char* entry = stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vs_main" : "ps_main";
        SDL_GPUShaderCreateInfo info{};
        info.code_size = code.size();
        info.code = code.data();
        info.entrypoint = entry;
        info.format = _shaderFormat.Format;
        info.stage = stage;
        info.num_samplers = samplers;
        info.num_uniform_buffers = UniformBuffers(name);
        auto* shader = SDL_CreateGPUShader(_device, &info);
        Check(shader != nullptr, "SDL_CreateGPUShader " + name);
        return shader;
    }

    SDL_GPUGraphicsPipeline* Renderer::Pipeline(const PipelineKey& key)
    {
        if (const auto found = _pipelines.find(key); found != _pipelines.end())
        {
            return found->second;
        }

        auto* pipeline = CreatePipeline(key);
        _pipelines[key] = pipeline;
        return pipeline;
    }

    SDL_GPUGraphicsPipeline* Renderer::CreatePipeline(const PipelineKey& key)
    {
        const auto& [program, pass, primitive, geometry, samples, output, blend] = key;
        auto* vertexShader = LoadShader(program + ".vs", SDL_GPU_SHADERSTAGE_VERTEX, 0);
        auto* fragmentShader = LoadShader(program + ".ps", SDL_GPU_SHADERSTAGE_FRAGMENT, SamplerCount(program));
        SDL_GPUVertexBufferDescription bufferDescription{};
        bufferDescription.slot = 0;
        bufferDescription.pitch = sizeof(Vertex);
        bufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        std::array<SDL_GPUVertexAttribute, VertexAttributes> attributes{};
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0};
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<uint>(sizeof(Vector3))};
        attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, static_cast<uint>(sizeof(Vector3) + sizeof(Vector2))};

        const auto blended = pass == Render::Pass::Blended || pass == Render::Pass::Overlay;
        SDL_GPUColorTargetDescription colourTarget{};
        colourTarget.format = ColourFormat;
        colourTarget.blend_state.enable_blend = blended;
        colourTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colourTarget.blend_state.dst_color_blendfactor = blend == Render::Blend::Add ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colourTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        colourTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        colourTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colourTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertexShader;
        info.fragment_shader = fragmentShader;
        info.vertex_input_state.vertex_buffer_descriptions = &bufferDescription;
        info.vertex_input_state.num_vertex_buffers = geometry == Geometry::Mesh ? 1u : 0u;
        info.vertex_input_state.vertex_attributes = attributes.data();
        info.vertex_input_state.num_vertex_attributes = geometry == Geometry::Mesh ? VertexAttributes : 0u;
        info.primitive_type = primitive == Render::Primitive::Lines ? SDL_GPU_PRIMITIVETYPE_LINELIST : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = pass == Render::Pass::Solid && geometry == Geometry::Mesh && output != Output::Shadow ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
        // MDK's front faces wind clockwise on screen.
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        // Clips at the near and far planes: the device has no depth clamping (CreateWith).
        info.rasterizer_state.enable_depth_clip = true;
        info.rasterizer_state.enable_depth_bias = output == Output::Shadow;
        info.rasterizer_state.depth_bias_slope_factor = output == Output::Shadow ? ShadowSlopeBias : 0.0f;
        info.multisample_state.sample_count = SampleCount(samples);
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.depth_stencil_state.enable_depth_test = geometry == Geometry::Mesh && pass != Render::Pass::Overlay && pass != Render::Pass::OnTop;
        info.depth_stencil_state.enable_depth_write = geometry == Geometry::Mesh && !blended && pass != Render::Pass::OnTop;
        info.target_info.color_target_descriptions = &colourTarget;
        info.target_info.num_color_targets = output == Output::Colour ? 1u : 0u;
        info.target_info.depth_stencil_format = output == Output::Colour ? _depthFormat : _sampledDepthFormat;
        info.target_info.has_depth_stencil_target = geometry != Geometry::Post;
        auto* pipeline = SDL_CreateGPUGraphicsPipeline(_device, &info);
        Check(pipeline != nullptr, std::format("SDL_CreateGPUGraphicsPipeline {} {}", program, PassName(pass)));
        SDL_ReleaseGPUShader(_device, vertexShader);
        SDL_ReleaseGPUShader(_device, fragmentShader);
        return pipeline;
    }

    int Renderer::CreateIndexTexture(int width, int height, std::span<const byte> indices)
    {
        auto* texture = CreateTexture(SDL_GPU_TEXTUREFORMAT_R8_UNORM, static_cast<uint>(width), static_cast<uint>(height), SDL_GPU_TEXTUREUSAGE_SAMPLER);
        Upload(texture, static_cast<uint>(width), static_cast<uint>(height), indices);
        _textures.push_back(texture);
        KeepIndices(static_cast<int>(_textures.size()) - 1, width, height, indices);
        return static_cast<int>(_textures.size()) - 1;
    }

    void Renderer::UpdateTexture(int texture, int width, int height, std::span<const byte> pixels)
    {
        Upload(_textures[texture], static_cast<uint>(width), static_cast<uint>(height), pixels);
        Changed(texture, width, height, pixels);
    }

    int Renderer::CreatePalette(std::span<const byte> rgba)
    {
        const auto palette = CreateRgbaTexture(PaletteSize, 1, rgba);
        KeepPalette(palette, rgba);
        return palette;
    }

    int Renderer::CreateRgbaTexture(int width, int height, std::span<const byte> rgba)
    {
        auto* texture = CreateTexture(ColourFormat, static_cast<uint>(width), static_cast<uint>(height), SDL_GPU_TEXTUREUSAGE_SAMPLER);
        Upload(texture, static_cast<uint>(width), static_cast<uint>(height), rgba);
        _textures.push_back(texture);
        return static_cast<int>(_textures.size()) - 1;
    }

    int Renderer::CreateMesh(std::span<const Vertex> vertices)
    {
        const auto size = static_cast<uint>(vertices.size() * sizeof(Vertex));
        SDL_GPUBufferCreateInfo bufferInfo{};
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufferInfo.size = size;
        auto* buffer = SDL_CreateGPUBuffer(_device, &bufferInfo);
        Check(buffer != nullptr, "SDL_CreateGPUBuffer");

        SDL_GPUTransferBuffer* transfer = nullptr;
        uint offset = 0;
        auto* copy = UploadPass(AsBytes(vertices), transfer, offset);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transfer;
        source.offset = offset;
        SDL_GPUBufferRegion destination{};
        destination.buffer = buffer;
        destination.size = size;
        SDL_UploadToGPUBuffer(copy, &source, &destination, false);

        _buffers.push_back(buffer);
        return static_cast<int>(_buffers.size()) - 1;
    }

    int Renderer::CreateDynamicMesh(int capacity)
    {
        const auto size = static_cast<uint>(capacity * static_cast<int>(sizeof(Vertex)));
        SDL_GPUBufferCreateInfo bufferInfo{};
        bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufferInfo.size = size;
        auto* buffer = SDL_CreateGPUBuffer(_device, &bufferInfo);
        Check(buffer != nullptr, "SDL_CreateGPUBuffer");
        _buffers.push_back(buffer);
        const auto id = static_cast<int>(_buffers.size()) - 1;
        _dynamic[id] = DynamicMesh{CreateTransfer(size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD)};
        return id;
    }

    void Renderer::UpdateMesh(int mesh, std::span<const Vertex> vertices)
    {
        auto& dynamic = _dynamic.at(mesh);
        const auto bytes = AsBytes(vertices);
        auto* mapped = SDL_MapGPUTransferBuffer(_device, dynamic.Transfer, true);
        std::memcpy(mapped, bytes.data(), bytes.size());
        SDL_UnmapGPUTransferBuffer(_device, dynamic.Transfer);
        dynamic.Pending = static_cast<uint>(bytes.size());
    }

    std::span<Vertex> Renderer::Stream(int count, int& mesh, int& first)
    {
        mesh = _streamMesh;
        first = static_cast<int>(_stream.size());
        if (first + count > StreamVertices)
        {
            return {};
        }

        _stream.resize(first + count);
        return std::span<Vertex>(_stream).subspan(first, count);
    }

    void Renderer::QueueStream()
    {
        if (!_stream.empty())
        {
            UpdateMesh(_streamMesh, _stream);
        }

        _stream.clear();
    }

    void Renderer::QueueCanvas()
    {
        if (!_canvasVertices.empty())
        {
            UpdateMesh(_canvasMesh, _canvasVertices);
        }

        // The canvas fills the view, or keeps to its middle (the VR viewer: a margin around it).
        const auto margin = Vector2(CanvasWidth(), CanvasHeight) * ((1.0f / StereoModes::CanvasZoom(StereoMode) - 1.0f) / 2.0f);
        const auto screen = Matrix4x4::CreateOrthographicOffCenter(-margin.X, CanvasWidth() + margin.X, CanvasHeight + margin.Y, -margin.Y, 0.0f, 1.0f);
        for (const auto& [first, count, material] : _canvasCommands)
        {
            _commands.push_back(DrawCommand{_canvasMesh, first, count, material, 0, screen});
        }

        _canvasVertices.clear();
        _canvasCommands.clear();
    }

    void Renderer::UploadDynamic(SDL_GPUCommandBuffer* commands)
    {
        SDL_GPUCopyPass* copy = nullptr;
        for (auto& [mesh, dynamic] : _dynamic)
        {
            if (dynamic.Pending == 0)
            {
                continue;
            }

            if (copy == nullptr)
            {
                copy = SDL_BeginGPUCopyPass(commands);
            }

            SDL_GPUTransferBufferLocation source{};
            source.transfer_buffer = dynamic.Transfer;
            SDL_GPUBufferRegion destination{};
            destination.buffer = _buffers[mesh];
            destination.size = dynamic.Pending;
            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
            dynamic.Pending = 0;
        }

        if (copy != nullptr)
        {
            SDL_EndGPUCopyPass(copy);
        }
    }

    void Renderer::Draw(int mesh, int firstVertex, int vertexCount, const Material& material, int frame)
    {
        Queue().push_back(DrawCommand{mesh, firstVertex, vertexCount, material, frame, Matrix4x4::Identity(), Render::Primitive::Triangles, Band});
    }

    void Renderer::Draw(int mesh, int firstVertex, int vertexCount, const Material& material, int frame, const Matrix4x4& world)
    {
        Queue().push_back(DrawCommand{mesh, firstVertex, vertexCount, material, frame, world, Render::Primitive::Triangles, Band});
    }

    void Renderer::DrawLines(int mesh, int firstVertex, int vertexCount, const Material& material)
    {
        Queue().push_back(DrawCommand{mesh, firstVertex, vertexCount, material, 0, Matrix4x4::Identity(), Render::Primitive::Lines, Band});
    }

    void Renderer::Present(const View& view, Vector4 clearColour, const std::optional<std::string>& screenshot)
    {
        if (Overlay)
        {
            Overlay();
        }

        FlushUploads();
        auto* commands = SDL_AcquireGPUCommandBuffer(_device);
        SDL_GPUTexture* swapchain = nullptr;
        auto width = static_cast<uint>(HiddenSize.Width);
        auto height = static_cast<uint>(HiddenSize.Height);

        // A hidden window has no swapchain to show (unless tests ask): the frame stays in the offscreen target.
        const auto shown = _window.Visibility() == Platform::Visibility::Shown || HiddenFrames == Render::HiddenFrames::Swapchain;
        const auto waitStart = StopwatchTicks();
        if (shown && (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, _window.Handle(), &swapchain, &width, &height) || swapchain == nullptr))
        {
            SDL_SubmitGPUCommandBuffer(commands);
            _commands.clear();
            _stream.clear();
            ClearInsets();
            return;
        }

        auto wait = ElapsedSince(waitStart);
        // The swapchain's command buffer presents when submitted: on Direct3D 12, where the draws may
        // submit theirs mid-frame (Reserve), they get their own, and the swapchain's only blits at the end.
        auto* presenting = commands;
        if (swapchain != nullptr && HeapBound())
        {
            commands = SDL_AcquireGPUCommandBuffer(_device);
        }

        _presenting = swapchain != nullptr ? presenting : nullptr;
        const auto percent = PercentFor(width, height);
        EnsureTargets(shown ? Scaled(width, percent) : width, shown ? Scaled(height, percent) : height);
        QueueCanvas();
        QueueStream();
        UploadDynamic(commands);
        PrepareColours(commands);
        _drawCalls = 0;
        _triangles = 0;
        _samplersBound = 0;
        PushLights(commands, view);
        _eyeSide = 0.0f;
        if (StereoMode != Stereo::Off && _eyes[0] != nullptr)
        {
            // Each eye's whole frame, then the two into the frame (side by side or interlaced).
            RenderEye(commands, view, clearColour, 0, -1.0f);
            RenderEye(commands, view, clearColour, 1, +1.0f);
            CompositeStereo(commands);
            _frame = _target;
        }
        else
        {
            _frame = _target;
            RenderScene(commands, view, clearColour);
        }

        // The insets belong to the frame, not to one eye: both eyes draw them (their views offset
        // per eye), so they're cleared only once the frame is drawn.
        ClearInsets();
        ApplyGamma(commands);
        if (swapchain != nullptr)
        {
            // The draws first, then the swapchain's blit and present.
            if (commands != presenting)
            {
                SDL_SubmitGPUCommandBuffer(commands);
                commands = presenting;
            }

            Blit(commands, swapchain, width, height);
        }

        if (screenshot.has_value())
        {
            Save(commands, *screenshot);
        }
        else
        {
            wait += Submit(commands, swapchain != nullptr ? Pacing::Swapchain : Pacing::Fence);
        }

        _stats = RenderStats{_drawCalls, _triangles, static_cast<float>(std::chrono::duration<double, std::milli>(wait).count())};
        _commands.clear();
        Band = DepthBand::Front;
    }

    TimeSpan Renderer::Submit(SDL_GPUCommandBuffer* commands, Pacing pacing)
    {
        if (Sync == GpuSync::Async && pacing == Pacing::Swapchain)
        {
            SDL_SubmitGPUCommandBuffer(commands);
            return TimeSpan::zero();
        }

        auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        const auto start = StopwatchTicks();
        if (Sync == GpuSync::Wait)
        {
            WaitFor(fence);
            return ElapsedSince(start);
        }

        WaitFor(_lastFrame);
        _lastFrame = fence;
        return ElapsedSince(start);
    }

    void Renderer::WaitFor(SDL_GPUFence* fence)
    {
        if (fence == nullptr)
        {
            return;
        }

        SDL_WaitForGPUFences(_device, true, &fence, 1);
        SDL_ReleaseGPUFence(_device, fence);
    }

    void Renderer::DrawPrimitives(SDL_GPURenderPass* pass, int count, int first, Render::Primitive primitive)
    {
        _drawCalls++;
        _triangles += primitive == Render::Primitive::Triangles ? count / TriangleVertices : 0;
        SDL_DrawGPUPrimitives(pass, static_cast<uint>(count), 1, static_cast<uint>(first), 0);
    }

    uint Renderer::Scaled(uint side, int percent) const
    {
        return Math::Clamp(side * static_cast<uint>(percent) / FullScale, 1u, MaxTargetSide);
    }

    int Renderer::AutoPercent(uint width, uint height)
    {
        const auto pixels = static_cast<float>(width) * static_cast<float>(height);
        if (pixels <= 0.0f)
        {
            return FullScale;
        }

        return Math::Clamp(ToInt(FullScale * MathF::Sqrt(AutoPixels / pixels)), MinAutoScale, FullScale);
    }

    void Renderer::SetVSync(Render::VSync value)
    {
        const auto mode = PresentModes::Choose(value, [this](Render::VSync m) { return SDL_WindowSupportsGPUPresentMode(_device, _window.Handle(), PresentMode(m)); });
        if (mode == _vsync)
        {
            return;
        }

        if (!SDL_SetGPUSwapchainParameters(_device, _window.Handle(), SDL_GPU_SWAPCHAINCOMPOSITION_SDR, PresentMode(mode)))
        {
            Console::Error().WriteLine(std::format("VSync {}: {}", PresentModes::Name(mode), SDL_GetError()));
            return;
        }

        _vsync = mode;
    }

    SDL_GPUPresentMode Renderer::PresentMode(Render::VSync mode)
    {
        switch (mode)
        {
        case Render::VSync::Off:
            return SDL_GPU_PRESENTMODE_IMMEDIATE;
        case Render::VSync::Adaptive:
            return SDL_GPU_PRESENTMODE_MAILBOX;
        default:
            return SDL_GPU_PRESENTMODE_VSYNC;
        }
    }

    std::string Renderer::FormatName(SDL_GPUTextureFormat format)
    {
        // format.ToString() without its prefix.
        switch (format)
        {
        case SDL_GPU_TEXTUREFORMAT_INVALID:
            return "INVALID";
        case SDL_GPU_TEXTUREFORMAT_A8_UNORM:
            return "A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
            return "R8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:
            return "R8G8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
            return "R8G8B8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16_UNORM:
            return "R16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16_UNORM:
            return "R16G16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM:
            return "R16G16B16A16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:
            return "R10G10B10A2_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM:
            return "B5G6R5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM:
            return "B5G5R5A1_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM:
            return "B4G4R4A4_UNORM";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
            return "B8G8R8A8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM:
            return "BC1_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM:
            return "BC2_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
            return "BC3_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM:
            return "BC4_R_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
            return "BC5_RG_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
            return "BC7_RGBA_UNORM";
        case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_FLOAT:
            return "BC6H_RGB_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT:
            return "BC6H_RGB_UFLOAT";
        case SDL_GPU_TEXTUREFORMAT_R8_SNORM:
            return "R8_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8_SNORM:
            return "R8G8_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
            return "R8G8B8A8_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16_SNORM:
            return "R16_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16_SNORM:
            return "R16G16_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SNORM:
            return "R16G16B16A16_SNORM";
        case SDL_GPU_TEXTUREFORMAT_R16_FLOAT:
            return "R16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT:
            return "R16G16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
            return "R16G16B16A16_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
            return "R32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
            return "R32G32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
            return "R32G32B32A32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT:
            return "R11G11B10_UFLOAT";
        case SDL_GPU_TEXTUREFORMAT_R8_UINT:
            return "R8_UINT";
        case SDL_GPU_TEXTUREFORMAT_R8G8_UINT:
            return "R8G8_UINT";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
            return "R8G8B8A8_UINT";
        case SDL_GPU_TEXTUREFORMAT_R16_UINT:
            return "R16_UINT";
        case SDL_GPU_TEXTUREFORMAT_R16G16_UINT:
            return "R16G16_UINT";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
            return "R16G16B16A16_UINT";
        case SDL_GPU_TEXTUREFORMAT_R32_UINT:
            return "R32_UINT";
        case SDL_GPU_TEXTUREFORMAT_R32G32_UINT:
            return "R32G32_UINT";
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT:
            return "R32G32B32A32_UINT";
        case SDL_GPU_TEXTUREFORMAT_R8_INT:
            return "R8_INT";
        case SDL_GPU_TEXTUREFORMAT_R8G8_INT:
            return "R8G8_INT";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
            return "R8G8B8A8_INT";
        case SDL_GPU_TEXTUREFORMAT_R16_INT:
            return "R16_INT";
        case SDL_GPU_TEXTUREFORMAT_R16G16_INT:
            return "R16G16_INT";
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT:
            return "R16G16B16A16_INT";
        case SDL_GPU_TEXTUREFORMAT_R32_INT:
            return "R32_INT";
        case SDL_GPU_TEXTUREFORMAT_R32G32_INT:
            return "R32G32_INT";
        case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT:
            return "R32G32B32A32_INT";
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
            return "R8G8B8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
            return "B8G8R8A8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB:
            return "BC1_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB:
            return "BC2_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB:
            return "BC3_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
            return "BC7_RGBA_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
            return "D16_UNORM";
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
            return "D24_UNORM";
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
            return "D32_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
            return "D24_UNORM_S8_UINT";
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
            return "D32_FLOAT_S8_UINT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM:
            return "ASTC_4x4_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM:
            return "ASTC_5x4_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM:
            return "ASTC_5x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM:
            return "ASTC_6x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM:
            return "ASTC_6x6_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM:
            return "ASTC_8x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM:
            return "ASTC_8x6_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM:
            return "ASTC_8x8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM:
            return "ASTC_10x5_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM:
            return "ASTC_10x6_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM:
            return "ASTC_10x8_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM:
            return "ASTC_10x10_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM:
            return "ASTC_12x10_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM:
            return "ASTC_12x12_UNORM";
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM_SRGB:
            return "ASTC_4x4_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x4_UNORM_SRGB:
            return "ASTC_5x4_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x5_UNORM_SRGB:
            return "ASTC_5x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x5_UNORM_SRGB:
            return "ASTC_6x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM_SRGB:
            return "ASTC_6x6_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x5_UNORM_SRGB:
            return "ASTC_8x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x6_UNORM_SRGB:
            return "ASTC_8x6_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM_SRGB:
            return "ASTC_8x8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM_SRGB:
            return "ASTC_10x5_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x6_UNORM_SRGB:
            return "ASTC_10x6_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x8_UNORM_SRGB:
            return "ASTC_10x8_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x10_UNORM_SRGB:
            return "ASTC_10x10_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x10_UNORM_SRGB:
            return "ASTC_12x10_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x12_UNORM_SRGB:
            return "ASTC_12x12_UNORM_SRGB";
        case SDL_GPU_TEXTUREFORMAT_ASTC_4x4_FLOAT:
            return "ASTC_4x4_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x4_FLOAT:
            return "ASTC_5x4_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_5x5_FLOAT:
            return "ASTC_5x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x5_FLOAT:
            return "ASTC_6x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_6x6_FLOAT:
            return "ASTC_6x6_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x5_FLOAT:
            return "ASTC_8x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x6_FLOAT:
            return "ASTC_8x6_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_8x8_FLOAT:
            return "ASTC_8x8_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x5_FLOAT:
            return "ASTC_10x5_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x6_FLOAT:
            return "ASTC_10x6_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x8_FLOAT:
            return "ASTC_10x8_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_10x10_FLOAT:
            return "ASTC_10x10_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x10_FLOAT:
            return "ASTC_12x10_FLOAT";
        case SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT:
            return "ASTC_12x12_FLOAT";
        default:
            return std::to_string(static_cast<int>(format));
        }
    }

    float Renderer::AspectRatio() const
    {
        return StereoModes::EyeAspect(StereoMode, _targetHeight == 0 ? 4.0f / 3.0f : static_cast<float>(_targetWidth) / static_cast<float>(_targetHeight));
    }

    void Renderer::RenderScene(SDL_GPUCommandBuffer*& commands, const View& view, Vector4 clearColour)
    {
        if (Lighting.has_value())
        {
            RenderEnhanced(commands, view, clearColour, *Lighting);
            return;
        }

        auto colourTarget = SceneColour(_frame, clearColour);
        auto depthTarget = SceneDepth();
        auto* pass = BeginPass(commands, &colourTarget, &depthTarget);
        _passSamples = _samples;
        if (Panorama.has_value() && Backdrop == Render::Backdrop::Sky)
        {
            DrawSky(commands, pass, view, *Panorama);
        }

        // With insets, the canvas waits for them (Renderer.Insets.cpp).
        const auto canvasNow = _insets.empty();
        for (const auto order : Passes)
        {
            if (order != Render::Pass::Overlay || canvasNow)
            {
                DrawPass(commands, pass, _commands, order, view);
            }
        }

        SDL_EndGPURenderPass(pass);
        _passSamples = 1;
        if (!canvasNow)
        {
            RenderInsets(commands);
        }
    }

    void Renderer::DrawSky(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const View& view, const Render::Panorama& panorama)
    {
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(PipelineKey{"sky", Render::Pass::DoubleSided, Render::Primitive::Triangles, Geometry::Screen, _passSamples, Output::Colour}));
        // The camera's own axes give steady directions (ViewCamera::SkyDirections); a stereo eye's
        // shifted frustum keeps the inverse.
        const auto clipToDirection = view.Camera.has_value() && _eyeSide == 0.0f ? view.Camera->SkyDirections() : view.ClipToDirection;
        SDL_PushGPUVertexUniformData(commands, 0, &clipToDirection, sizeof(Matrix4x4));
        BindPanorama(commands, pass, panorama, panorama.Sky, 0.0f, view.Position);
        DrawPrimitives(pass, ScreenTriangle, 0, Render::Primitive::Triangles);
    }

    void Renderer::BindPanorama(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const Render::Panorama& panorama, int texture, float rowShift, Vector3 camera)
    {
        std::array<SDL_GPUTextureSamplerBinding, 2> samplers{};
        samplers[0] = {_textures[texture], _clampSampler};
        samplers[1] = {_textures[panorama.Palette], _clampSampler};
        SDL_BindGPUFragmentSamplers(pass, 0, samplers.data(), 2);

        PanoramaUniforms uniforms{};
        uniforms.WrapWidth = panorama.WrapWidth;
        uniforms.HorizonRow = panorama.HorizonRow;
        uniforms.Offset = panorama.Offset;
        uniforms.Height = panorama.Height;
        uniforms.TopColour = panorama.TopColour;
        uniforms.BottomColour = panorama.BottomColour;
        uniforms.RowShift = rowShift;
        uniforms.Sampling = panorama.Sampling;
        uniforms.Camera = Vector4(camera, 1.0f);
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(PanoramaUniforms));
    }

    void Renderer::DrawPass(SDL_GPUCommandBuffer*& commands, SDL_GPURenderPass*& pass, const std::vector<DrawCommand>& source, Render::Pass order, const View& view)
    {
        for (const auto& command : source)
        {
            if (command.Material.Pass == order)
            {
                Reserve(commands, pass);
                UseBand(pass, command.Band);
                DrawOne(commands, pass, command, view);
            }
        }
    }

    void Renderer::UseBand(SDL_GPURenderPass* pass, DepthBand band)
    {
        if (_band == band)
        {
            return;
        }

        SDL_GPUViewport viewport{};
        if (_openPass.Clipped)
        {
            viewport = _openPass.Viewport;
        }
        else
        {
            viewport.w = static_cast<float>(_targetWidth);
            viewport.h = static_cast<float>(_targetHeight);
        }

        const auto range = DepthBands::Of(band);
        viewport.min_depth = range.Min;
        viewport.max_depth = range.Max;
        SDL_SetGPUViewport(pass, &viewport);
        _band = band;
    }

    void Renderer::DrawOne(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const DrawCommand& command, const View& view)
    {
        const auto& material = command.Material;
        const auto mode = ModeOf(command);
        const std::string program = material.Pass == Render::Pass::Mirror ? "mirror" : mode.has_value() ? "enhanced" : "palette";
        SDL_BindGPUGraphicsPipeline(pass, Pipeline(PipelineKey{program, material.Pass, command.Primitive, Geometry::Mesh, _passSamples, Output::Colour, material.Blend}));

        SDL_GPUBufferBinding binding{};
        binding.buffer = _buffers[command.Mesh];
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);

        // The canvas is already in clip space; the rest goes through the camera.
        // Layers and lines are drawn nearer than the surfaces they lie on.
        const auto nearer = command.World * DepthPull::Of(view.Position, command.Primitive, material.DepthLayer);
        const auto transform = material.Pass == Render::Pass::Overlay ? command.World : nearer * view.ViewProjection;
        if (mode.has_value())
        {
            DrawEnhanced(commands, pass, command, transform, *mode, view);
            return;
        }

        SDL_PushGPUVertexUniformData(commands, 0, &transform, sizeof(Matrix4x4));

        // Mirrors show nothing without a panorama.
        if (material.Pass == Render::Pass::Mirror)
        {
            if (!Panorama.has_value())
            {
                return;
            }

            BindPanorama(commands, pass, *Panorama, Panorama->MirrorSky, material.RowShift, view.Position);
            DrawPrimitives(pass, command.Count, command.First, command.Primitive);
            return;
        }

        // Flat colours still bind a texture pair: any will do.
        const auto textured = material.Texture != Material::None;
        std::array<SDL_GPUTextureSamplerBinding, 2> samplers{};
        samplers[0] = {_textures[textured ? material.Texture : 0], _repeatSampler};
        samplers[1] = {_textures[textured ? material.Palette : 0], _clampSampler};
        SDL_BindGPUFragmentSamplers(pass, 0, samplers.data(), 2);

        FragmentUniforms uniforms{};
        uniforms.Colour = material.Colour;
        uniforms.Textured = textured ? 1 : 0;
        uniforms.FrameCount = material.FrameCount;
        uniforms.Frame = command.Frame;
        SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(FragmentUniforms));
        DrawPrimitives(pass, command.Count, command.First, command.Primitive);
    }

    void Renderer::Blit(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* swapchain, uint width, uint height)
    {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = _shown;
        blit.source.w = _targetWidth;
        blit.source.h = _targetHeight;
        blit.destination.texture = swapchain;
        blit.destination.w = width;
        blit.destination.h = height;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        // A scaled frame is smoothed; at 100 % pixels copy as they are.
        blit.filter = _targetWidth == width && _targetHeight == height ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(commands, &blit);
    }

    void Renderer::Save(SDL_GPUCommandBuffer* commands, const std::string& path)
    {
        const auto size = _targetWidth * _targetHeight * BytesPerPixel;
        auto* transfer = CreateTransfer(size, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD);
        auto* copy = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureRegion source{};
        source.texture = _shown;
        source.w = _targetWidth;
        source.h = _targetHeight;
        source.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = transfer;
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        SDL_WaitForGPUFences(_device, true, &fence, 1);
        SDL_ReleaseGPUFence(_device, fence);

        const auto* mapped = static_cast<const byte*>(SDL_MapGPUTransferBuffer(_device, transfer, false));
        const std::vector<byte> rgba(mapped, mapped + size);
        SDL_UnmapGPUTransferBuffer(_device, transfer);
        SDL_ReleaseGPUTransferBuffer(_device, transfer);
        Bmp::Write(path, static_cast<int>(_targetWidth), static_cast<int>(_targetHeight), rgba);
    }

    void Renderer::EnsureTargets(uint width, uint height)
    {
        const auto samples = SupportedSamples();
        if (_target != nullptr && width == _targetWidth && height == _targetHeight && samples == _samples
            && (StereoMode != Stereo::Off) == (_eyes[0] != nullptr))
        {
            return;
        }

        if (_target != nullptr)
        {
            SDL_ReleaseGPUTexture(_device, _target);
            SDL_ReleaseGPUTexture(_device, _depth);
        }

        ReleaseSizedTargets();
        _target = CreateTexture(ColourFormat, width, height, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        _depth = CreateTexture(_depthFormat, width, height, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        _targetWidth = width;
        _targetHeight = height;
        _samples = samples;
        CreateMultisampled(width, height);
        EnsureEyes();
    }

    SDL_GPUTexture* Renderer::CreateTexture(SDL_GPUTextureFormat format, uint width, uint height, SDL_GPUTextureUsageFlags usage,
        uint levels, uint samples)
    {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = levels;
        info.sample_count = SampleCount(samples);
        auto* texture = SDL_CreateGPUTexture(_device, &info);
        Check(texture != nullptr, "SDL_CreateGPUTexture");
        return texture;
    }

    SDL_GPUTransferBuffer* Renderer::CreateTransfer(uint size, SDL_GPUTransferBufferUsage usage)
    {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = usage;
        info.size = size;
        return SDL_CreateGPUTransferBuffer(_device, &info);
    }

    std::span<byte> Renderer::MapStaging(int size, SDL_GPUTransferBuffer*& transfer, bool& temporary)
    {
        temporary = size > StagingKeep;
        if (temporary)
        {
            transfer = CreateTransfer(static_cast<uint>(size), SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
            return {static_cast<byte*>(SDL_MapGPUTransferBuffer(_device, transfer, false)), static_cast<size_t>(size)};
        }

        if (_stagingSize < static_cast<uint>(size))
        {
            if (_staging != nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(_device, _staging);
            }

            _stagingSize = Math::Max(static_cast<uint>(size), _stagingSize * 2);
            _staging = CreateTransfer(_stagingSize, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
        }

        transfer = _staging;
        return {static_cast<byte*>(SDL_MapGPUTransferBuffer(_device, transfer, true)), static_cast<size_t>(size)};
    }

    void Renderer::Upload(SDL_GPUTexture* texture, uint width, uint height, std::span<const byte> pixels)
    {
        SDL_GPUTransferBuffer* transfer = nullptr;
        uint offset = 0;
        auto* copy = UploadPass(pixels, transfer, offset);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.offset = offset;
        source.pixels_per_row = width;
        source.rows_per_layer = height;
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = width;
        destination.h = height;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
    }

    SDL_GPUCopyPass* Renderer::UploadPass(std::span<const byte> data, SDL_GPUTransferBuffer*& transfer, uint& offset)
    {
        const auto size = static_cast<uint>(data.size());
        offset = (_uploadUsed + UploadAlignment - 1) / UploadAlignment * UploadAlignment;
        if (size <= UploadBatchSize && offset + size > UploadBatchSize)
        {
            FlushUploads();
            offset = 0;
        }

        if (_uploadCommands == nullptr)
        {
            _uploadCommands = SDL_AcquireGPUCommandBuffer(_device);
            _uploadCopy = SDL_BeginGPUCopyPass(_uploadCommands);
        }

        if (size > UploadBatchSize)
        {
            offset = 0;
            transfer = CreateTransfer(size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
            std::memcpy(SDL_MapGPUTransferBuffer(_device, transfer, false), data.data(), data.size());
            SDL_UnmapGPUTransferBuffer(_device, transfer);
            SDL_ReleaseGPUTransferBuffer(_device, transfer);
            return _uploadCopy;
        }

        if (_uploadBuffer == nullptr)
        {
            _uploadBuffer = CreateTransfer(UploadBatchSize, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
        }

        // A new batch cycles off the buffer the GPU may still read; later uploads add to it.
        transfer = _uploadBuffer;
        auto* mapped = static_cast<byte*>(SDL_MapGPUTransferBuffer(_device, transfer, offset == 0));
        std::memcpy(mapped + offset, data.data(), data.size());
        SDL_UnmapGPUTransferBuffer(_device, transfer);
        _uploadUsed = offset + size;
        return _uploadCopy;
    }

    void Renderer::FlushUploads()
    {
        if (_uploadCommands == nullptr)
        {
            return;
        }

        SDL_EndGPUCopyPass(_uploadCopy);
        auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(_uploadCommands);
        WaitFor(_uploadFence);
        _uploadFence = fence;
        _uploadCommands = nullptr;
        _uploadCopy = nullptr;
        _uploadUsed = 0;
    }

    Renderer::~Renderer()
    {
        FlushUploads();
        WaitFor(_uploadFence);
        WaitFor(_lastFrame);
        SDL_WaitForGPUIdle(_device);
        for (auto* texture : _textures)
        {
            SDL_ReleaseGPUTexture(_device, texture);
        }

        for (auto* buffer : _buffers)
        {
            SDL_ReleaseGPUBuffer(_device, buffer);
        }

        for (const auto& [mesh, dynamic] : _dynamic)
        {
            SDL_ReleaseGPUTransferBuffer(_device, dynamic.Transfer);
        }

        for (const auto& [key, pipeline] : _pipelines)
        {
            SDL_ReleaseGPUGraphicsPipeline(_device, pipeline);
        }

        if (_target != nullptr)
        {
            SDL_ReleaseGPUTexture(_device, _target);
            SDL_ReleaseGPUTexture(_device, _depth);
        }

        ReleaseSizedTargets();
        ReleaseShadowMap();
        if (_staging != nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(_device, _staging);
        }

        if (_uploadBuffer != nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(_device, _uploadBuffer);
        }

        SDL_ReleaseGPUSampler(_device, _repeatSampler);
        SDL_ReleaseGPUSampler(_device, _clampSampler);
        SDL_ReleaseGPUSampler(_device, _mipSampler);
        DisposeColours();
        SDL_ReleaseWindowFromGPUDevice(_device, _window.Handle());
        SDL_DestroyGPUDevice(_device);
    }
}
