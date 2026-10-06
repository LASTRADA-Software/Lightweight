// SPDX-License-Identifier: Apache-2.0
//
// Formatting helpers shared by the run progress readout and the run outcome
// banner, so a figure reads the same while a run is going and once it is done.

.pragma library

/// Compact count: 950 -> "950", 12_500 -> "13 k", 4_600_000 -> "4.6 M".
/// @param n Non-negative integer.
/// @return The formatted count.
function formatCount(n) {
    if (n >= 1000000)
        return (n / 1000000).toFixed(1) + " M"
    if (n >= 10000)
        return Math.round(n / 1000) + " k"
    return String(n)
}

/// Duration as prose: 800 -> "0.8 s", 41000 -> "41 s", 125000 -> "2 min 05 s".
/// @param ms Duration in milliseconds.
/// @return The formatted duration.
function formatDuration(ms) {
    if (ms < 1000)
        return (ms / 1000).toFixed(1) + " s"
    const total = Math.round(ms / 1000)
    if (total < 60)
        return total + " s"
    const seconds = total % 60
    return Math.floor(total / 60) + " min " + (seconds < 10 ? "0" : "") + seconds + " s"
}

/// Elapsed time as a clock: 23000 -> "00:23", 125000 -> "02:05".
/// @param ms Duration in milliseconds.
/// @return "mm:ss".
function clock(ms) {
    const total = Math.max(0, Math.floor(ms / 1000))
    const minutes = Math.floor(total / 60)
    const seconds = total % 60
    return (minutes < 10 ? "0" : "") + minutes + ":" + (seconds < 10 ? "0" : "") + seconds
}

/// File name of a path (the part after the last slash or backslash).
/// @param path A file path using either separator.
/// @return The last path component.
function baseName(path) {
    const parts = String(path).split(/[\\/]/)
    return parts[parts.length - 1]
}
