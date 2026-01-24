// SPDX-License-Identifier: Apache-2.0

#include "../Utils.hpp"
#include "DataGenerator.hpp"
#include "Entities.hpp"

#include <Lightweight/Lightweight.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Lightweight;
using namespace LargeDb;

// NOLINTBEGIN(bugprone-unchecked-optional-access)

TEST_CASE_METHOD(SqlTestFixture, "LargeDb: Schema creation", "[large-db]")
{
    auto dm = DataMapper();

    CreateSchema(dm);

    // Verify all tables were created by querying each one
    SECTION("User table exists")
    {
        auto count = dm.Query<LargeDb_User>().Count();
        CHECK(count == 0);
    }

    SECTION("Category table exists")
    {
        auto count = dm.Query<LargeDb_Category>().Count();
        CHECK(count == 0);
    }

    SECTION("Product table exists")
    {
        auto count = dm.Query<LargeDb_Product>().Count();
        CHECK(count == 0);
    }

    SECTION("ProductImage table exists")
    {
        auto count = dm.Query<LargeDb_ProductImage>().Count();
        CHECK(count == 0);
    }

    SECTION("Order table exists")
    {
        auto count = dm.Query<LargeDb_Order>().Count();
        CHECK(count == 0);
    }

    SECTION("OrderItem table exists")
    {
        auto count = dm.Query<LargeDb_OrderItem>().Count();
        CHECK(count == 0);
    }

    SECTION("Review table exists")
    {
        auto count = dm.Query<LargeDb_Review>().Count();
        CHECK(count == 0);
    }

    SECTION("Tag table exists")
    {
        auto count = dm.Query<LargeDb_Tag>().Count();
        CHECK(count == 0);
    }

    SECTION("ProductTag table exists")
    {
        auto count = dm.Query<LargeDb_ProductTag>().Count();
        CHECK(count == 0);
    }

    SECTION("ActivityLog table exists")
    {
        auto count = dm.Query<LargeDb_ActivityLog>().Count();
        CHECK(count == 0);
    }

    SECTION("SystemAuditLog table exists")
    {
        auto count = dm.Query<LargeDb_SystemAuditLog>().Count();
        CHECK(count == 0);
    }

    SECTION("Article table exists")
    {
        auto count = dm.Query<LargeDb_Article>().Count();
        CHECK(count == 0);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "LargeDb: Small scale population and queries", "[large-db]")
{
    auto dm = DataMapper();

    CreateSchema(dm);

    // Use a very small scale config for quick testing
    auto config = CreateScaledConfig(0.01); // 1% of full size
    config.userCount = 10;
    config.categoryCount = 5;
    config.productCount = 10;
    config.productImageCount = 20;
    config.orderCount = 20;
    config.orderItemCount = 40;
    config.reviewCount = 15;
    config.tagCount = 5;
    config.productTagCount = 20;
    config.activityLogCount = 50;
    config.systemAuditLogCount = 10;
    config.articleCount = 5;

    // Also reduce field sizes for faster generation
    config.userBioSize = 50;
    config.userAvatarSize = 100;
    config.categoryDescriptionSize = 100;
    config.productLongDescriptionSize = 200;
    config.productSpecsSize = 100;
    config.productImageSize = 500;
    config.productThumbnailSize = 100;
    config.reviewContentSize = 100;
    config.activityLogJsonSize = 100;
    config.systemAuditContextSize = 200;
    config.systemAuditStackTraceSize = 100;
    config.articleContentSize = 300;
    config.articleFeaturedImageSize = 200;

    PopulateDatabase(dm, config);

    SECTION("Verify row counts")
    {
        CHECK(dm.Query<LargeDb_User>().Count() == config.userCount);
        CHECK(dm.Query<LargeDb_Category>().Count() == config.categoryCount);
        CHECK(dm.Query<LargeDb_Product>().Count() == config.productCount);
        CHECK(dm.Query<LargeDb_ProductImage>().Count() == config.productImageCount);
        CHECK(dm.Query<LargeDb_Order>().Count() == config.orderCount);
        CHECK(dm.Query<LargeDb_OrderItem>().Count() == config.orderItemCount);
        CHECK(dm.Query<LargeDb_Review>().Count() == config.reviewCount);
        CHECK(dm.Query<LargeDb_Tag>().Count() == config.tagCount);
        CHECK(dm.Query<LargeDb_ProductTag>().Count() == config.productTagCount);
        CHECK(dm.Query<LargeDb_ActivityLog>().Count() == config.activityLogCount);
        CHECK(dm.Query<LargeDb_SystemAuditLog>().Count() == config.systemAuditLogCount);
        CHECK(dm.Query<LargeDb_Article>().Count() == config.articleCount);
    }

    SECTION("Query users with conditions")
    {
        auto activeUsers = dm.Query<LargeDb_User>().Where(FieldNameOf<Member(LargeDb_User::is_active)>, "=", true).All();
        CHECK(!activeUsers.empty());
    }

    SECTION("Query products with ordering")
    {
        auto products = dm.Query<LargeDb_Product>().OrderBy(FieldNameOf<Member(LargeDb_Product::name)>).All();
        CHECK(products.size() == config.productCount);
    }

    SECTION("Query orders with pagination")
    {
        auto pagedOrders = dm.Query<LargeDb_Order>().OrderBy(FieldNameOf<Member(LargeDb_Order::id)>).Range(0, 5);
        CHECK(pagedOrders.size() == 5);
    }

    SECTION("Query reviews by rating")
    {
        auto highRatedReviews = dm.Query<LargeDb_Review>().Where(FieldNameOf<Member(LargeDb_Review::rating)>, ">=", 4).All();
        // Should have some high-rated reviews (random distribution)
        INFO("High-rated reviews: " << highRatedReviews.size());
    }

    SECTION("Query activity logs by action type")
    {
        auto loginLogs =
            dm.Query<LargeDb_ActivityLog>().Where(FieldNameOf<Member(LargeDb_ActivityLog::action_type)>, "=", "login").All();
        INFO("Login activity logs: " << loginLogs.size());
    }

    SECTION("First and Exist queries")
    {
        auto firstUser = dm.Query<LargeDb_User>().First();
        REQUIRE(firstUser.has_value());
        CHECK(!firstUser->email.Value().empty());

        auto exists = dm.Query<LargeDb_Product>().Where(FieldNameOf<Member(LargeDb_Product::is_active)>, "=", true).Exist();
        CHECK(exists);
    }
}

TEST_CASE_METHOD(SqlTestFixture, "LargeDb: Deterministic generation", "[large-db]")
{
    auto dm = DataMapper();

    CreateSchema(dm);

    // Very small config for reproducibility test
    auto config = GeneratorConfig {};
    config.seed = 12345;
    config.userCount = 5;
    config.categoryCount = 3;
    config.productCount = 5;
    config.productImageCount = 0; // Skip images to make test faster
    config.orderCount = 5;
    config.orderItemCount = 10;
    config.reviewCount = 5;
    config.tagCount = 3;
    config.productTagCount = 5;
    config.activityLogCount = 10;
    config.systemAuditLogCount = 5;
    config.articleCount = 3;

    // Reduce field sizes
    config.userBioSize = 20;
    config.userAvatarSize = 0;
    config.categoryDescriptionSize = 20;
    config.productLongDescriptionSize = 50;
    config.productSpecsSize = 30;
    config.reviewContentSize = 30;
    config.activityLogJsonSize = 30;
    config.systemAuditContextSize = 50;
    config.systemAuditStackTraceSize = 30;
    config.articleContentSize = 50;
    config.articleFeaturedImageSize = 0;

    PopulateDatabase(dm, config);

    // Query first user and verify deterministic generation
    auto firstUser = dm.Query<LargeDb_User>().OrderBy(FieldNameOf<Member(LargeDb_User::id)>).First();

    REQUIRE(firstUser.has_value());

    // With seed 12345, the first user should always have the same email pattern
    INFO("First user email: " << firstUser->email.Value().c_str());
    CHECK(firstUser->email.Value().c_str()[0] != '\0'); // Non-empty

    // Drop and recreate to verify reproducibility
    DropSchema(dm);
    CreateSchema(dm);

    PopulateDatabase(dm, config);

    auto firstUserAgain = dm.Query<LargeDb_User>().OrderBy(FieldNameOf<Member(LargeDb_User::id)>).First();

    REQUIRE(firstUserAgain.has_value());

    // Same seed should produce same data
    CHECK(firstUser->first_name.Value() == firstUserAgain->first_name.Value());
    CHECK(firstUser->last_name.Value() == firstUserAgain->last_name.Value());
}

TEST_CASE_METHOD(SqlTestFixture, "LargeDb: Expected data size calculation", "[large-db]")
{
    auto config = GeneratorConfig {}; // Full default config

    auto expectedSize = GetExpectedDataSize(config);

    // The expected size for the full config should be around 500MB
    INFO("Expected data size: " << expectedSize << " bytes (" << (expectedSize / (1024 * 1024)) << " MB)");

    // Should be at least 400MB
    CHECK(expectedSize >= 400 * 1024 * 1024);

    // Should be roughly around 500MB (with some tolerance)
    CHECK(expectedSize >= 450 * 1024 * 1024);
    CHECK(expectedSize <= 600 * 1024 * 1024);
}

TEST_CASE_METHOD(SqlTestFixture, "LargeDb: Scaled config generation", "[large-db]")
{
    auto fullConfig = GeneratorConfig {};
    auto scaledConfig = CreateScaledConfig(0.1); // 10% scale

    // Verify scaling worked correctly
    CHECK(scaledConfig.userCount == fullConfig.userCount / 10);
    CHECK(scaledConfig.productCount == fullConfig.productCount / 10);
    CHECK(scaledConfig.orderCount == fullConfig.orderCount / 10);

    // Field sizes should remain the same
    CHECK(scaledConfig.userBioSize == fullConfig.userBioSize);
    CHECK(scaledConfig.productImageSize == fullConfig.productImageSize);
}

// NOLINTEND(bugprone-unchecked-optional-access)
