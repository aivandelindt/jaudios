#include "OpenKeyScanProtocol.h"

namespace jaudios::analysis::openkeyscan
{

std::optional<theory::Key> keyFromClassIndex (int classIndex)
{
    if (classIndex < 0 || classIndex > 23)
        return std::nullopt;
    const auto camelot = juce::String (classIndex % 12 + 1) + (classIndex < 12 ? "A" : "B");
    return theory::keyFromCamelot (camelot.toStdString());
}

int classIndexFromKey (theory::Key key)
{
    return theory::camelotNumber (key) - 1 + (key.mode == theory::Mode::major ? 12 : 0);
}

Message parseMessage (const juce::String& line)
{
    Message m;
    juce::var json;
    if (juce::JSON::parse (line.trim(), json).failed() || ! json.isObject())
        return m; // unparseable (library chatter on stdout etc.)

    const auto type = json["type"].toString();
    if (type == "ready")     { m.kind = Message::Kind::ready; return m; }
    if (type == "heartbeat") { m.kind = Message::Kind::heartbeat; return m; }

    m.id = json["id"].toString();
    const auto status = json["status"].toString();

    if (status == "error")
    {
        m.kind = Message::Kind::error;
        m.error = json["error"].toString();
        return m;
    }

    if (status == "success")
    {
        const auto cls = json.getProperty ("class_id", -1);
        const auto key = cls.isInt() || cls.isInt64() || cls.isDouble() ? keyFromClassIndex ((int) cls) : std::nullopt;
        const auto camelot = json["camelot"].toString();

        if (! key || (camelot.isNotEmpty() && camelot.toStdString() != theory::camelot (*key)))
        {
            m.kind = Message::Kind::error;
            m.error = "Inconsistent response (class_id " + cls.toString() + ", camelot " + camelot + ")";
            return m;
        }

        m.kind = Message::Kind::success;
        m.classId = (int) cls;
        m.key = *key;
        return m;
    }

    m.kind = Message::Kind::other; // reset_complete / reset_idle / future types
    return m;
}

juce::String makeRequest (const juce::String& id, const juce::File& absolutePath)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("path", absolutePath.getFullPathName());
    return juce::JSON::toString (juce::var (o), true); // all on one line
}

juce::StringArray defaultServerArguments()
{
   #if JUCE_MAC && JUCE_ARM
    return { "-w", "1", "--device", "mps" };
   #else
    return { "-w", "1", "--device", "cpu" };
   #endif
}

} // namespace jaudios::analysis::openkeyscan
