// SPDX-License-Identifier: Apache-2.0
//
// Plain-file secret backend modelled on PostgreSQL's `.pgpass`: one secret
// per line, colon-separated `profileName:uid:password`. The backend enforces
// mode 0600 on load (POSIX only; Windows ACLs are not checked) to avoid the
// "I committed my password file to git" failure mode.
//
// The on-disk format is intentionally line-oriented so ops folks can cat,
// edit, and diff the file without tools. Unknown lines and comment lines
// (`# ...`) are skipped silently.

#pragma once

#include "../ISecretBackend.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Lightweight::Secrets
{

class LIGHTWEIGHT_API FileBackend final: public ISecretBackend
{
  public:
    /// Constructs a backend backed by `path`. The file is NOT read until the
    /// first `Read`/`Write` call. A missing file is treated as an empty
    /// credential store.
    explicit FileBackend(std::filesystem::path path);

    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "file";
    }

    /// Returns the password for the given profile key. The key is interpreted
    /// as the `profileName` column; `uid` is not used for lookup but is
    /// preserved on writes.
    [[nodiscard]] std::optional<std::string> Read(std::string_view key) override;

    /// Inserts or updates the secret for `key`. Rewrites the file atomically
    /// (write-then-rename) with mode 0600 on POSIX. Throws if the parent
    /// directory is not writable.
    void Write(std::string_view key, std::string_view value) override;

    /// Removes the entry for `key`, if present. Rewrites the file atomically.
    void Erase(std::string_view key) override;

    /// Returns the backing file path, for diagnostics.
    [[nodiscard]] std::filesystem::path const& Path() const noexcept
    {
        return _path;
    }

  private:
    std::filesystem::path _path;
};

} // namespace Lightweight::Secrets
