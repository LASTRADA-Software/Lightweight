// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Utils.hpp"

#include <Lightweight/Lightweight.hpp>

#include <ostream>

/// @file Entities.hpp
/// @brief Entity definitions for the large test database schema.
///
/// This file contains all entity structs for a complex 500MB+ test database
/// that works across all supported backends (SQLite3, PostgreSQL, MS-SQL Server).

// Forward declarations
struct LargeDb_User;
struct LargeDb_Category;
struct LargeDb_Product;
struct LargeDb_ProductImage;
struct LargeDb_Order;
struct LargeDb_OrderItem;
struct LargeDb_Review;
struct LargeDb_Tag;
struct LargeDb_ProductTag;
struct LargeDb_ActivityLog;
struct LargeDb_SystemAuditLog;
struct LargeDb_Article;

/// @brief User entity with large bio and avatar fields.
/// Estimated: 2,000 rows × 11KB = 22MB
struct LargeDb_User
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlGuid> guid;
    Light::Field<Light::SqlAnsiString<100>> email;
    Light::Field<Light::SqlAnsiString<50>> first_name;
    Light::Field<Light::SqlAnsiString<50>> last_name;
    Light::Field<Light::SqlAnsiString<100>> password_hash;
    Light::Field<std::optional<Light::SqlText>> bio;    // ~500 bytes
    Light::Field<std::optional<Light::SqlText>> avatar; // ~10KB (stored as text for compatibility)
    Light::Field<bool> is_active { true };
    Light::Field<bool> is_verified { false };
    Light::Field<Light::SqlDateTime> created_at;
    Light::Field<std::optional<Light::SqlDateTime>> last_login_at;
};

/// @brief Category entity.
/// Estimated: 200 rows × 2.5KB = 0.5MB
struct LargeDb_Category
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlAnsiString<100>> name;
    Light::Field<Light::SqlText> description; // ~2KB
    Light::Field<std::optional<Light::SqlAnsiString<200>>> slug;
    Light::Field<bool> is_active { true };
    Light::Field<int> sort_order { 0 };
    // Simplified: removed self-referential parent to avoid ORM recursion issues
    Light::Field<std::optional<uint64_t>> parent_id {};
};

/// @brief Product entity with large description and specifications JSON.
/// Estimated: 2,000 rows × 12KB = 24MB
struct LargeDb_Product
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlGuid> sku;
    Light::Field<Light::SqlAnsiString<200>> name;
    Light::Field<std::optional<Light::SqlAnsiString<500>>> short_description;
    Light::Field<Light::SqlText> long_description;    // ~8KB - main size contributor
    Light::Field<Light::SqlText> specifications_json; // ~2KB
    Light::Field<double> price;
    Light::Field<std::optional<double>> discount_price;
    Light::Field<int> stock_quantity { 0 };
    Light::Field<bool> is_active { true };
    Light::Field<bool> is_featured { false };
    Light::Field<Light::SqlDateTime> created_at;
    Light::Field<std::optional<Light::SqlDateTime>> updated_at;

    Light::BelongsTo<Member(LargeDb_Category::id), Light::SqlRealName { "category_id" }> category {};
};

/// @brief ProductImage entity - main size driver with large binary image data.
/// Estimated: 4,000 rows × 55KB = 220MB (largest contributor)
struct LargeDb_ProductImage
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlAnsiString<200>> filename;
    Light::Field<Light::SqlAnsiString<100>> content_type;
    Light::Field<Light::SqlText> image_data;     // ~50KB pseudo-image (stored as text for compatibility)
    Light::Field<Light::SqlText> thumbnail_data; // ~5KB pseudo-thumbnail (stored as text for compatibility)
    Light::Field<int> sort_order { 0 };
    Light::Field<bool> is_primary { false };
    Light::Field<Light::SqlDateTime> created_at;

    Light::BelongsTo<Member(LargeDb_Product::id), Light::SqlRealName { "product_id" }> product {};
};

/// @brief Order entity with address JSON fields.
/// Estimated: 10,000 rows × 1.5KB = 15MB
struct LargeDb_Order
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlGuid> order_number;
    Light::Field<Light::SqlAnsiString<20>> status; // pending, processing, shipped, delivered, cancelled
    Light::Field<double> subtotal;
    Light::Field<double> tax_amount;
    Light::Field<double> shipping_amount;
    Light::Field<double> total_amount;
    Light::Field<Light::SqlText> shipping_address_json; // ~500 bytes
    Light::Field<Light::SqlText> billing_address_json;  // ~500 bytes
    Light::Field<std::optional<Light::SqlAnsiString<500>>> notes;
    Light::Field<Light::SqlDateTime> created_at;
    Light::Field<std::optional<Light::SqlDateTime>> updated_at;

    Light::BelongsTo<Member(LargeDb_User::id), Light::SqlRealName { "user_id" }> user {};
};

/// @brief OrderItem entity - join between Order and Product with quantity and price.
/// Estimated: 30,000 rows × 600B = 18MB
struct LargeDb_OrderItem
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<int> quantity;
    Light::Field<double> unit_price;
    Light::Field<double> total_price;
    Light::Field<std::optional<double>> discount_amount;

    Light::BelongsTo<Member(LargeDb_Order::id), Light::SqlRealName { "order_id" }> order {};
    Light::BelongsTo<Member(LargeDb_Product::id), Light::SqlRealName { "product_id" }> product {};
};

/// @brief Review entity with content, pros and cons.
/// Estimated: 8,000 rows × 3.5KB = 28MB
struct LargeDb_Review
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<int> rating; // 1-5
    Light::Field<std::optional<Light::SqlAnsiString<200>>> title;
    Light::Field<Light::SqlText> content; // ~2KB
    Light::Field<std::optional<Light::SqlText>> pros;
    Light::Field<std::optional<Light::SqlText>> cons;
    Light::Field<bool> is_verified_purchase { false };
    Light::Field<int> helpful_votes { 0 };
    Light::Field<Light::SqlDateTime> created_at;
    Light::Field<std::optional<Light::SqlDateTime>> updated_at;

    Light::BelongsTo<Member(LargeDb_User::id), Light::SqlRealName { "user_id" }> user {};
    Light::BelongsTo<Member(LargeDb_Product::id), Light::SqlRealName { "product_id" }> product {};
};

/// @brief Tag entity for product tagging.
/// Estimated: 500 rows × 350B = 175KB
struct LargeDb_Tag
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlAnsiString<50>> name;
    Light::Field<std::optional<Light::SqlAnsiString<200>>> description;
    Light::Field<Light::SqlAnsiString<50>> slug;
};

/// @brief ProductTag join table for many-to-many Product<->Tag relationship.
/// Estimated: 6,000 rows × 50B = 300KB
struct LargeDb_ProductTag
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;

    Light::BelongsTo<Member(LargeDb_Product::id), Light::SqlRealName { "product_id" }> product {};
    Light::BelongsTo<Member(LargeDb_Tag::id), Light::SqlRealName { "tag_id" }> tag {};
};

/// @brief ActivityLog for tracking user actions - high volume table.
/// Estimated: 50,000 rows × 2.5KB = 125MB
struct LargeDb_ActivityLog
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlAnsiString<50>> action_type; // login, logout, view_product, add_to_cart, purchase, etc.
    Light::Field<Light::SqlAnsiString<100>> entity_type;
    Light::Field<std::optional<uint64_t>> entity_id;
    Light::Field<std::optional<Light::SqlText>> old_values_json; // ~1KB
    Light::Field<std::optional<Light::SqlText>> new_values_json; // ~1KB
    Light::Field<std::optional<Light::SqlAnsiString<45>>> ip_address;
    Light::Field<std::optional<Light::SqlAnsiString<500>>> user_agent;
    Light::Field<Light::SqlDateTime> created_at;

    Light::BelongsTo<Member(LargeDb_User::id), Light::SqlRealName { "user_id" }, Light::SqlNullable::Null> user {};
};

/// @brief SystemAuditLog for system-level events - standalone high-volume table.
/// Estimated: 5,000 rows × 8.5KB = 42.5MB
struct LargeDb_SystemAuditLog
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlAnsiString<50>> severity; // debug, info, warning, error, critical
    Light::Field<Light::SqlAnsiString<100>> source;
    Light::Field<Light::SqlAnsiString<200>> event_type;
    Light::Field<Light::SqlText> message;
    Light::Field<Light::SqlText> context_json;               // ~5KB
    Light::Field<std::optional<Light::SqlText>> stack_trace; // ~3KB
    Light::Field<std::optional<Light::SqlAnsiString<200>>> correlation_id;
    Light::Field<Light::SqlDateTime> created_at;
};

/// @brief Article entity for blog/content management with large content.
/// Estimated: 500 rows × 37KB = 18.5MB
struct LargeDb_Article
{
    Light::Field<uint64_t, Light::PrimaryKey::ServerSideAutoIncrement> id;
    Light::Field<Light::SqlAnsiString<200>> title;
    Light::Field<Light::SqlAnsiString<200>> slug;
    Light::Field<std::optional<Light::SqlAnsiString<500>>> excerpt;
    Light::Field<Light::SqlText> content;                       // ~15KB
    Light::Field<std::optional<Light::SqlText>> featured_image; // ~20KB (stored as text for compatibility)
    Light::Field<Light::SqlAnsiString<20>> status;              // draft, published, archived
    Light::Field<int> view_count { 0 };
    Light::Field<bool> allow_comments { true };
    Light::Field<Light::SqlDateTime> created_at;
    Light::Field<std::optional<Light::SqlDateTime>> published_at;
    Light::Field<std::optional<Light::SqlDateTime>> updated_at;

    Light::BelongsTo<Member(LargeDb_User::id), Light::SqlRealName { "author_id" }> author {};
};

// Output stream operators for debugging

inline std::ostream& operator<<(std::ostream& os, LargeDb_User const& value)
{
    return os << std::format("LargeDb_User {{ id: {}, email: {}, name: {} {} }}",
                             value.id.Value(),
                             value.email.Value().c_str(),
                             value.first_name.Value().c_str(),
                             value.last_name.Value().c_str());
}

inline std::ostream& operator<<(std::ostream& os, LargeDb_Category const& value)
{
    return os << std::format("LargeDb_Category {{ id: {}, name: {} }}", value.id.Value(), value.name.Value().c_str());
}

inline std::ostream& operator<<(std::ostream& os, LargeDb_Product const& value)
{
    return os << std::format("LargeDb_Product {{ id: {}, sku: {}, name: {} }}",
                             value.id.Value(),
                             value.sku.Value(),
                             value.name.Value().c_str());
}

inline std::ostream& operator<<(std::ostream& os, LargeDb_Order const& value)
{
    return os << std::format("LargeDb_Order {{ id: {}, order_number: {}, status: {} }}",
                             value.id.Value(),
                             value.order_number.Value(),
                             value.status.Value().c_str());
}

inline std::ostream& operator<<(std::ostream& os, LargeDb_Review const& value)
{
    return os << std::format("LargeDb_Review {{ id: {}, rating: {} }}", value.id.Value(), value.rating.Value());
}

inline std::ostream& operator<<(std::ostream& os, LargeDb_Tag const& value)
{
    return os << std::format("LargeDb_Tag {{ id: {}, name: {} }}", value.id.Value(), value.name.Value().c_str());
}

inline std::ostream& operator<<(std::ostream& os, LargeDb_Article const& value)
{
    return os << std::format("LargeDb_Article {{ id: {}, title: {} }}", value.id.Value(), value.title.Value().c_str());
}
