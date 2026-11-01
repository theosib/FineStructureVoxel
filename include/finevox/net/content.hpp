#pragma once

/**
 * @file content.hpp
 * @brief The files a client needs, and getting them there
 *
 * Content is a directory of files, named by hash. A client compares the server's list with what it
 * has, fetches the rest, and ends up with exactly the server's files, wherever it got them.
 */

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox::net {

using Sha256 = std::array<uint8_t, 32>;

[[nodiscard]] Sha256 sha256(std::span<const uint8_t> data);
[[nodiscard]] std::optional<Sha256> sha256File(const std::filesystem::path& path);
[[nodiscard]] std::string toHex(const Sha256& hash);
[[nodiscard]] std::optional<Sha256> fromHex(std::string_view text);

struct ContentFile {
    std::string path;  ///< Relative, with `/`
    uint64_t size = 0;
    Sha256 hash{};
};

/// What goes to a client unless the game says otherwise
[[nodiscard]] std::vector<std::string> defaultClientContent();

/// List and hash `entries` under `root`, each a file or a directory to walk. Sorted by path, so that
/// the same files always give the same list.
[[nodiscard]] std::vector<ContentFile> buildManifest(const std::filesystem::path& root,
                                                     const std::vector<std::string>& entries);

/// False for anything that could reach outside the content root: an absolute path, an empty
/// component, `.`, `..`, or a backslash. The path came from a server.
[[nodiscard]] bool isSafeContentPath(std::string_view path);

/// `~/.cache/finevox`, or wherever $XDG_CACHE_HOME says
[[nodiscard]] std::filesystem::path defaultCacheDir();

/**
 * @brief A client's side of getting content
 *
 * Give it the manifest, then what it asks for. When nothing's missing, root() is a directory holding
 * exactly the manifest. That's the game root if it already held all of it, so that the usual case
 * of two players with the same game touches nothing.
 */
class ContentSync {
public:
    ContentSync(std::filesystem::path gameRoot, std::filesystem::path cacheDir);

    /// False if the manifest has a path that isn't safe, which means a server to walk away from
    [[nodiscard]] bool begin(std::vector<ContentFile> files);

    /// Hashes of what's still needed
    [[nodiscard]] std::vector<Sha256> missing() const;

    /// Take a piece of a file. Pieces of one file come in order. False on a hash that doesn't check
    /// out, or a file that isn't on the list: the server is broken or lying.
    [[nodiscard]] bool receive(const Sha256& hash, uint64_t offset, uint64_t total, std::span<const uint8_t> data);

    [[nodiscard]] bool done() const { return remaining_ == 0; }
    [[nodiscard]] uint64_t bytesExpected() const { return bytesExpected_; }
    [[nodiscard]] uint64_t bytesReceived() const { return bytesReceived_; }

    /// Build the directory the client runs from. Call once done().
    [[nodiscard]] std::filesystem::path finish();

private:
    struct Incoming {
        std::filesystem::path partial;
        uint64_t received = 0;
    };

    [[nodiscard]] std::filesystem::path cached(const Sha256& hash) const;
    [[nodiscard]] bool storeWhole(const Sha256& hash, const std::filesystem::path& partial);

    std::filesystem::path gameRoot_;
    std::filesystem::path cacheDir_;
    std::vector<ContentFile> files_;
    std::unordered_map<std::string, Incoming> incoming_;
    std::vector<Sha256> missing_;
    size_t remaining_ = 0;
    uint64_t bytesExpected_ = 0;
    uint64_t bytesReceived_ = 0;
    bool usesCache_ = false;
};

}  // namespace finevox::net
