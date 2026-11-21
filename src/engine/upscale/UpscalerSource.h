#pragma once

#include <string>

namespace Mdk::Engine::Upscale
{
    // Where the upscaler's release archive comes from (an https URL, a file URL or a local
    // path) and its SHA-256 (lower-case hex). The defaults: Real-ESRGAN v0.2.5.0 (2022-04-24 build) for
    // this platform; a user's values (settings.cfg) replace them when the source moves.
    struct UpscalerSource
    {
        std::string Url;
        std::string Sha256;

        bool operator==(const UpscalerSource&) const = default;

        static const UpscalerSource& Default();

        // A user's values over the defaults (empty: the default). A URL of the user's without a
        // checksum keeps it empty: the install then refuses it.
        static UpscalerSource Of(const std::string& url, const std::string& sha256);
    };
}
