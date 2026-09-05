var group__Retry =
[
    [ "Lightweight::SqlRetryClassifier", "db/d4d/classLightweight_1_1SqlRetryClassifier.html", [
      [ "~SqlRetryClassifier", "db/d4d/classLightweight_1_1SqlRetryClassifier.html#ad1cb796dc6a43390e66b5a02567ab5ac", null ],
      [ "Classify", "db/d4d/classLightweight_1_1SqlRetryClassifier.html#af55ee13a0a7cd88e4be1de0866cb049e", null ],
      [ "IsTransient", "db/d4d/classLightweight_1_1SqlRetryClassifier.html#a745d29742b7ff3e63916c8f21be20cc6", null ]
    ] ],
    [ "Lightweight::SqlRetrySettings", "d9/d7e/structLightweight_1_1SqlRetrySettings.html", [
      [ "maxRetries", "d9/d7e/structLightweight_1_1SqlRetrySettings.html#a8aa448f5c51023ccb5a68fad53f78e9d", null ],
      [ "initialDelay", "d9/d7e/structLightweight_1_1SqlRetrySettings.html#a0e9dbe911c17a0c3aea32eb5cbb9b356", null ],
      [ "backoffMultiplier", "d9/d7e/structLightweight_1_1SqlRetrySettings.html#a2fd9afb5b130603c232593485cee7c4a", null ],
      [ "maxDelay", "d9/d7e/structLightweight_1_1SqlRetrySettings.html#a509583cce773d9b887e14ec485b07dde", null ],
      [ "totalDelayBudget", "d9/d7e/structLightweight_1_1SqlRetrySettings.html#a5804ba7e2a88eaae1dc258a41a1c99eb", null ]
    ] ],
    [ "Lightweight::SqlRetryState", "da/def/structLightweight_1_1SqlRetryState.html", [
      [ "retriesSoFar", "da/def/structLightweight_1_1SqlRetryState.html#a0fc445febcec32eacd5275d181bca773", null ],
      [ "delaySoFar", "da/def/structLightweight_1_1SqlRetryState.html#ace917b8786d0a572f06ab81b0cfa05c0", null ]
    ] ],
    [ "Lightweight::SqlRetryDecision", "d4/d09/structLightweight_1_1SqlRetryDecision.html", [
      [ "operator bool", "d4/d09/structLightweight_1_1SqlRetryDecision.html#a2206b1e2f4343a39cbfd37b69be55528", null ],
      [ "action", "d4/d09/structLightweight_1_1SqlRetryDecision.html#a76891f1b3ada49fb39c40114e54c7799", null ],
      [ "delay", "d4/d09/structLightweight_1_1SqlRetryDecision.html#a6d8ff61090986966713b173d49c0fcc1", null ],
      [ "reason", "d4/d09/structLightweight_1_1SqlRetryDecision.html#ab734c7ed1c4085aeedb3ab4f245374b8", null ]
    ] ],
    [ "Lightweight::SqlRetryAttempt", "d6/d00/structLightweight_1_1SqlRetryAttempt.html", [
      [ "retryNumber", "d6/d00/structLightweight_1_1SqlRetryAttempt.html#aef554ecc759e7a9c6fab88c67637b23e", null ],
      [ "maxRetries", "d6/d00/structLightweight_1_1SqlRetryAttempt.html#ab28f5ce595ae93f6ca77940793642376", null ],
      [ "delay", "d6/d00/structLightweight_1_1SqlRetryAttempt.html#ac8a62f5ec1321f584b422968ea839f81", null ],
      [ "error", "d6/d00/structLightweight_1_1SqlRetryAttempt.html#ab9f8325956f217473c5a997674d0b87f", null ]
    ] ],
    [ "Lightweight::SqlRetrySleeper", "df/dc1/classLightweight_1_1SqlRetrySleeper.html", [
      [ "~SqlRetrySleeper", "df/dc1/classLightweight_1_1SqlRetrySleeper.html#abbb277e4363a15f6b62dfdecf34ee70a", null ],
      [ "Sleep", "df/dc1/classLightweight_1_1SqlRetrySleeper.html#a05eb7eeecfc236bce8fda5311b6ed37c", null ]
    ] ],
    [ "Lightweight::SqlRetryPolicy", "df/d49/classLightweight_1_1SqlRetryPolicy.html", [
      [ "RetryObserver", "df/d49/classLightweight_1_1SqlRetryPolicy.html#af00ac4ab739b78c22ff0af5061e71f31", null ],
      [ "SqlRetryPolicy", "df/d49/classLightweight_1_1SqlRetryPolicy.html#a34676e8137f5e2629e02e72511833152", null ],
      [ "SqlRetryPolicy", "df/d49/classLightweight_1_1SqlRetryPolicy.html#af205f1a6c81ed8ecea6abd51214d059c", null ],
      [ "Settings", "df/d49/classLightweight_1_1SqlRetryPolicy.html#ac0c6d548882c4336434d020265b14577", null ],
      [ "Classifier", "df/d49/classLightweight_1_1SqlRetryPolicy.html#afada65643617d53abb16d4352e99f604", null ],
      [ "SetRetryObserver", "df/d49/classLightweight_1_1SqlRetryPolicy.html#a4aa80edc122bc584d893aa68e6dfbbf7", null ],
      [ "DelayFor", "df/d49/classLightweight_1_1SqlRetryPolicy.html#a2a80d65d64c65e6329f2263f2d2c88f9", null ],
      [ "Decide", "df/d49/classLightweight_1_1SqlRetryPolicy.html#ae651a297dedb9e10e1cc29d1c7b11f7e", null ],
      [ "Execute", "df/d49/classLightweight_1_1SqlRetryPolicy.html#af7d7298bf71cb5196aafc25a9d015a8a", null ],
      [ "TryExecute", "df/d49/classLightweight_1_1SqlRetryPolicy.html#a3574a8084b1e14928fcdf0cd47c0623d", null ]
    ] ],
    [ "Lightweight::SqlErrorTransience", "d1/d7b/group__Retry.html#ga39565d3b352970a3546ce9cbaaca35f5", [
      [ "Lightweight::SqlErrorTransience::Permanent", "d1/d7b/group__Retry.html#gga39565d3b352970a3546ce9cbaaca35f5a23adaa457573eeb089c33214c90d3013", null ],
      [ "Lightweight::SqlErrorTransience::Transient", "d1/d7b/group__Retry.html#gga39565d3b352970a3546ce9cbaaca35f5ab1f023eff9a6b5308d6024e4c6b3d475", null ]
    ] ],
    [ "Lightweight::SqlRetryAction", "d1/d7b/group__Retry.html#ga342e14df587d6a9b4a42d34038e2fdc5", [
      [ "Lightweight::SqlRetryAction::Retry", "d1/d7b/group__Retry.html#gga342e14df587d6a9b4a42d34038e2fdc5a6327b4e59f58137083214a1fec358855", null ],
      [ "Lightweight::SqlRetryAction::GiveUp", "d1/d7b/group__Retry.html#gga342e14df587d6a9b4a42d34038e2fdc5abf9deec9cf8f3d92bff5d1edeff93206", null ]
    ] ],
    [ "Lightweight::SqlRetryGiveUpReason", "d1/d7b/group__Retry.html#ga736570debff7c5c20bb31ef7b87a75e2", [
      [ "Lightweight::SqlRetryGiveUpReason::None", "d1/d7b/group__Retry.html#gga736570debff7c5c20bb31ef7b87a75e2a6adf97f83acf6453d4a6a4b1070f3754", null ],
      [ "Lightweight::SqlRetryGiveUpReason::NotTransient", "d1/d7b/group__Retry.html#gga736570debff7c5c20bb31ef7b87a75e2a5ed55fc1039bbbea0a40df99a9535407", null ],
      [ "Lightweight::SqlRetryGiveUpReason::RetriesExhausted", "d1/d7b/group__Retry.html#gga736570debff7c5c20bb31ef7b87a75e2a4ff23e928f5a3fb19ee4768446d8da93", null ],
      [ "Lightweight::SqlRetryGiveUpReason::DelayBudgetExhausted", "d1/d7b/group__Retry.html#gga736570debff7c5c20bb31ef7b87a75e2a6aa9c1609189ce4ccd01f6755eea392b", null ]
    ] ],
    [ "Lightweight::GenericRetryOps", "d1/d7b/group__Retry.html#ga6fd64413db20720d706b6c63968c0f93", null ],
    [ "Lightweight::SqliteRetryOps", "d1/d7b/group__Retry.html#ga169666931dd9d9037e261abaee619621", null ],
    [ "Lightweight::SqlServerRetryOps", "d1/d7b/group__Retry.html#gae7cf652094d9a164d5e71dffeaf33207", null ],
    [ "Lightweight::PostgreSqlRetryOps", "d1/d7b/group__Retry.html#ga304db798a3dd0369141ba6c4122b68ee", null ],
    [ "Lightweight::ThreadSleeper", "d1/d7b/group__Retry.html#ga8a2c0b5b8193b4c3661ea48101a551a1", null ]
];