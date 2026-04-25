// SPDX-License-Identifier: Apache-2.0

#include "SplitFileWriter.hpp"

#include <format>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace Lightweight::CodeGen
{

std::vector<std::vector<CodeBlock>> GroupBlocksByLineBudget(std::vector<CodeBlock> const& blocks,
                                                            std::size_t maxLinesPerFile)
{
    std::vector<std::vector<CodeBlock>> chunks;
    if (blocks.empty())
        return chunks;

    if (maxLinesPerFile == 0)
    {
        chunks.emplace_back(blocks);
        return chunks;
    }

    chunks.emplace_back();
    std::size_t currentLines = 0;
    for (auto const& block: blocks)
    {
        if (!chunks.back().empty() && currentLines + block.lineCount > maxLinesPerFile)
        {
            chunks.emplace_back();
            currentLines = 0;
        }
        chunks.back().push_back(block);
        currentLines += block.lineCount;
    }
    if (chunks.back().empty())
        chunks.pop_back();
    return chunks;
}

namespace
{
    /// @brief Ensures the parent directory exists so subsequent `ofstream` opens succeed.
    void EnsureParentDirectoryExists(std::filesystem::path const& filePath)
    {
        auto const parent = filePath.parent_path();
        if (parent.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec)
            throw std::runtime_error(std::format("Failed to create directory {}: {}", parent.string(), ec.message()));
    }

    /// @brief Writes one chunk file with optional header/footer.
    void WriteChunkFile(std::filesystem::path const& path,
                        std::vector<CodeBlock> const& chunk,
                        std::string_view header,
                        std::string_view footer)
    {
        EnsureParentDirectoryExists(path);
        std::ofstream out(path);
        if (!out.is_open())
            throw std::runtime_error(std::format("Failed to open output file: {}", path.string()));
        if (!header.empty())
            out << header;
        for (auto const& block: chunk)
            out << block.content;
        if (!footer.empty())
            out << footer;
    }

    /// @brief Total line count of all blocks; cached on the `CodeBlock` so this loop
    /// is just a sum, not a per-call newline scan.
    [[nodiscard]] std::size_t TotalLines(std::vector<CodeBlock> const& blocks)
    {
        std::size_t total = 0;
        for (auto const& b: blocks)
            total += b.lineCount;
        return total;
    }
} // namespace

WriteResult EmitChunked(std::filesystem::path const& outputPath,
                        std::vector<CodeBlock> const& blocks,
                        std::size_t maxLinesPerFile,
                        std::string_view fileHeader,
                        std::string_view fileFooter)
{
    WriteResult result;

    if (maxLinesPerFile == 0 || TotalLines(blocks) <= maxLinesPerFile)
    {
        WriteChunkFile(outputPath, blocks, fileHeader, fileFooter);
        result.writtenFiles.push_back(outputPath);
        return result;
    }

    auto const chunks = GroupBlocksByLineBudget(blocks, maxLinesPerFile);
    auto const stem = outputPath.parent_path() / outputPath.stem();
    auto const ext = outputPath.extension().string();

    for (std::size_t i = 0; i < chunks.size(); ++i)
    {
        auto partPath = std::filesystem::path { std::format("{}_part{:02}{}", stem.string(), i + 1, ext) };
        WriteChunkFile(partPath, chunks[i], fileHeader, fileFooter);
        result.writtenFiles.push_back(std::move(partPath));
    }

    return result;
}

void EmitPluginCmake(std::filesystem::path const& outputDir,
                     std::string_view pluginName,
                     std::string_view sourceGlob)
{
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec)
        throw std::runtime_error(std::format("Failed to create directory {}: {}", outputDir.string(), ec.message()));

    auto const cmakePath = outputDir / "CMakeLists.txt";
    {
        std::ofstream out(cmakePath);
        if (!out.is_open())
            throw std::runtime_error(std::format("Failed to open output file: {}", cmakePath.string()));
        out << "# SPDX-License-Identifier: Apache-2.0\n"
            << "# Auto-generated. DO NOT EDIT.\n"
            << "\n"
            << "cmake_minimum_required(VERSION 3.25)\n"
            << "\n"
            << "# Pick up every generated migration source. CONFIGURE_DEPENDS makes CMake re-glob\n"
            << "# when the generator regenerates the directory, so new sources enter the build\n"
            << "# without a manual reconfigure.\n"
            << "file(GLOB " << pluginName << "_MIGRATIONS CONFIGURE_DEPENDS\n"
            << "    \"${CMAKE_CURRENT_SOURCE_DIR}/" << sourceGlob << "\"\n"
            << ")\n"
            << "\n"
            << "add_library(" << pluginName << " MODULE\n"
            << "    Plugin.cpp\n"
            << "    ${" << pluginName << "_MIGRATIONS}\n"
            << ")\n"
            << "\n"
            << "target_link_libraries(" << pluginName << " PRIVATE Lightweight::Lightweight)\n"
            << "\n"
            << "set_target_properties(" << pluginName << " PROPERTIES\n"
            << "    LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/plugins\"\n"
            << "    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/plugins\"\n"
            << "    PREFIX \"\"\n"
            << ")\n"
            << "\n"
            << "# Generated migration sources are large and intentionally literal; skip clang-tidy\n"
            << "# so lint thresholds (function size, cognitive complexity) don't trip on them.\n"
            << "set_target_properties(" << pluginName << " PROPERTIES CXX_CLANG_TIDY \"\")\n"
            << "set_source_files_properties(${" << pluginName << "_MIGRATIONS} PROPERTIES SKIP_LINTING TRUE)\n";
    }

    auto const pluginPath = outputDir / "Plugin.cpp";
    {
        std::ofstream out(pluginPath);
        if (!out.is_open())
            throw std::runtime_error(std::format("Failed to open output file: {}", pluginPath.string()));
        out << "// SPDX-License-Identifier: Apache-2.0\n"
            << "// Auto-generated. DO NOT EDIT.\n"
            << "//\n"
            << "// Migration plugin entry point. Individual migrations self-register with the\n"
            << "// MigrationManager via static initialization; this file only exposes the\n"
            << "// plugin ABI that dbtool expects when dlopen'ing the shared module.\n"
            << "\n"
            << "#include <Lightweight/SqlMigration.hpp>\n"
            << "\n"
            << "LIGHTWEIGHT_MIGRATION_PLUGIN()\n";
    }
}

} // namespace Lightweight::CodeGen
