#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios
{

class JAudiosApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "JAudios"; }
    const juce::String getApplicationVersion() override { return JAUDIOS_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override { mainWindow = std::make_unique<MainWindow> (getApplicationName()); }
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
