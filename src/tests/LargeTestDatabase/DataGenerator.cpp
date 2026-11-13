// SPDX-License-Identifier: Apache-2.0

#include "DataGenerator.hpp"

#include <Lightweight/SqlStatement.hpp>
#include <Lightweight/SqlTransaction.hpp>

#include <array>
#include <chrono>
#include <format>
#include <print>

using namespace Lightweight;

namespace LargeDb
{

namespace
{

    // Word lists for generating realistic-looking text
    constexpr std::array<std::string_view, 100> CommonWords = {
        "the",   "be",    "to",      "of",    "and",   "a",     "in",   "that", "have",   "I",     "it",    "for",   "not",
        "on",    "with",  "he",      "as",    "you",   "do",    "at",   "this", "but",    "his",   "by",    "from",  "they",
        "we",    "say",   "her",     "she",   "or",    "an",    "will", "my",   "one",    "all",   "would", "there", "their",
        "what",  "so",    "up",      "out",   "if",    "about", "who",  "get",  "which",  "go",    "me",    "when",  "make",
        "can",   "like",  "time",    "no",    "just",  "him",   "know", "take", "people", "into",  "year",  "your",  "good",
        "some",  "could", "them",    "see",   "other", "than",  "then", "now",  "look",   "only",  "come",  "its",   "over",
        "think", "also",  "back",    "after", "use",   "two",   "how",  "our",  "work",   "first", "well",  "way",   "even",
        "new",   "want",  "because", "any",   "these", "give",  "day",  "most", "us",
    };

    constexpr std::array<std::string_view, 50> FirstNames = {
        "James",   "Mary",    "John",    "Patricia", "Robert",  "Jennifer", "Michael", "Linda",    "William", "Elizabeth",
        "David",   "Barbara", "Richard", "Susan",    "Joseph",  "Jessica",  "Thomas",  "Sarah",    "Charles", "Karen",
        "Chris",   "Nancy",   "Daniel",  "Lisa",     "Matthew", "Betty",    "Anthony", "Margaret", "Mark",    "Sandra",
        "Donald",  "Ashley",  "Steven",  "Kimberly", "Paul",    "Emily",    "Andrew",  "Donna",    "Joshua",  "Michelle",
        "Kenneth", "Dorothy", "Kevin",   "Carol",    "Brian",   "Amanda",   "George",  "Melissa",  "Edward",  "Deborah",
    };

    constexpr std::array<std::string_view, 50> LastNames = {
        "Smith",     "Johnson", "Williams", "Brown",  "Jones",    "Garcia",  "Miller",   "Davis",    "Rodriguez", "Martinez",
        "Hernandez", "Lopez",   "Gonzalez", "Wilson", "Anderson", "Thomas",  "Taylor",   "Moore",    "Jackson",   "Martin",
        "Lee",       "Perez",   "Thompson", "White",  "Harris",   "Sanchez", "Clark",    "Ramirez",  "Lewis",     "Robinson",
        "Walker",    "Young",   "Allen",    "King",   "Wright",   "Scott",   "Torres",   "Nguyen",   "Hill",      "Flores",
        "Green",     "Adams",   "Nelson",   "Baker",  "Hall",     "Rivera",  "Campbell", "Mitchell", "Carter",    "Roberts",
    };

    constexpr std::array<std::string_view, 30> ProductAdjectives = {
        "Premium",   "Deluxe",    "Professional", "Ultimate", "Essential",    "Advanced", "Classic",     "Modern",
        "Compact",   "Portable",  "Wireless",     "Smart",    "Eco-Friendly", "Durable",  "Lightweight", "Heavy-Duty",
        "Ergonomic", "Versatile", "Innovative",   "Reliable", "Stylish",      "Sleek",    "Powerful",    "Efficient",
        "Quiet",     "Fast",      "Secure",       "Enhanced", "Custom",       "Original",
    };

    constexpr std::array<std::string_view, 30> ProductNouns = {
        "Widget",    "Gadget",    "Device",   "Tool",      "System",  "Kit",       "Set",        "Package",
        "Solution",  "Component", "Module",   "Unit",      "Station", "Hub",       "Controller", "Adapter",
        "Connector", "Interface", "Platform", "Framework", "Engine",  "Processor", "Sensor",     "Monitor",
        "Display",   "Speaker",   "Camera",   "Scanner",   "Printer", "Reader",
    };

    constexpr std::array<std::string_view, 20> CategoryNames = {
        "Electronics",    "Home & Garden",    "Sports & Outdoors", "Books & Media",    "Clothing",
        "Toys & Games",   "Health & Beauty",  "Automotive",        "Office Supplies",  "Pet Supplies",
        "Food & Grocery", "Baby Products",    "Jewelry",           "Tools & Hardware", "Music & Instruments",
        "Art & Crafts",   "Travel & Luggage", "Software",          "Collectibles",     "Industrial",
    };

    constexpr std::array<std::string_view, 20> TagNames = {
        "bestseller", "new-arrival", "on-sale",     "eco-friendly",    "premium",   "limited-edition", "clearance",
        "trending",   "popular",     "recommended", "budget-friendly", "top-rated", "exclusive",       "handmade",
        "organic",    "vegan",       "wireless",    "smart",           "portable",  "durable",
    };

    constexpr std::array<std::string_view, 10> ActionTypes = {
        "login",    "logout", "view_product",   "add_to_cart", "remove_from_cart",
        "purchase", "search", "update_profile", "add_review",  "wishlist_add",
    };

    constexpr std::array<std::string_view, 5> SeverityLevels = {
        "debug", "info", "warning", "error", "critical",
    };

    constexpr std::array<std::string_view, 10> EventSources = {
        "web_server",    "api_gateway",   "payment_service", "auth_service", "inventory_service",
        "email_service", "search_engine", "cache_layer",     "database",     "message_queue",
    };

} // anonymous namespace

GeneratorConfig CreateScaledConfig(double scaleFactor)
{
    GeneratorConfig config;

    config.userCount = static_cast<size_t>(static_cast<double>(config.userCount) * scaleFactor);
    config.categoryCount = static_cast<size_t>(static_cast<double>(config.categoryCount) * scaleFactor);
    config.productCount = static_cast<size_t>(static_cast<double>(config.productCount) * scaleFactor);
    config.productImageCount = static_cast<size_t>(static_cast<double>(config.productImageCount) * scaleFactor);
    config.orderCount = static_cast<size_t>(static_cast<double>(config.orderCount) * scaleFactor);
    config.orderItemCount = static_cast<size_t>(static_cast<double>(config.orderItemCount) * scaleFactor);
    config.reviewCount = static_cast<size_t>(static_cast<double>(config.reviewCount) * scaleFactor);
    config.tagCount = static_cast<size_t>(static_cast<double>(config.tagCount) * scaleFactor);
    config.productTagCount = static_cast<size_t>(static_cast<double>(config.productTagCount) * scaleFactor);
    config.activityLogCount = static_cast<size_t>(static_cast<double>(config.activityLogCount) * scaleFactor);
    config.systemAuditLogCount = static_cast<size_t>(static_cast<double>(config.systemAuditLogCount) * scaleFactor);
    config.articleCount = static_cast<size_t>(static_cast<double>(config.articleCount) * scaleFactor);

    // Ensure minimum counts
    config.userCount = std::max(config.userCount, size_t { 10 });
    config.categoryCount = std::max(config.categoryCount, size_t { 5 });
    config.productCount = std::max(config.productCount, size_t { 10 });
    config.tagCount = std::max(config.tagCount, size_t { 5 });

    return config;
}

SeededRandom::SeededRandom(uint64_t seed):
    m_generator(seed)
{
}

int64_t SeededRandom::NextInt(int64_t min, int64_t max)
{
    std::uniform_int_distribution<int64_t> dist(min, max);
    return dist(m_generator);
}

double SeededRandom::NextDouble(double min, double max)
{
    std::uniform_real_distribution<double> dist(min, max);
    return dist(m_generator);
}

bool SeededRandom::NextBool(double probabilityTrue)
{
    return NextDouble(0.0, 1.0) < probabilityTrue;
}

std::string SeededRandom::GenerateText(size_t targetSize)
{
    std::string result;
    result.reserve(targetSize + 100);

    while (result.size() < targetSize)
    {
        if (!result.empty())
            result += ' ';

        auto const& word = CommonWords[static_cast<size_t>(NextInt(0, CommonWords.size() - 1))];
        result += word;

        // Add punctuation occasionally
        if (NextBool(0.1))
            result += '.';
        else if (NextBool(0.05))
            result += ',';
    }

    // Trim to target size if necessary
    if (result.size() > targetSize)
        result.resize(targetSize);

    return result;
}

std::vector<uint8_t> SeededRandom::GenerateBinaryData(size_t targetSize)
{
    std::vector<uint8_t> data(targetSize);

    // Generate pseudo-random binary data in chunks for efficiency
    for (size_t i = 0; i < targetSize; i += 8)
    {
        auto const value = static_cast<uint64_t>(NextInt(0, INT64_MAX));
        auto const bytesToCopy = std::min(size_t { 8 }, targetSize - i);
        std::memcpy(data.data() + i, &value, bytesToCopy);
    }

    return data;
}

std::string SeededRandom::GenerateJson(size_t targetSize)
{
    std::string json = "{\n";

    auto const keyCount = targetSize / 50; // Approximate number of key-value pairs
    for (size_t i = 0; i < keyCount && json.size() < targetSize; ++i)
    {
        if (i > 0)
            json += ",\n";

        json += std::format(R"(  "field_{}": "{}")", i, GenerateText(30));
    }

    json += "\n}";

    // Pad with spaces if too short
    while (json.size() < targetSize)
        json.insert(json.size() - 2, "  ");

    // Trim if too long
    if (json.size() > targetSize)
        json.resize(targetSize);

    return json;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::string SeededRandom::GenerateEmail(int64_t userId)
{
    auto const userIdU = static_cast<size_t>(userId);
    auto const firstName = FirstNames[userIdU % FirstNames.size()];
    auto const lastName = LastNames[(userIdU / FirstNames.size()) % LastNames.size()];
    return std::format("{}.{}{}@example.com", firstName, lastName, userId);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
std::string SeededRandom::GenerateProductName(int64_t productId)
{
    auto const productIdU = static_cast<size_t>(productId);
    auto const adj = ProductAdjectives[productIdU % ProductAdjectives.size()];
    auto const noun = ProductNouns[(productIdU / ProductAdjectives.size()) % ProductNouns.size()];
    return std::format("{} {} {}", adj, noun, productId);
}

std::string SeededRandom::GenerateAddressJson()
{
    return std::format(R"({{
  "street": "{} {} Street",
  "city": "City{}",
  "state": "State{}",
  "zip": "{:05d}",
  "country": "Country{}"
}})",
                       NextInt(100, 9999),
                       CommonWords[static_cast<size_t>(NextInt(0, CommonWords.size() - 1))],
                       NextInt(1, 100),
                       NextInt(1, 50),
                       static_cast<int>(NextInt(10000, 99999)),
                       NextInt(1, 20));
}

void CreateSchema(Light::DataMapper& dm)
{
    // Create tables in dependency order
    dm.CreateTable<LargeDb_User>();
    dm.CreateTable<LargeDb_Category>();
    dm.CreateTable<LargeDb_Tag>();
    dm.CreateTable<LargeDb_Product>();
    dm.CreateTable<LargeDb_ProductImage>();
    dm.CreateTable<LargeDb_ProductTag>();
    dm.CreateTable<LargeDb_Order>();
    dm.CreateTable<LargeDb_OrderItem>();
    dm.CreateTable<LargeDb_Review>();
    dm.CreateTable<LargeDb_ActivityLog>();
    dm.CreateTable<LargeDb_SystemAuditLog>();
    dm.CreateTable<LargeDb_Article>();
}

void DropSchema(Light::DataMapper& dm)
{
    auto stmt = SqlStatement(dm.Connection());

    // Drop tables in reverse dependency order
    auto const tables = std::array {
        "LargeDb_Article",  "LargeDb_SystemAuditLog", "LargeDb_ActivityLog",  "LargeDb_Review",  "LargeDb_OrderItem",
        "LargeDb_Order",    "LargeDb_ProductTag",     "LargeDb_ProductImage", "LargeDb_Product", "LargeDb_Tag",
        "LargeDb_Category", "LargeDb_User",
    };

    for (auto const& table: tables)
    {
        try
        {
            (void) stmt.ExecuteDirect(std::format("DROP TABLE IF EXISTS \"{}\"", table));
        }
        // NOLINTNEXTLINE(bugprone-empty-catch) - intentionally ignoring errors during cleanup
        catch (...)
        {
        }
    }
}

namespace
{

    /// @brief Helper to convert vector<uint8_t> to SqlDynamicBinary
    template <size_t N>
    SqlDynamicBinary<N> ToSqlBinary(std::vector<uint8_t> const& data)
    {
        auto const copySize = std::min(data.size(), N);
        return SqlDynamicBinary<N>(data.data(), data.data() + copySize);
    }

} // anonymous namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void PopulateDatabase(Light::DataMapper& dm,
                      GeneratorConfig const& config,
                      std::function<void(double, std::string_view)> progressCallback)
{
    auto rng = SeededRandom(config.seed);

    auto const totalSteps = 12;
    auto currentStep = 0;

    auto reportProgress = [&](std::string_view entity) {
        ++currentStep;
        if (progressCallback)
            progressCallback(static_cast<double>(currentStep) / totalSteps, entity);
    };

    auto const now = SqlDateTime::Now();

    // Use transactions for better performance
    auto transaction = SqlTransaction(dm.Connection(), SqlTransactionMode::COMMIT);

    // 1. Create Users
    for (size_t i = 0; i < config.userCount; ++i)
    {
        auto const userId = static_cast<int64_t>(i) + 1;
        auto const firstNameStr = std::string(FirstNames[i % FirstNames.size()]);
        auto const lastNameStr = std::string(LastNames[(i / FirstNames.size()) % LastNames.size()]);

        auto const bioText = rng.GenerateText(config.userBioSize);
        auto const avatarData = rng.GenerateBinaryData(config.userAvatarSize);

        auto user = LargeDb_User {
            .guid = SqlGuid::Create(),
            .email = rng.GenerateEmail(userId),
            .first_name = firstNameStr,
            .last_name = lastNameStr,
            .password_hash = std::format("hash_{}", userId),
            .bio = SqlText { bioText },
            // Store avatar as base64-like text data
            .avatar = SqlText { rng.GenerateText(config.userAvatarSize) },
            .is_active = rng.NextBool(0.95),
            .is_verified = rng.NextBool(0.7),
            .created_at = now,
        };
        if (rng.NextBool(0.8))
            user.last_login_at = now;

        dm.Create(user);
    }
    reportProgress("Users");

    // 2. Create Categories (with hierarchy)
    for (size_t i = 0; i < config.categoryCount; ++i)
    {
        auto category = LargeDb_Category {
            .name = std::format("{} {}", CategoryNames[i % CategoryNames.size()], i),
            .description = SqlText { rng.GenerateText(config.categoryDescriptionSize) },
            .slug = std::format("category-{}", i),
            .is_active = rng.NextBool(0.9),
            .sort_order = static_cast<int>(i),
            // parent is left null (root categories)
        };

        dm.Create(category);
    }
    reportProgress("Categories");

    // 3. Create Tags
    for (size_t i = 0; i < config.tagCount; ++i)
    {
        auto tag = LargeDb_Tag {
            .name = std::format("{}{}", TagNames[i % TagNames.size()], i),
            .description = std::format("Description for tag {}", i),
            .slug = std::format("tag-{}", i),
        };

        dm.Create(tag);
    }
    reportProgress("Tags");

    // 4. Create Products
    // For products, we need to create a temporary category to reference
    std::vector<uint64_t> productIds;
    productIds.reserve(config.productCount);

    for (size_t i = 0; i < config.productCount; ++i)
    {
        auto const categoryId = static_cast<uint64_t>(i % config.categoryCount) + 1;
        auto const productName = rng.GenerateProductName(static_cast<int64_t>(i));

        // The random values are drawn up front, in column order, so that the discount can depend on the price.
        auto longDescription = SqlText { rng.GenerateText(config.productLongDescriptionSize) };
        auto specificationsJson = SqlText { rng.GenerateJson(config.productSpecsSize) };
        auto const price = rng.NextDouble(9.99, 999.99);

        auto product = LargeDb_Product {
            .sku = SqlGuid::Create(),
            .name = productName,
            .short_description = std::format("Short description for product {}", i),
            .long_description = std::move(longDescription),
            .specifications_json = std::move(specificationsJson),
            .price = price,
            .discount_price = rng.NextBool(0.3) ? std::optional { price * 0.8 } : std::nullopt,
            .stock_quantity = static_cast<int>(rng.NextInt(0, 1000)),
            .is_active = rng.NextBool(0.9),
            .is_featured = rng.NextBool(0.1),
            .created_at = now,
            .category = categoryId,
        };

        dm.Create(product);
        productIds.push_back(product.id.Value());
    }
    reportProgress("Products");

    // 5. Create ProductImages (main size driver)
    for (size_t i = 0; i < config.productImageCount; ++i)
    {
        auto const productId = productIds[i % productIds.size()];

        auto image = LargeDb_ProductImage {
            .filename = std::format("product_image_{}.jpg", i),
            .content_type = std::string_view("image/jpeg"),
            // Store image data as text for compatibility
            .image_data = SqlText { rng.GenerateText(config.productImageSize) },
            .thumbnail_data = SqlText { rng.GenerateText(config.productThumbnailSize) },
            .sort_order = static_cast<int>(i % 4),
            .is_primary = (i % 4) == 0,
            .created_at = now,
            .product = productId,
        };

        dm.Create(image);
    }
    reportProgress("ProductImages");

    // 6. Create ProductTags
    for (size_t i = 0; i < config.productTagCount; ++i)
    {
        auto const productId = productIds[i % productIds.size()];
        auto const tagId = static_cast<uint64_t>(i % config.tagCount) + 1;

        auto productTag = LargeDb_ProductTag {
            .product = productId,
            .tag = tagId,
        };

        dm.Create(productTag);
    }
    reportProgress("ProductTags");

    // 7. Create Orders
    std::vector<uint64_t> orderIds;
    orderIds.reserve(config.orderCount);

    for (size_t i = 0; i < config.orderCount; ++i)
    {
        auto const userId = static_cast<uint64_t>(i % config.userCount) + 1;

        static constexpr std::array OrderStatuses = { "pending", "processing", "shipped", "delivered", "cancelled" };
        auto const statusIdx = i % OrderStatuses.size();
        std::string_view status = OrderStatuses[statusIdx];

        auto subtotalVal = rng.NextDouble(10.0, 500.0);
        auto taxVal = subtotalVal * 0.08;
        auto shippingVal = rng.NextDouble(5.0, 25.0);

        auto order = LargeDb_Order {
            .order_number = SqlGuid::Create(),
            .status = status,
            .subtotal = subtotalVal,
            .tax_amount = taxVal,
            .shipping_amount = shippingVal,
            .total_amount = subtotalVal + taxVal + shippingVal,
            .shipping_address_json = SqlText { rng.GenerateAddressJson() },
            .billing_address_json = SqlText { rng.GenerateAddressJson() },
            .created_at = now,
            .user = userId,
        };
        if (rng.NextBool(0.2))
            order.notes = rng.GenerateText(100);

        dm.Create(order);
        orderIds.push_back(order.id.Value());
    }
    reportProgress("Orders");

    // 8. Create OrderItems
    for (size_t i = 0; i < config.orderItemCount; ++i)
    {
        auto const orderId = orderIds[i % orderIds.size()];
        auto const productId = productIds[i % productIds.size()];

        auto qty = static_cast<int>(rng.NextInt(1, 5));
        auto unitPriceVal = rng.NextDouble(9.99, 199.99);
        auto totalPriceVal = unitPriceVal * qty;

        auto orderItem = LargeDb_OrderItem {
            .quantity = qty,
            .unit_price = unitPriceVal,
            .total_price = totalPriceVal,
            .order = orderId,
            .product = productId,
        };
        if (rng.NextBool(0.1))
            orderItem.discount_amount = totalPriceVal * 0.1;

        dm.Create(orderItem);
    }
    reportProgress("OrderItems");

    // 9. Create Reviews
    for (size_t i = 0; i < config.reviewCount; ++i)
    {
        auto const userId = static_cast<uint64_t>(i % config.userCount) + 1;
        auto const productId = productIds[i % productIds.size()];

        // Braced initialization evaluates left to right, so the random draws keep their column order.
        auto review = LargeDb_Review {
            .rating = static_cast<int>(rng.NextInt(1, 5)),
            .title = std::format("Review Title {}", i),
            .content = SqlText { rng.GenerateText(config.reviewContentSize) },
            .pros = rng.NextBool(0.5) ? std::optional { SqlText { rng.GenerateText(200) } } : std::nullopt,
            .cons = rng.NextBool(0.5) ? std::optional { SqlText { rng.GenerateText(200) } } : std::nullopt,
            .is_verified_purchase = rng.NextBool(0.7),
            .helpful_votes = static_cast<int>(rng.NextInt(0, 100)),
            .created_at = now,
            .user = userId,
            .product = productId,
        };

        dm.Create(review);
    }
    reportProgress("Reviews");

    // 10. Create ActivityLogs (high volume)
    for (size_t i = 0; i < config.activityLogCount; ++i)
    {
        auto const productId = productIds[i % productIds.size()];

        auto log = LargeDb_ActivityLog {
            .action_type = ActionTypes[i % ActionTypes.size()],
            .entity_type = std::string_view("Product"),
            .entity_id = productId,
            .old_values_json = rng.NextBool(0.5) ? std::optional { SqlText { rng.GenerateJson(config.activityLogJsonSize) } }
                                                 : std::nullopt,
            .new_values_json = rng.NextBool(0.5) ? std::optional { SqlText { rng.GenerateJson(config.activityLogJsonSize) } }
                                                 : std::nullopt,
            .ip_address = std::format("192.168.{}.{}", rng.NextInt(0, 255), rng.NextInt(0, 255)),
            .user_agent = std::string_view("Mozilla/5.0 (compatible; TestAgent/1.0)"),
            .created_at = now,
        };

        // Assign to a user (with some anonymous activities)
        if (rng.NextBool(0.9))
        {
            auto const userId = static_cast<uint64_t>(i % config.userCount) + 1;
            log.user = userId;
        }

        dm.Create(log);
    }
    reportProgress("ActivityLogs");

    // 11. Create SystemAuditLogs
    for (size_t i = 0; i < config.systemAuditLogCount; ++i)
    {
        auto log = LargeDb_SystemAuditLog {
            .severity = SeverityLevels[i % SeverityLevels.size()],
            .source = EventSources[i % EventSources.size()],
            .event_type = std::format("event_type_{}", i % 50),
            .message = SqlText { std::format("System audit message {}: {}", i, rng.GenerateText(100)) },
            .context_json = SqlText { rng.GenerateJson(config.systemAuditContextSize) },
            .correlation_id = std::format("corr-{}", i),
            .created_at = now,
        };
        if (rng.NextBool(0.3))
            log.stack_trace = SqlText { rng.GenerateText(config.systemAuditStackTraceSize) };

        dm.Create(log);
    }
    reportProgress("SystemAuditLogs");

    // 12. Create Articles
    for (size_t i = 0; i < config.articleCount; ++i)
    {
        auto const userId = static_cast<uint64_t>(i % config.userCount) + 1;

        static constexpr std::array ArticleStatuses = { "draft", "published", "archived" };
        auto const statusIdx = i % ArticleStatuses.size();
        std::string_view status = ArticleStatuses[statusIdx];

        auto article = LargeDb_Article {
            .title = std::format("Article Title {} - {}", i, rng.GenerateText(50)).substr(0, 200),
            .slug = std::format("article-{}", i),
            .excerpt = rng.GenerateText(300),
            .content = SqlText { rng.GenerateText(config.articleContentSize) },
            .featured_image = rng.NextBool(0.7)
                                  ? std::optional { SqlText { rng.GenerateText(config.articleFeaturedImageSize) } }
                                  : std::nullopt,
            .status = status,
            .view_count = static_cast<int>(rng.NextInt(0, 10000)),
            .allow_comments = rng.NextBool(0.8),
            .created_at = now,
            .author = userId,
        };
        if (statusIdx == 1)
            article.published_at = now;

        dm.Create(article);
    }
    reportProgress("Articles");

    transaction.Commit();
}

size_t GetExpectedDataSize(GeneratorConfig const& config)
{
    size_t totalSize = 0;

    // User: id + guid + email + names + password_hash + bio + avatar + flags + dates
    totalSize += config.userCount * (8 + 16 + 100 + 50 + 50 + 100 + config.userBioSize + config.userAvatarSize + 4 + 32);

    // Category: id + name + description + slug + flags
    totalSize += config.categoryCount * (8 + 100 + config.categoryDescriptionSize + 200 + 8);

    // Product: id + sku + names + descriptions + price + stock + dates
    totalSize += config.productCount
                 * (8 + 16 + 200 + 500 + config.productLongDescriptionSize + config.productSpecsSize + 32 + 8 + 32);

    // ProductImage: id + filename + content_type + image_data + thumbnail + sort + dates
    totalSize += config.productImageCount * (8 + 200 + 100 + config.productImageSize + config.productThumbnailSize + 8 + 16);

    // Order: id + guid + status + amounts + addresses + notes + dates
    totalSize += config.orderCount * (8 + 16 + 20 + 48 + 1000 + 500 + 32);

    // OrderItem: id + quantity + prices
    totalSize += config.orderItemCount * (8 + 4 + 32 + 16);

    // Review: id + rating + title + content + pros/cons + votes + dates
    totalSize += config.reviewCount * (8 + 4 + 200 + config.reviewContentSize + 400 + 4 + 32);

    // Tag: id + name + description + slug
    totalSize += config.tagCount * (8 + 50 + 200 + 50);

    // ProductTag: id + product_id + tag_id
    totalSize += config.productTagCount * (8 + 8 + 8);

    // ActivityLog: id + action + entity + json + ip + user_agent + dates
    totalSize += config.activityLogCount * (8 + 50 + 100 + 8 + (config.activityLogJsonSize * 2) + 45 + 500 + 16);

    // SystemAuditLog: id + severity + source + event_type + message + context + stack_trace + correlation_id + dates
    totalSize += config.systemAuditLogCount
                 * (8 + 50 + 100 + 200 + 200 + config.systemAuditContextSize + config.systemAuditStackTraceSize + 200 + 16);

    // Article: id + title + slug + excerpt + content + featured_image + status + view_count + dates
    totalSize += config.articleCount
                 * (8 + 200 + 200 + 500 + config.articleContentSize + config.articleFeaturedImageSize + 20 + 4 + 48);

    return totalSize;
}

} // namespace LargeDb
