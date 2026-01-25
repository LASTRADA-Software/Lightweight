// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Entities.hpp"

#include <Lightweight/DataMapper/DataMapper.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

/// @file DataGenerator.hpp
/// @brief Data generation utilities for creating a 500MB+ test database.
///
/// All generation functions are deterministic when using the same seed,
/// allowing reproducible test runs across all supported database backends.

namespace LargeDb
{

/// @brief Configuration for database generation.
struct GeneratorConfig
{
    uint64_t seed = 42; ///< Random seed for deterministic generation

    // Entity counts
    size_t userCount = 2000;
    size_t categoryCount = 200;
    size_t productCount = 2000;
    size_t productImageCount = 4000; ///< ~2 images per product
    size_t orderCount = 10000;
    size_t orderItemCount = 30000; ///< ~3 items per order
    size_t reviewCount = 8000;
    size_t tagCount = 500;
    size_t productTagCount = 6000; ///< ~3 tags per product
    size_t activityLogCount = 50000;
    size_t systemAuditLogCount = 5000;
    size_t articleCount = 500;

    // Field size targets (in bytes)
    size_t userBioSize = 500;
    size_t userAvatarSize = 10240;
    size_t categoryDescriptionSize = 2048;
    size_t productLongDescriptionSize = 8192;
    size_t productSpecsSize = 2048;
    size_t productImageSize = 51200;
    size_t productThumbnailSize = 5120;
    size_t reviewContentSize = 2048;
    size_t activityLogJsonSize = 1024;
    size_t systemAuditContextSize = 5120;
    size_t systemAuditStackTraceSize = 3072;
    size_t articleContentSize = 15360;
    size_t articleFeaturedImageSize = 20480;
};

/// @brief Creates a scaled-down configuration for faster testing.
/// @param scaleFactor Factor to scale down counts (e.g., 0.1 for 10% of full size)
/// @return Scaled configuration
GeneratorConfig CreateScaledConfig(double scaleFactor);

/// @brief Seeded random number generator wrapper for deterministic generation.
class SeededRandom
{
  public:
    explicit SeededRandom(uint64_t seed);

    /// @brief Generates a random integer in range [min, max].
    int64_t NextInt(int64_t min, int64_t max);

    /// @brief Generates a random double in range [min, max).
    double NextDouble(double min, double max);

    /// @brief Picks a random element from a vector.
    template <typename T>
    T const& Pick(std::vector<T> const& items)
    {
        return items[static_cast<size_t>(NextInt(0, static_cast<int64_t>(items.size()) - 1))];
    }

    /// @brief Generates a random boolean with given probability of true.
    bool NextBool(double probabilityTrue = 0.5);

    /// @brief Generates random text of approximately the given size.
    std::string GenerateText(size_t targetSize);

    /// @brief Generates pseudo-random binary data of the given size.
    std::vector<uint8_t> GenerateBinaryData(size_t targetSize);

    /// @brief Generates a pseudo-random JSON object of approximately the given size.
    std::string GenerateJson(size_t targetSize);

    /// @brief Generates a random email address.
    std::string GenerateEmail(int64_t userId);

    /// @brief Generates a random product name.
    std::string GenerateProductName(int64_t productId);

    /// @brief Generates random address JSON.
    std::string GenerateAddressJson();

  private:
    std::mt19937_64 m_generator;
};

/// @brief Creates all tables for the large test database schema.
/// @param dm DataMapper instance connected to the target database.
void CreateSchema(Light::DataMapper& dm);

/// @brief Drops all tables from the large test database schema.
/// @param dm DataMapper instance connected to the target database.
void DropSchema(Light::DataMapper& dm);

/// @brief Populates the database with generated data according to config.
/// @param dm DataMapper instance connected to the target database.
/// @param config Generation configuration.
/// @param progressCallback Optional callback for progress reporting (0.0 to 1.0).
void PopulateDatabase(Light::DataMapper& dm,
                      GeneratorConfig const& config = {},
                      std::function<void(double, std::string_view)> progressCallback = {});

/// @brief Gets the expected total size of generated data based on config.
/// @param config Generation configuration.
/// @return Expected raw data size in bytes.
size_t GetExpectedDataSize(GeneratorConfig const& config);

/// @brief Batch insert helper for efficient data loading.
template <typename Entity, typename Generator>
size_t BatchInsert(Light::DataMapper& dm, size_t count, Generator&& generator, size_t batchSize = 1000);

} // namespace LargeDb
