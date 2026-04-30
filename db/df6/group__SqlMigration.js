var group__SqlMigration =
[
    [ "Lightweight::SqlMigration::MigrationTimestamp", "d3/d3b/structLightweight_1_1SqlMigration_1_1MigrationTimestamp.html", [
      [ "operator<=>", "d3/d3b/structLightweight_1_1SqlMigration_1_1MigrationTimestamp.html#a04e7dbdc97868e3dfe263266363c4b45", null ],
      [ "value", "d3/d3b/structLightweight_1_1SqlMigration_1_1MigrationTimestamp.html#a7ff59005141458f049508004b33e77ee", null ]
    ] ],
    [ "Lightweight::SqlMigration::ChecksumVerificationResult", "da/d49/structLightweight_1_1SqlMigration_1_1ChecksumVerificationResult.html", [
      [ "timestamp", "da/d49/structLightweight_1_1SqlMigration_1_1ChecksumVerificationResult.html#a23fd8a93e907e447544b0acf4a5453be", null ],
      [ "title", "da/d49/structLightweight_1_1SqlMigration_1_1ChecksumVerificationResult.html#ae89a23f74516602d7c4e773d768b0436", null ],
      [ "storedChecksum", "da/d49/structLightweight_1_1SqlMigration_1_1ChecksumVerificationResult.html#ad7d7d1e51e7cc53def26f3de24754cb9", null ],
      [ "computedChecksum", "da/d49/structLightweight_1_1SqlMigration_1_1ChecksumVerificationResult.html#a43d5cee600034783b2a864d126faf481", null ],
      [ "matches", "da/d49/structLightweight_1_1SqlMigration_1_1ChecksumVerificationResult.html#ae3b7009e838fb2ab37c9cd65740c20e7", null ]
    ] ],
    [ "Lightweight::SqlMigration::RevertResult", "d1/de7/structLightweight_1_1SqlMigration_1_1RevertResult.html", [
      [ "revertedTimestamps", "d1/de7/structLightweight_1_1SqlMigration_1_1RevertResult.html#a14223384e1e346debcb57e6c4383154f", null ],
      [ "failedAt", "d1/de7/structLightweight_1_1SqlMigration_1_1RevertResult.html#a44e625e09b82ed9a3612acc9faf3eac8", null ],
      [ "errorMessage", "d1/de7/structLightweight_1_1SqlMigration_1_1RevertResult.html#aadae093f5fac7dd9ffe41f7be93ca0e5", null ]
    ] ],
    [ "Lightweight::SqlMigration::MigrationStatus", "d8/d37/structLightweight_1_1SqlMigration_1_1MigrationStatus.html", [
      [ "appliedCount", "d8/d37/structLightweight_1_1SqlMigration_1_1MigrationStatus.html#a1ac8a6b73ea294f3a7bee66b97feb2c1", null ],
      [ "pendingCount", "d8/d37/structLightweight_1_1SqlMigration_1_1MigrationStatus.html#aad711e9003b262ff1dc9e576dd8e4ef7", null ],
      [ "mismatchCount", "d8/d37/structLightweight_1_1SqlMigration_1_1MigrationStatus.html#a05dd8547017c74830846316c54a1a415", null ],
      [ "unknownAppliedCount", "d8/d37/structLightweight_1_1SqlMigration_1_1MigrationStatus.html#add9ed9d433afd954d99e936d1d3e0367", null ],
      [ "totalRegistered", "d8/d37/structLightweight_1_1SqlMigration_1_1MigrationStatus.html#a5ae7285c2d9fe56478bbd53631e5fb27", null ]
    ] ],
    [ "Lightweight::SqlMigration::MigrationRelease", "d1/d4f/structLightweight_1_1SqlMigration_1_1MigrationRelease.html", [
      [ "version", "d1/d4f/structLightweight_1_1SqlMigration_1_1MigrationRelease.html#ab1116d155822475c5e653b49aa5805aa", null ],
      [ "highestTimestamp", "d1/d4f/structLightweight_1_1SqlMigration_1_1MigrationRelease.html#a48cc0c694f65a3ad2e43b839ab5d5088", null ]
    ] ],
    [ "Lightweight::SqlMigration::MigrationManager", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html", [
      [ "MigrationList", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a259626f016bf24f265c81c1bc4446ae1", null ],
      [ "ExecuteCallback", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a14e9ebfa20b805d80c18d7c6a378e364", null ],
      [ "AddMigration", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a1e2638d4b642e5692df3d2ab24e199a4", null ],
      [ "GetAllMigrations", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a265744d717ca33feffc27bb6c024bb07", null ],
      [ "GetMigration", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a56194b41fc9b404256d379bd51f5002d", null ],
      [ "RemoveAllMigrations", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a301e0219e6dcd600640c63e5b53839eb", null ],
      [ "GetPending", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a5f02a99faee92eb68b52a9cb49876c0e", null ],
      [ "ApplySingleMigration", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#add42626ceb43ee64238c5a1dd0d0d27f", null ],
      [ "RevertSingleMigration", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a416865e209264f27605aff0e547e7d66", null ],
      [ "ApplyPendingMigrations", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a9f6136aee1902543bdb428d6890574ee", null ],
      [ "CreateMigrationHistory", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#aa670c82bec993bd89189cb4a37007572", null ],
      [ "GetAppliedMigrationIds", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#ae7a7e92f9a2ef9cb11402812eaac3566", null ],
      [ "GetDataMapper", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a1869bb06cd3794f327182136cccff094", null ],
      [ "GetDataMapper", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a48ed86737153cbf627f517f3b0d11322", null ],
      [ "CloseDataMapper", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a424f1fa714b26c7b4b0a63a05dccc4b8", null ],
      [ "Transaction", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a53933c029c1d92dfc61c2301f6d1aa81", null ],
      [ "PreviewMigration", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a9b64d3bec74b85d054215a4321cbcad8", null ],
      [ "PreviewPendingMigrations", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a3b9961110aefc26051c06c6934e26e69", null ],
      [ "VerifyChecksums", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a63ce9461c4cd37394364f25f4e9f0cc8", null ],
      [ "MarkMigrationAsApplied", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a0ddf3ef0e6d833b829509ac769d04530", null ],
      [ "RevertToMigration", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a1dece4a4dc2811ac8cd8dce1c1130b11", null ],
      [ "GetMigrationStatus", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#aaaca2ec37763d11612d487ecad5daebf", null ],
      [ "ValidateDependencies", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#ab30c624d73149913fc06226b9d7b595a", null ],
      [ "RegisterRelease", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a542c4d9a99d7bbd8116e7c5f688be0bd", null ],
      [ "RemoveAllReleases", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a4a3c08e9053c7409a170abd9891e15bf", null ],
      [ "GetAllReleases", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#ad390ac8da8ac693c7357b67bc183bb60", null ],
      [ "FindReleaseByVersion", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a68b4434898a6ebceb06025065af79ec0", null ],
      [ "FindReleaseForTimestamp", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a97663a37d26d962c2c17d97706651c0f", null ],
      [ "GetMigrationsForRelease", "d4/d5c/classLightweight_1_1SqlMigration_1_1MigrationManager.html#a97918619adda32f2e9f65b089221a01f", null ]
    ] ],
    [ "Lightweight::SqlMigration::MigrationBase", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html", [
      [ "MigrationBase", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#a8ee0594832c46d4679a3db0a139b77b9", null ],
      [ "MigrationBase", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#ab7a7ef460d0a27d3104beb9c89757d28", null ],
      [ "operator=", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#af20606723a905996e482b453ed376886", null ],
      [ "Up", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#a08e7f32b9ae6147732e6cc89cf6ac35e", null ],
      [ "Down", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#abf76f7e1b5519cef9ca2aee981d9054b", null ],
      [ "HasDownImplementation", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#ae34f5e169eb26bf9318270631669f8d4", null ],
      [ "GetDependencies", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#a1af4ea7db6effd658b141e8d599978dc", null ],
      [ "GetAuthor", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#a2ec317eaae756e721addecb505082902", null ],
      [ "GetDescription", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#a4d9ec4afc996dacc72bbe5948aa7db73", null ],
      [ "GetTimestamp", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#a0ab6e8c26f230333862500cc5e39a03e", null ],
      [ "GetTitle", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#a120faae4809045b0fd412b6550b634f0", null ],
      [ "ComputeChecksum", "de/dfc/classLightweight_1_1SqlMigration_1_1MigrationBase.html#aa91e611caafae9e19e8850e786baf5c0", null ]
    ] ],
    [ "Lightweight::SqlMigration::MigrationMetadata", "dc/d0c/structLightweight_1_1SqlMigration_1_1MigrationMetadata.html", [
      [ "dependencies", "dc/d0c/structLightweight_1_1SqlMigration_1_1MigrationMetadata.html#aaacc540bd4fd3ab8f556b8cc92f2894b", null ],
      [ "author", "dc/d0c/structLightweight_1_1SqlMigration_1_1MigrationMetadata.html#ab52a61034e07facd43a641d9c40f9b62", null ],
      [ "description", "dc/d0c/structLightweight_1_1SqlMigration_1_1MigrationMetadata.html#aede084084d8c094a9f4e08d86b8f13f8", null ]
    ] ],
    [ "Lightweight::SqlMigration::MigrationLock", "d0/d8d/classLightweight_1_1SqlMigration_1_1MigrationLock.html", [
      [ "MigrationLock", "d0/d8d/classLightweight_1_1SqlMigration_1_1MigrationLock.html#ad96abad387505fd1f962993f60d91913", null ],
      [ "~MigrationLock", "d0/d8d/classLightweight_1_1SqlMigration_1_1MigrationLock.html#a2ac5fc5f8d975982e9f079eae1d2ad80", null ],
      [ "MigrationLock", "d0/d8d/classLightweight_1_1SqlMigration_1_1MigrationLock.html#a2af15418d7767a004d11f61094a6ba96", null ],
      [ "operator=", "d0/d8d/classLightweight_1_1SqlMigration_1_1MigrationLock.html#ab39b5e9a688a9f1b7ddd23a4c5fbe1c4", null ],
      [ "IsLocked", "d0/d8d/classLightweight_1_1SqlMigration_1_1MigrationLock.html#a097efefa7b59595476424596436760d8", null ],
      [ "Release", "d0/d8d/classLightweight_1_1SqlMigration_1_1MigrationLock.html#a23b43a71bb926aa5ef5e802576060a10", null ]
    ] ],
    [ "LIGHTWEIGHT_MIGRATION_PLUGIN", "db/df6/group__SqlMigration.html#gaef227c1f2230d962d19572a234ceaa73", null ],
    [ "LIGHTWEIGHT_MIGRATION_INSTANCE", "db/df6/group__SqlMigration.html#ga32d47d81e5b99a703e522a33a54fdc74", null ],
    [ "LIGHTWEIGHT_SQL_MIGRATION", "db/df6/group__SqlMigration.html#ga41dacd6fabd65f95ca0dfb209e7d187e", null ],
    [ "LIGHTWEIGHT_SQL_RELEASE", "db/df6/group__SqlMigration.html#ga91ef871053a7180297054840ef21a41a", null ]
];