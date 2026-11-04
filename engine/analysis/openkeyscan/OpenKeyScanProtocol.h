#pragma once

#include "engine/theory/Key.h"

#include <juce_core/juce_core.h>

#include <optional>

/** NDJSON protocol of rekordcloud/openkeyscan-analyzer (pinned in models/manifest.json).
    Verified against openkeyscan_analyzer_server.py; see PREPROCESSING.md for the schema. */
namespace jaudios::analysis::openkeyscan
{

/** Upstream class index (0..23): camelot number = class % 12 + 1, 'A' if class < 12 else 'B'
    (openkeyscan_analyzer_server.py camelot_output; dataset.py CAMELOT_MAPPING). */
std::optional<theory::Key> keyFromClassIndex (int classIndex);
int classIndexFromKey (theory::Key);

struct Message
{
    enum class Kind { ready, heartbeat, success, error, other, unparseable };
    Kind kind = Kind::unparseable;
    juce::String id;
    theory::Key key;   // success
    int classId = -1;  // success
    juce::String error; // error
};

/** Parses one stdout line. A success whose class_id and camelot disagree becomes an error. */
Message parseMessage (const juce::String& line);

/** One request line (no trailing newline). The path must be absolute. */
juce::String makeRequest (const juce::String& id, const juce::File& absolutePath);

/** `-w 1 --device mps` on Apple Silicon, `--device cpu` elsewhere (skill: DirectML not validated). */
juce::StringArray defaultServerArguments();

} // namespace jaudios::analysis::openkeyscan
