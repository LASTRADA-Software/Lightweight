// SPDX-License-Identifier: Apache-2.0
//
// Example migration plugin for the Chinook sample database.
//
// 40 forward-only migrations grouped into 9 releases (1.0.0 → 2.0.0),
// written with the LIGHTWEIGHT_SQL_MIGRATION macro.

#include <Lightweight/SqlMigration.hpp>
#include <Lightweight/SqlQuery/Migrate.hpp>

using namespace Lightweight;
using namespace Lightweight::SqlColumnTypeDefinitions;

LIGHTWEIGHT_MIGRATION_PLUGIN()

namespace
{

SqlForeignKeyReferenceDefinition Ref(std::string table, std::string column)
{
    return SqlForeignKeyReferenceDefinition { .tableName = std::move(table), .columnName = std::move(column) };
}

} // namespace

// ===========================================================================
// Release 1.0.0 — initial schema
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422000100, "Create Chinook schema")
{
    plan.CreateTable("Artist")
        .PrimaryKeyWithAutoIncrement("ArtistId", Integer {})
        .Column("Name", NVarchar { 120 });

    plan.CreateTable("Genre")
        .PrimaryKeyWithAutoIncrement("GenreId", Integer {})
        .Column("Name", NVarchar { 120 });

    plan.CreateTable("MediaType")
        .PrimaryKeyWithAutoIncrement("MediaTypeId", Integer {})
        .Column("Name", NVarchar { 120 });

    plan.CreateTable("Playlist")
        .PrimaryKeyWithAutoIncrement("PlaylistId", Integer {})
        .Column("Name", NVarchar { 120 });

    plan.CreateTable("Employee")
        .PrimaryKeyWithAutoIncrement("EmployeeId", Integer {})
        .RequiredColumn("LastName", NVarchar { 20 })
        .RequiredColumn("FirstName", NVarchar { 20 })
        .Column("Title", NVarchar { 30 })
        .ForeignKey("ReportsTo", Integer {}, Ref("Employee", "EmployeeId"))
        .Column("BirthDate", DateTime {})
        .Column("HireDate", DateTime {})
        .Column("Address", NVarchar { 70 })
        .Column("City", NVarchar { 40 })
        .Column("State", NVarchar { 40 })
        .Column("Country", NVarchar { 40 })
        .Column("PostalCode", NVarchar { 10 })
        .Column("Phone", NVarchar { 24 })
        .Column("Fax", NVarchar { 24 })
        .Column("Email", NVarchar { 60 });

    plan.CreateTable("Album")
        .PrimaryKeyWithAutoIncrement("AlbumId", Integer {})
        .RequiredColumn("Title", NVarchar { 160 })
        .RequiredForeignKey("ArtistId", Integer {}, Ref("Artist", "ArtistId"));

    plan.CreateTable("Track")
        .PrimaryKeyWithAutoIncrement("TrackId", Integer {})
        .RequiredColumn("Name", NVarchar { 200 })
        .ForeignKey("AlbumId", Integer {}, Ref("Album", "AlbumId"))
        .RequiredForeignKey("MediaTypeId", Integer {}, Ref("MediaType", "MediaTypeId"))
        .ForeignKey("GenreId", Integer {}, Ref("Genre", "GenreId"))
        .Column("Composer", NVarchar { 220 })
        .RequiredColumn("Milliseconds", Integer {})
        .Column("Bytes", Integer {})
        .RequiredColumn("UnitPrice", Decimal { .precision = 10, .scale = 2 });

    plan.CreateTable("Customer")
        .PrimaryKeyWithAutoIncrement("CustomerId", Integer {})
        .RequiredColumn("FirstName", NVarchar { 40 })
        .RequiredColumn("LastName", NVarchar { 20 })
        .Column("Company", NVarchar { 80 })
        .Column("Address", NVarchar { 70 })
        .Column("City", NVarchar { 40 })
        .Column("State", NVarchar { 40 })
        .Column("Country", NVarchar { 40 })
        .Column("PostalCode", NVarchar { 10 })
        .Column("Phone", NVarchar { 24 })
        .Column("Fax", NVarchar { 24 })
        .RequiredColumn("Email", NVarchar { 60 })
        .ForeignKey("SupportRepId", Integer {}, Ref("Employee", "EmployeeId"));

    plan.CreateTable("Invoice")
        .PrimaryKeyWithAutoIncrement("InvoiceId", Integer {})
        .RequiredForeignKey("CustomerId", Integer {}, Ref("Customer", "CustomerId"))
        .RequiredColumn("InvoiceDate", DateTime {})
        .Column("BillingAddress", NVarchar { 70 })
        .Column("BillingCity", NVarchar { 40 })
        .Column("BillingState", NVarchar { 40 })
        .Column("BillingCountry", NVarchar { 40 })
        .Column("BillingPostalCode", NVarchar { 10 })
        .RequiredColumn("Total", Decimal { .precision = 10, .scale = 2 });

    plan.CreateTable("InvoiceLine")
        .PrimaryKeyWithAutoIncrement("InvoiceLineId", Integer {})
        .RequiredForeignKey("InvoiceId", Integer {}, Ref("Invoice", "InvoiceId"))
        .RequiredForeignKey("TrackId", Integer {}, Ref("Track", "TrackId"))
        .RequiredColumn("UnitPrice", Decimal { .precision = 10, .scale = 2 })
        .RequiredColumn("Quantity", Integer {});

    plan.CreateTable("PlaylistTrack")
        .Column(SqlColumnDeclaration { .name = "PlaylistId",
                                       .type = Integer {},
                                       .primaryKey = SqlPrimaryKeyType::MANUAL,
                                       .required = true })
        .Column(SqlColumnDeclaration { .name = "TrackId",
                                       .type = Integer {},
                                       .primaryKey = SqlPrimaryKeyType::MANUAL,
                                       .required = true })
        .ForeignKey({ "PlaylistId" }, "Playlist", { "PlaylistId" })
        .ForeignKey({ "TrackId" }, "Track", { "TrackId" });

    plan.CreateIndex("IFK_AlbumArtistId", "Album", { "ArtistId" });
    plan.CreateIndex("IFK_CustomerSupportRepId", "Customer", { "SupportRepId" });
    plan.CreateIndex("IFK_EmployeeReportsTo", "Employee", { "ReportsTo" });
    plan.CreateIndex("IFK_InvoiceCustomerId", "Invoice", { "CustomerId" });
    plan.CreateIndex("IFK_InvoiceLineInvoiceId", "InvoiceLine", { "InvoiceId" });
    plan.CreateIndex("IFK_InvoiceLineTrackId", "InvoiceLine", { "TrackId" });
    plan.CreateIndex("IFK_PlaylistTrackTrackId", "PlaylistTrack", { "TrackId" });
    plan.CreateIndex("IFK_TrackAlbumId", "Track", { "AlbumId" });
    plan.CreateIndex("IFK_TrackGenreId", "Track", { "GenreId" });
    plan.CreateIndex("IFK_TrackMediaTypeId", "Track", { "MediaTypeId" });
}
LIGHTWEIGHT_SQL_RELEASE("1.0.0", 20260422000100);

// ===========================================================================
// Release 1.1.0 — Track metadata enhancements
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422000200, "Add Track.Rating")
{
    plan.AlterTable("Track").AddNotRequiredColumn("Rating", Tinyint {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422000300, "Add Track.PlayCount")
{
    plan.AlterTable("Track").AddColumn("PlayCount", Integer {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422000400, "Add Track.LastPlayedAt")
{
    plan.AlterTable("Track").AddNotRequiredColumn("LastPlayedAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422000500, "Add Track.IsExplicit")
{
    plan.AlterTable("Track").AddColumn("IsExplicit", Bool {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422000600, "Index Track.Name for search")
{
    plan.AlterTable("Track").AddIndex("Name");
}
LIGHTWEIGHT_SQL_RELEASE("1.1.0", 20260422000600);

// ===========================================================================
// Release 1.2.0 — Customer profile
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422000700, "Add Customer.DateOfBirth")
{
    plan.AlterTable("Customer").AddNotRequiredColumn("DateOfBirth", Date {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422000800, "Add Customer.Gender")
{
    plan.AlterTable("Customer").AddNotRequiredColumn("Gender", NVarchar { 16 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422000900, "Add Customer.PreferredLanguage")
{
    plan.AlterTable("Customer").AddNotRequiredColumn("PreferredLanguage", NVarchar { 8 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001000, "Add unique index on Customer.Email")
{
    plan.AlterTable("Customer").AddUniqueIndex("Email");
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001100, "Add Customer.CreatedAt")
{
    plan.AlterTable("Customer").AddNotRequiredColumn("CreatedAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001200, "Add Customer.LastLoginAt")
{
    plan.AlterTable("Customer").AddNotRequiredColumn("LastLoginAt", DateTime {});
}
LIGHTWEIGHT_SQL_RELEASE("1.2.0", 20260422001200);

// ===========================================================================
// Release 1.3.0 — Playlist features
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422001300, "Add Playlist.Description")
{
    plan.AlterTable("Playlist").AddNotRequiredColumn("Description", NVarchar { 500 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001400, "Add Playlist.IsPublic")
{
    plan.AlterTable("Playlist").AddColumn("IsPublic", Bool {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001500, "Add Playlist.OwnerCustomerId")
{
    // Plain INTEGER column: SQLite doesn't support ALTER TABLE ADD CONSTRAINT FOREIGN KEY.
    plan.AlterTable("Playlist").AddNotRequiredColumn("OwnerCustomerId", Integer {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001600, "Add Playlist.CreatedAt")
{
    plan.AlterTable("Playlist").AddNotRequiredColumn("CreatedAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001700, "Add PlaylistTrack.AddedAt")
{
    plan.AlterTable("PlaylistTrack").AddNotRequiredColumn("AddedAt", DateTime {});
}
LIGHTWEIGHT_SQL_RELEASE("1.3.0", 20260422001700);

// ===========================================================================
// Release 1.4.0 — Reviews
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422001800, "Create Review table")
{
    plan.CreateTable("Review")
        .PrimaryKeyWithAutoIncrement("ReviewId", Integer {})
        .RequiredForeignKey("CustomerId", Integer {}, Ref("Customer", "CustomerId"))
        .RequiredForeignKey("TrackId", Integer {}, Ref("Track", "TrackId"))
        .RequiredColumn("Rating", Tinyint {})
        .Column("Comment", NVarchar { 1000 })
        .RequiredColumn("CreatedAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422001900, "Create AlbumReview table")
{
    plan.CreateTable("AlbumReview")
        .PrimaryKeyWithAutoIncrement("AlbumReviewId", Integer {})
        .RequiredForeignKey("CustomerId", Integer {}, Ref("Customer", "CustomerId"))
        .RequiredForeignKey("AlbumId", Integer {}, Ref("Album", "AlbumId"))
        .RequiredColumn("Rating", Tinyint {})
        .Column("Comment", NVarchar { 1000 })
        .RequiredColumn("CreatedAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002000, "Index review foreign keys")
{
    plan.CreateIndex("IFK_ReviewCustomerId", "Review", { "CustomerId" });
    plan.CreateIndex("IFK_ReviewTrackId", "Review", { "TrackId" });
    plan.CreateIndex("IFK_AlbumReviewCustomerId", "AlbumReview", { "CustomerId" });
    plan.CreateIndex("IFK_AlbumReviewAlbumId", "AlbumReview", { "AlbumId" });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002100, "Add Review.Helpful counter")
{
    plan.AlterTable("Review").AddColumn("Helpful", Integer {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002200, "Add Review.UpdatedAt")
{
    plan.AlterTable("Review").AddNotRequiredColumn("UpdatedAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002300, "Rename Review.Comment to Body")
{
    plan.AlterTable("Review").RenameColumn("Comment", "Body");
}
LIGHTWEIGHT_SQL_RELEASE("1.4.0", 20260422002300);

// ===========================================================================
// Release 1.5.0 — Subscriptions
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422002400, "Create SubscriptionPlan")
{
    plan.CreateTable("SubscriptionPlan")
        .PrimaryKeyWithAutoIncrement("SubscriptionPlanId", Integer {})
        .RequiredColumn("Name", NVarchar { 60 })
        .RequiredColumn("PriceMonthly", Decimal { .precision = 10, .scale = 2 })
        .Column("Description", NVarchar { 500 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002500, "Create Subscription")
{
    plan.CreateTable("Subscription")
        .PrimaryKeyWithAutoIncrement("SubscriptionId", Integer {})
        .RequiredForeignKey("CustomerId", Integer {}, Ref("Customer", "CustomerId"))
        .RequiredForeignKey("SubscriptionPlanId", Integer {}, Ref("SubscriptionPlan", "SubscriptionPlanId"))
        .RequiredColumn("StartedAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002600, "Add Customer.CurrentSubscriptionId")
{
    // Plain INTEGER column: SQLite doesn't support ALTER TABLE ADD CONSTRAINT FOREIGN KEY.
    plan.AlterTable("Customer").AddNotRequiredColumn("CurrentSubscriptionId", Integer {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002700, "Add Subscription.CancelledAt")
{
    plan.AlterTable("Subscription").AddNotRequiredColumn("CancelledAt", DateTime {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422002800, "Index Subscription foreign keys")
{
    plan.CreateIndex("IFK_SubscriptionCustomerId", "Subscription", { "CustomerId" });
    plan.CreateIndex("IFK_SubscriptionPlanId", "Subscription", { "SubscriptionPlanId" });
}
LIGHTWEIGHT_SQL_RELEASE("1.5.0", 20260422002800);

// ===========================================================================
// Release 1.6.0 — Artist extras
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422002900, "Add Artist.Bio")
{
    plan.AlterTable("Artist").AddNotRequiredColumn("Bio", NVarchar { 2000 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003000, "Add Artist.ImageUrl")
{
    plan.AlterTable("Artist").AddNotRequiredColumn("ImageUrl", NVarchar { 255 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003100, "Add Artist.CountryCode")
{
    plan.AlterTable("Artist").AddNotRequiredColumn("CountryCode", Char { 2 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003200, "Add Artist.FormedYear")
{
    plan.AlterTable("Artist").AddNotRequiredColumn("FormedYear", Smallint {});
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003300, "Create Tag table")
{
    plan.CreateTable("Tag")
        .PrimaryKeyWithAutoIncrement("TagId", Integer {})
        .RequiredColumn("Name", NVarchar { 60 })
        .Unique();
}
LIGHTWEIGHT_SQL_RELEASE("1.6.0", 20260422003300);

// ===========================================================================
// Release 1.7.0 — Tagging joins
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422003400, "Create TrackTag junction")
{
    plan.CreateTable("TrackTag")
        .Column(SqlColumnDeclaration {
            .name = "TrackId", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true })
        .Column(SqlColumnDeclaration {
            .name = "TagId", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true })
        .ForeignKey({ "TrackId" }, "Track", { "TrackId" })
        .ForeignKey({ "TagId" }, "Tag", { "TagId" });
    plan.CreateIndex("IFK_TrackTagTagId", "TrackTag", { "TagId" });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003500, "Create ArtistTag junction")
{
    plan.CreateTable("ArtistTag")
        .Column(SqlColumnDeclaration {
            .name = "ArtistId", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true })
        .Column(SqlColumnDeclaration {
            .name = "TagId", .type = Integer {}, .primaryKey = SqlPrimaryKeyType::MANUAL, .required = true })
        .ForeignKey({ "ArtistId" }, "Artist", { "ArtistId" })
        .ForeignKey({ "TagId" }, "Tag", { "TagId" });
    plan.CreateIndex("IFK_ArtistTagTagId", "ArtistTag", { "TagId" });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003600, "Index Tag.Name")
{
    plan.AlterTable("Tag").AddIndex("Name");
}
LIGHTWEIGHT_SQL_RELEASE("1.7.0", 20260422003600);

// ===========================================================================
// Release 2.0.0 — breaking changes
// ===========================================================================

LIGHTWEIGHT_SQL_MIGRATION(20260422003700, "Rename Employee.ReportsTo to ManagerId")
{
    plan.AlterTable("Employee").RenameColumn("ReportsTo", "ManagerId");
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003800, "Add Invoice.Currency")
{
    plan.AlterTable("Invoice").AddNotRequiredColumn("Currency", Char { 3 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422003900, "Add InvoiceLine.Discount")
{
    plan.AlterTable("InvoiceLine").AddNotRequiredColumn("Discount", Decimal { .precision = 10, .scale = 2 });
}

LIGHTWEIGHT_SQL_MIGRATION(20260422004000, "Drop obsolete Customer.Fax column")
{
    plan.AlterTable("Customer").DropColumn("Fax");
}
LIGHTWEIGHT_SQL_RELEASE("2.0.0", 20260422004000);
