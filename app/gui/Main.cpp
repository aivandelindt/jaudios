#include "MainComponent.h"

#include "app/core/SelfTest.h"

#include <iostream>

#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios
{

class JAudiosApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "JAudios"; }
    const juce::String getApplicationVersion() override { return JAUDIOS_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        // `JAudios --self-test [file]`: the release smoke test, no window (docs/releasing.md)
        const auto args = getCommandLineParameterArray();
        if (const int at = args.indexOf ("--self-test"); at >= 0)
        {
            const auto file = at + 1 < args.size() ? juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 1].unquoted()) : juce::File();
            bool ok = false;
            std::cout << juce::JSON::toString (runSelfTest (file, ok)) << std::endl;
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // "Open with" / command line: first argument that is an existing file.
        for (const auto& arg : getCommandLineParameterArray())
            if (const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (arg.unquoted()); file.existsAsFile())
            {
                if (auto* content = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    content->importFile (file);
                break;
            }
        juce::ignoreUnused (commandLine);
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Desktop::getInstance().getDefaultLookAndFeel()
                                        .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace jaudios

START_JUCE_APPLICATION (jaudios::JAudiosApplication)
