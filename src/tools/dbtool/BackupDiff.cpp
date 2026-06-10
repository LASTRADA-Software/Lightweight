// SPDX-License-Identifier: Apache-2.0

#include "BackupDiff.hpp"

#include <Lightweight/SqlBackup/MsgPackChunkFormats.hpp>
#include <Lightweight/SqlBackup/Sha256.hpp>
#include <Lightweight/SqlBackup/SqlBackupFormats.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace Lightweight::Tools
{

using Lightweight::SqlBackup::BackupValue;
using Lightweight::SqlBackup::ColumnBatch;
using Lightweight::SqlBackup::CreateMsgPackChunkReaderFromBuffer;
using Lightweight::SqlBackup::Sha256;

namespace
{

    /// RAII wrapper around a `zip_t*` opened read-only, so the comparison stays exception-safe
    /// and we never leak a libzip handle on an early return.
    class ZipReader
    {
      public:
        explicit ZipReader(std::filesystem::path const& path)
        {
            int err = 0;
            _zip = zip_open(path.string().c_str(), ZIP_RDONLY, &err);
        }

        ZipReader(ZipReader const&) = delete;
        ZipReader& operator=(ZipReader const&) = delete;
        ZipReader(ZipReader&&) = delete;
        ZipReader& operator=(ZipReader&&) = delete;

        ~ZipReader()
        {
            if (_zip)
                zip_close(_zip);
        }

        [[nodiscard]] bool IsOpen() const noexcept
        {
            return _zip != nullptr;
        }

        [[nodiscard]] zip_t* Handle() const noexcept
        {
            return _zip;
        }

      private:
        zip_t* _zip = nullptr;
    };

    /// A single data chunk entry inside a backup archive.
    struct ChunkEntry
    {
        zip_uint64_t index = 0;
        zip_uint64_t size = 0;
    };

    /// Enumerates the data chunks per table from a backup archive.
    ///
    /// Table identity is taken from the `data/<sanitized_table>/NNNN.msgpack` entry-name prefix
    /// rather than from `metadata.json`. The on-disk path is the *sanitized* table name (the
    /// backup writer replaces '.' with '_'); using it directly guarantees both archives are keyed
    /// the same way and sidesteps any schema/sanitization mismatch between the two metadata files.
    ///
    /// @param zip The opened archive.
    /// @return Map of sanitized table name -> its data chunk entries (in archive order).
    std::map<std::string, std::vector<ChunkEntry>> EnumerateTableChunks(zip_t* zip)
    {
        std::map<std::string, std::vector<ChunkEntry>> result;

        zip_int64_t const numEntries = zip_get_num_entries(zip, 0);
        for (zip_int64_t i = 0; i < numEntries; ++i)
        {
            zip_stat_t stat;
            if (zip_stat_index(zip, static_cast<zip_uint64_t>(i), 0, &stat) < 0)
                continue;

            std::string const name = stat.name;
            if (!name.starts_with("data/") || !name.ends_with(".msgpack"))
                continue;

            // Path format: data/<table>/<chunk>.msgpack
            auto const firstSlash = name.find('/');
            auto const secondSlash = name.find('/', firstSlash + 1);
            if (firstSlash == std::string::npos || secondSlash == std::string::npos)
                continue;

            std::string tableName = name.substr(firstSlash + 1, secondSlash - firstSlash - 1);
            result[std::move(tableName)].push_back(ChunkEntry { .index = static_cast<zip_uint64_t>(i), .size = stat.size });
        }

        return result;
    }

    /// Reads a zip entry fully into a byte buffer. Returns an empty optional on any read failure.
    std::optional<std::vector<uint8_t>> ReadEntryBytes(zip_t* zip, ChunkEntry const& entry)
    {
        zip_file_t* file = zip_fopen_index(zip, entry.index, 0);
        if (!file)
            return std::nullopt;

        std::vector<uint8_t> data(entry.size);
        zip_int64_t const bytesRead = zip_fread(file, data.data(), entry.size);
        zip_fclose(file);

        if (bytesRead < 0 || std::cmp_not_equal(bytesRead, entry.size))
            return std::nullopt;

        return data;
    }

    /// Appends a length-prefixed, type-tagged encoding of one cell to `out`.
    ///
    /// Canonical encoding rationale: each cell is written as a single type tag byte followed by a
    /// length-prefixed payload, so two distinct values can never produce the same byte sequence by
    /// concatenation accident (e.g. string "12" then int 3 vs. string "123"). NULL gets its own tag
    /// with no payload. Numeric payloads are fixed-width little-endian so the encoding is stable and
    /// independent of how the value was originally textually formatted.
    void EncodeCell(std::string& out, BackupValue const& value)
    {
        auto appendLength = [&out](uint64_t n) {
            std::array<char, sizeof(uint64_t)> bytes {};
            std::memcpy(bytes.data(), &n, sizeof(n));
            out.append(bytes.data(), bytes.size());
        };
        auto appendRaw = [&out](void const* p, size_t n) {
            out.append(static_cast<char const*>(p), n);
        };

        std::visit(
            [&](auto const& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    out.push_back('\x00'); // NULL tag, no payload
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    out.push_back('\x01');
                    out.push_back(v ? '\x01' : '\x00');
                }
                else if constexpr (std::is_same_v<T, int64_t>)
                {
                    out.push_back('\x02');
                    appendRaw(&v, sizeof(v));
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    out.push_back('\x03');
                    appendRaw(&v, sizeof(v));
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    out.push_back('\x04');
                    appendLength(v.size());
                    appendRaw(v.data(), v.size());
                }
                else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
                {
                    out.push_back('\x05');
                    appendLength(v.size());
                    appendRaw(v.data(), v.size());
                }
            },
            value);
    }

    /// Extracts the cell at (row, column) from a decoded `ColumnBatch` as a `BackupValue`,
    /// honoring the parallel null indicators. The typed column vectors run parallel to the
    /// null indicators (the chunk reader default-constructs a placeholder element for each NULL),
    /// so element `row` of the typed vector aligns with `nullIndicators[col][row]`.
    BackupValue CellAt(ColumnBatch const& batch, size_t col, size_t row)
    {
        if (col < batch.nullIndicators.size() && row < batch.nullIndicators[col].size() && batch.nullIndicators[col][row])
            return std::monostate {};

        return std::visit(
            [row](auto const& vec) -> BackupValue {
                using VecT = std::decay_t<decltype(vec)>;
                if constexpr (std::is_same_v<VecT, std::monostate>)
                    return std::monostate {};
                else
                {
                    if (row >= vec.size())
                        return std::monostate {}; // Defensive: malformed chunk
                    return BackupValue { vec[row] };
                }
            },
            batch.columns[col]);
    }

    /// Row-multiset of a single table, stored compactly as digest -> occurrence count.
    ///
    /// Performance: each row is encoded canonically (see `EncodeCell`) and hashed with SHA-256;
    /// only the 64-hex-char digest and a count are retained. This keeps memory proportional to the
    /// number of *distinct* rows (×~80 bytes) rather than the full row payload, so archives with
    /// millions of wide rows stay tractable. SHA-256 collisions between distinct rows are
    /// astronomically unlikely, so digest equality is treated as row equality.
    using RowMultiset = std::map<std::string, uint64_t>;

    /// Holds the result of folding one table's chunks into a row multiset.
    struct TableRowset
    {
        RowMultiset rows;
        uint64_t rowCount = 0;
        bool ok = true; ///< False if a chunk failed to read/decode.
    };

    /// Reads every chunk of one table from one archive and folds the rows into a multiset of
    /// per-row SHA-256 digests. Chunks are processed and released one at a time so we never hold
    /// the whole table (let alone the whole archive) in memory.
    TableRowset BuildTableRowset(zip_t* zip, std::vector<ChunkEntry> const& chunks)
    {
        TableRowset out;
        std::string rowEncoding;

        for (auto const& chunk: chunks)
        {
            auto bytes = ReadEntryBytes(zip, chunk);
            if (!bytes)
            {
                out.ok = false;
                return out;
            }

            try
            {
                auto reader = CreateMsgPackChunkReaderFromBuffer(*bytes);
                ColumnBatch batch;
                while (reader->ReadBatch(batch))
                {
                    size_t const columnCount = batch.columns.size();
                    for (size_t row = 0; row < batch.rowCount; ++row)
                    {
                        rowEncoding.clear();
                        for (size_t col = 0; col < columnCount; ++col)
                            EncodeCell(rowEncoding, CellAt(batch, col, row));

                        Sha256 hasher;
                        hasher.Update(rowEncoding);
                        ++out.rows[Sha256::ToHex(hasher.Finalize())];
                        ++out.rowCount;
                    }
                }
            }
            catch (std::exception const&)
            {
                out.ok = false;
                return out;
            }
        }

        return out;
    }

    /// Computes how many row-occurrences appear only on one side of two multisets, and collects up
    /// to `maxExamples` digests that are present on the `left` side but missing/short on the right.
    struct MultisetDiff
    {
        uint64_t onlyInLeft = 0;
        uint64_t onlyInRight = 0;
        std::vector<std::string> leftExamples;
        std::vector<std::string> rightExamples;
    };

    MultisetDiff DiffMultisets(RowMultiset const& left, RowMultiset const& right, size_t maxExamples)
    {
        MultisetDiff diff;
        for (auto const& [digest, leftCount]: left)
        {
            auto const it = right.find(digest);
            uint64_t const rightCount = it != right.end() ? it->second : 0;
            if (leftCount > rightCount)
            {
                diff.onlyInLeft += leftCount - rightCount;
                if (diff.leftExamples.size() < maxExamples)
                    diff.leftExamples.push_back(digest);
            }
        }
        for (auto const& [digest, rightCount]: right)
        {
            auto const it = left.find(digest);
            uint64_t const leftCount = it != left.end() ? it->second : 0;
            if (rightCount > leftCount)
            {
                diff.onlyInRight += rightCount - leftCount;
                if (diff.rightExamples.size() < maxExamples)
                    diff.rightExamples.push_back(digest);
            }
        }
        return diff;
    }

    /// Compares one common table's row multiset across both archives and builds the resulting diff
    /// event (Identical / Differing / ReadError). Events are populated by assignment rather than
    /// designated initializers so omitting the kind-specific fields stays valid (and warning-free).
    BackupDiffEvent CompareCommonTable(zip_t* leftZip,
                                       zip_t* rightZip,
                                       std::vector<ChunkEntry> const& leftChunks,
                                       std::vector<ChunkEntry> const& rightChunks,
                                       std::string const& name,
                                       bool ignored,
                                       size_t maxExamples)
    {
        BackupDiffEvent event;
        event.table = name;
        event.ignored = ignored;

        auto const leftRowset = BuildTableRowset(leftZip, leftChunks);
        auto const rightRowset = BuildTableRowset(rightZip, rightChunks);
        if (!leftRowset.ok || !rightRowset.ok)
        {
            event.kind = BackupDiffEvent::Kind::ReadError;
            event.leftReadOk = leftRowset.ok;
            event.rightReadOk = rightRowset.ok;
            return event;
        }

        auto const diff = DiffMultisets(leftRowset.rows, rightRowset.rows, maxExamples);
        event.leftRowCount = leftRowset.rowCount;
        if (diff.onlyInLeft == 0 && diff.onlyInRight == 0)
        {
            event.kind = BackupDiffEvent::Kind::Identical;
            return event;
        }

        event.kind = BackupDiffEvent::Kind::Differing;
        event.rightRowCount = rightRowset.rowCount;
        event.onlyInLeft = diff.onlyInLeft;
        event.onlyInRight = diff.onlyInRight;
        event.leftExamples = diff.leftExamples;
        event.rightExamples = diff.rightExamples;
        return event;
    }

} // namespace

BackupDiffResult BackupDiff(std::filesystem::path const& left,
                            std::filesystem::path const& right,
                            std::set<std::string> const& ignoreTables,
                            BackupDiffObserver* observer)
{
    constexpr size_t MaxExamples = 3;

    auto const emit = [observer](BackupDiffEvent const& event) {
        if (observer)
            observer->OnEvent(event);
    };

    BackupDiffResult result;

    ZipReader leftZip(left);
    ZipReader rightZip(right);
    // Both opens are attempted so the caller can report exactly which archive failed.
    result.leftReadable = leftZip.IsOpen();
    result.rightReadable = rightZip.IsOpen();
    if (!result.leftReadable || !result.rightReadable)
    {
        result.archivesReadable = false;
        result.differenceFound = true;
        return result;
    }

    auto const leftChunks = EnumerateTableChunks(leftZip.Handle());
    auto const rightChunks = EnumerateTableChunks(rightZip.Handle());

    // Tables present in only one archive are themselves a difference.
    std::set<std::string> allTables;
    for (auto const& [name, _]: leftChunks)
        allTables.insert(name);
    for (auto const& [name, _]: rightChunks)
        allTables.insert(name);

    // Count a reported difference as either a hard failure or an (ignored) informational drift.
    auto const recordDifference = [&result](bool ignored) {
        if (ignored)
            ++result.ignoredDifferences;
        else
            result.differenceFound = true;
    };

    for (auto const& name: allTables)
    {
        bool const inLeft = leftChunks.contains(name);
        bool const inRight = rightChunks.contains(name);
        if (inLeft && inRight)
            continue; // Common tables are compared by row multiset below.

        bool const ignored = ignoreTables.contains(name);
        BackupDiffEvent event;
        event.kind = inLeft ? BackupDiffEvent::Kind::OnlyInLeft : BackupDiffEvent::Kind::OnlyInRight;
        event.table = name;
        event.ignored = ignored;
        emit(event);
        recordDifference(ignored);
    }

    // Compare the row multiset of each common table, one table at a time on both sides.
    for (auto const& name: allTables)
    {
        if (!leftChunks.contains(name) || !rightChunks.contains(name))
            continue;

        ++result.comparedTables;
        bool const ignored = ignoreTables.contains(name);
        auto const event = CompareCommonTable(
            leftZip.Handle(), rightZip.Handle(), leftChunks.at(name), rightChunks.at(name), name, ignored, MaxExamples);
        emit(event);

        switch (event.kind)
        {
            case BackupDiffEvent::Kind::Identical:
                ++result.identicalTables;
                break;
            case BackupDiffEvent::Kind::ReadError:
                ++result.differingTables;
                result.differenceFound = true; // A read error is always a failure, never ignorable.
                break;
            case BackupDiffEvent::Kind::Differing:
                ++result.differingTables;
                recordDifference(ignored);
                break;
            case BackupDiffEvent::Kind::OnlyInLeft:
            case BackupDiffEvent::Kind::OnlyInRight:
                break; // Not produced by CompareCommonTable.
        }
    }

    return result;
}

} // namespace Lightweight::Tools
