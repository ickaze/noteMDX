#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace notemdx {

enum class Severity { warning, error };

struct Diagnostic {
    Severity severity = Severity::error;
    std::string file;
    int line = 1;
    int column = 1;
    char channel = 0;
    std::string message;
};

// All strings containing MML, titles and PDX names use CP932 bytes.
// The host owns encoding conversion and file access. The compiler performs no I/O.
struct Source {
    std::string name;
    std::string text;
};

struct AuxiliaryFile {
    std::string name;   // CP932 file name requested by #save-*
    std::string parent; // logical source name for relative resolution
    std::vector<std::uint8_t> bytes;
};

struct Options {
    bool ex_pcm = false;
    bool reverse_octave = false;
    std::string muted_channels;
    // Complete file budget; 16-bit body offsets are validated separately.
    std::size_t max_output_bytes = 64 * 1024;
    std::size_t max_source_bytes = 8 * 1024 * 1024;
    std::size_t max_expanded_bytes = 16 * 1024 * 1024;
    // Return false and set error on failure. Resolve relative to 'parent'.
    std::function<bool(const std::string& requested, const std::string& parent,
                       Source& result, std::string& error)> include_loader;
    // Read a raw NOTE bank; expected sizes: tone 7168, wave 131968 bytes.
    std::function<bool(const std::string& requested, const std::string& parent,
                       std::vector<std::uint8_t>& bytes, std::string& error)> binary_loader;
    // Unset: follow MML directives. Set: override throughout all included sources.
    // compression: -1 disabled, 0 rests, 1 rests and tied notes.
    std::optional<int> compression;
    // Empty disables optimization; otherwise dvqpt012 or *.
    std::optional<std::string> optimization;
    // Optional NOTE -t/-w overrides (CP932). Relative paths use save_parent.
    std::optional<std::string> save_tone_filename, save_wave_filename;
    std::string save_parent; // empty: source.name
    bool save_banks_on_error = false; // NOTE -e; recovery_files, never partial MDX
    std::function<void(char)> progress; // host callback before each track
};

struct StepCount {
    std::uint64_t value = 0;
    bool overflow = false; // value saturates at UINT64_MAX
};
struct TrackStatistics {
    StepCount total_steps; // finite repeats expanded, one initial pass through L
    std::optional<StepCount> loop_steps; // first executed L/C to end; absent if none
};
struct PcmBankStatistics {
    int bank = 0;
    std::vector<int> used, unused; // note slots 0..95, not PDX availability
};
struct Statistics {
    std::string title; // CP932, as in the MDX header
    std::vector<int> fm_used, fm_unused; // emitted bank / other defined voices
    std::vector<PcmBankStatistics> pcm_banks; // only banks with a played note
    std::array<TrackStatistics,16> tracks; // ABCDEFGHPQRSTUVW
};
struct Result {
    std::vector<std::uint8_t> mdx;
    std::vector<Diagnostic> diagnostics;
    std::vector<AuxiliaryFile> auxiliary_files;
    std::vector<AuxiliaryFile> recovery_files; // requested banks at first error, only with -e
    Statistics statistics; // valid when ok() is true
    bool ok() const;
};

// Reentrant: each call owns its parser state; no mutable process-global data.
Result compile(const Source& source, const Options& options = Options());

} // namespace notemdx
