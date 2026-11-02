#include "finevox/net/content.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace finevox::net {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t ROUND[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

class Hasher {
public:
    void update(const uint8_t* data, size_t size) {
        total_ += size;
        while (size > 0) {
            size_t take = std::min(size, sizeof(block_) - used_);
            std::memcpy(block_ + used_, data, take);
            used_ += take;
            data += take;
            size -= take;
            if (used_ == sizeof(block_)) {
                compress();
                used_ = 0;
            }
        }
    }

    Sha256 finish() {
        uint64_t bits = total_ * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (used_ != 56) update(&zero, 1);
        uint8_t length[8];
        for (int i = 0; i < 8; ++i) length[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
        update(length, 8);

        Sha256 out;
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>(state_[i] >> (24 - j * 8));
        }
        return out;
    }

private:
    void compress() {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block_[i * 4]) << 24) | (uint32_t(block_[i * 4 + 1]) << 16) |
                   (uint32_t(block_[i * 4 + 2]) << 8) | uint32_t(block_[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t choose = (e & f) ^ (~e & g);
            uint32_t t1 = h + s1 + choose + ROUND[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

}  // namespace

Sha256 sha256(std::span<const uint8_t> data) {
    Hasher hasher;
    hasher.update(data.data(), data.size());
    return hasher.finish();
}

std::optional<Sha256> sha256File(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    Hasher hasher;
    std::vector<char> buffer(64 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hasher.update(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(in.gcount()));
    }
    return hasher.finish();
}

std::string toHex(const Sha256& hash) {
    constexpr char DIGITS[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t byte : hash) {
        out.push_back(DIGITS[byte >> 4]);
        out.push_back(DIGITS[byte & 0xF]);
    }
    return out;
}

std::optional<Sha256> fromHex(std::string_view text) {
    Sha256 out{};
    if (text.size() != out.size() * 2) return std::nullopt;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < out.size(); ++i) {
        int high = digit(text[i * 2]), low = digit(text[i * 2 + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        out[i] = static_cast<uint8_t>(high << 4 | low);
    }
    return out;
}

std::vector<std::string> defaultClientContent() {
    return {"game.conf", "sky.conf", "blocks", "shapes",    "entities", "models", "textures", "lang",
            "input",     "ui",       "fluids", "particles", "emitters", "sounds", "blueprints"};
}

std::vector<ContentFile> buildManifest(const fs::path& root, const std::vector<std::string>& entries) {
    std::vector<ContentFile> files;

    auto add = [&](const fs::path& path) {
        auto hash = sha256File(path);
        if (!hash) return;
        ContentFile file;
        file.path = path.lexically_relative(root).generic_string();
        file.size = static_cast<uint64_t>(fs::file_size(path));
        file.hash = *hash;
        files.push_back(std::move(file));
    };

    for (const std::string& entry : entries) {
        fs::path path = root / entry;
        std::error_code error;
        if (fs::is_regular_file(path, error)) {
            add(path);
        } else if (fs::is_directory(path, error)) {
            for (const auto& item : fs::recursive_directory_iterator(path, error)) {
                if (!item.is_regular_file()) continue;
                // macOS leaves these wherever Finder has been
                if (item.path().filename() == ".DS_Store") continue;
                add(item.path());
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const ContentFile& a, const ContentFile& b) { return a.path < b.path; });
    return files;
}

bool isSafeContentPath(std::string_view path) {
    if (path.empty() || path.front() == '/') return false;
    if (path.find('\\') != std::string_view::npos || path.find('\0') != std::string_view::npos) return false;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

fs::path defaultCacheDir() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        return fs::path(local) / "finevox" / "cache";
    }
#endif
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return fs::path(xdg) / "finevox";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".cache" / "finevox";
    return fs::temp_directory_path() / "finevox";
}

ContentSync::ContentSync(fs::path gameRoot, fs::path cacheDir)
    : gameRoot_(std::move(gameRoot)), cacheDir_(std::move(cacheDir)) {}

fs::path ContentSync::cached(const Sha256& hash) const {
    return cacheDir_ / "content" / toHex(hash);
}

bool ContentSync::begin(std::vector<ContentFile> files) {
    for (const ContentFile& file : files) {
        if (!isSafeContentPath(file.path)) return false;
    }
    files_ = std::move(files);
    missing_.clear();
    incoming_.clear();
    usesCache_ = false;
    bytesExpected_ = bytesReceived_ = 0;

    std::vector<std::string> asked;
    for (const ContentFile& file : files_) {
        // Cheap test first: hashing the whole game takes a moment, and sizes rarely lie
        std::error_code error;
        fs::path local = gameRoot_ / file.path;
        if (!gameRoot_.empty() && fs::is_regular_file(local, error) && fs::file_size(local, error) == file.size) {
            auto hash = sha256File(local);
            if (hash && *hash == file.hash) continue;
        }

        usesCache_ = true;
        fs::path stored = cached(file.hash);
        if (fs::is_regular_file(stored, error) && fs::file_size(stored, error) == file.size) {
            auto hash = sha256File(stored);
            if (hash && *hash == file.hash) continue;
            fs::remove(stored, error);  // Damaged, so it can't be trusted
        }

        std::string hex = toHex(file.hash);
        if (std::find(asked.begin(), asked.end(), hex) == asked.end()) {
            asked.push_back(std::move(hex));
            missing_.push_back(file.hash);
            bytesExpected_ += file.size;
        }
    }
    remaining_ = missing_.size();

    // A game that has no files of the server's own needs no cache, and an empty file needs no chunks
    for (auto it = missing_.begin(); it != missing_.end();) {
        auto file = std::find_if(files_.begin(), files_.end(), [&](const ContentFile& f) { return f.hash == *it; });
        if (file->size != 0) {
            ++it;
            continue;
        }
        std::error_code error;
        fs::create_directories(cacheDir_ / "content", error);
        std::ofstream(cached(*it), std::ios::binary).close();
        --remaining_;
        it = missing_.erase(it);
    }
    return true;
}

std::vector<Sha256> ContentSync::missing() const {
    return missing_;
}

bool ContentSync::receive(const Sha256& hash, uint64_t offset, uint64_t total, std::span<const uint8_t> data) {
    auto wanted = std::find(missing_.begin(), missing_.end(), hash);
    if (wanted == missing_.end()) return false;
    auto file = std::find_if(files_.begin(), files_.end(), [&](const ContentFile& f) { return f.hash == hash; });
    if (file == files_.end() || file->size != total) return false;

    std::string hex = toHex(hash);
    Incoming& incoming = incoming_[hex];
    if (incoming.partial.empty()) {
        std::error_code error;
        fs::create_directories(cacheDir_ / "content", error);
        incoming.partial = cacheDir_ / "content" / (hex + "." + std::to_string(getpid()) + ".part");
        fs::remove(incoming.partial, error);
    }
    if (offset != incoming.received || offset + data.size() > total) return false;

    {
        std::ofstream out(incoming.partial, std::ios::binary | std::ios::app);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
    incoming.received += data.size();
    bytesReceived_ += data.size();

    if (incoming.received < total) return true;
    if (!storeWhole(hash, incoming.partial)) return false;
    incoming_.erase(hex);
    missing_.erase(wanted);
    --remaining_;
    return true;
}

bool ContentSync::storeWhole(const Sha256& hash, const fs::path& partial) {
    std::error_code error;
    auto actual = sha256File(partial);
    if (!actual || *actual != hash) {
        fs::remove(partial, error);
        return false;
    }
    fs::rename(partial, cached(hash), error);
    return !error;
}

fs::path ContentSync::finish() {
    if (!usesCache_) return gameRoot_;

    // Named for what's in it, so the same game is the same directory next time
    std::string names;
    for (const ContentFile& file : files_) names += file.path + toHex(file.hash);
    Sha256 identity = sha256(std::span(reinterpret_cast<const uint8_t*>(names.data()), names.size()));
    fs::path root = cacheDir_ / "sessions" / toHex(identity).substr(0, 24);

    std::error_code error;
    fs::remove_all(root, error);
    for (const ContentFile& file : files_) {
        fs::path target = root / file.path;
        fs::create_directories(target.parent_path(), error);

        fs::path source = cached(file.hash);
        if (!fs::is_regular_file(source, error)) source = fs::absolute(gameRoot_ / file.path, error);
        fs::create_symlink(source, target, error);
        if (error) fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
    }
    return root;
}

}  // namespace finevox::net
