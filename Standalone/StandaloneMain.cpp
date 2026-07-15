#include <JuceHeader.h>
#include "StandaloneWindow.h"

// ============================================================================
//  SpectrumTagStandaloneApplication
// ----------------------------------------------------------------------------
//  独立可执行程序入口。行为：
//   - 创建一个可缩放的主窗口，内容为 SpectrumTagMainComponent
//   - 关闭窗口即退出程序
// ============================================================================
class SpectrumTagStandaloneApplication : public juce::JUCEApplication
{
public:
    SpectrumTagStandaloneApplication() = default;

    const juce::String getApplicationName() override       { return "SpectrumTag"; }
    const juce::String getApplicationVersion() override    { return "1.2.9"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> ("SpectrumTag");
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : juce::DocumentWindow (name,
                                    juce::Colour (0xff1a1c1f),
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);

            auto* content = new SpectrumTagMainComponent();
            setContentOwned (content, true);

            // 允许缩放；宽高比锁定为默认设计比例，最小尺寸不小于 800x500
            setResizable (true, true);
            const int w = content->getWidth();
            const int h = content->getHeight();
            const double aspect = (double) w / (double) h;
            constrainer.setFixedAspectRatio (aspect);
            constrainer.setSizeLimits (800, 500, 4096, 4096);
            setConstrainer (&constrainer);

            centreWithSize (w, h);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        juce::ComponentBoundsConstrainer constrainer;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (SpectrumTagStandaloneApplication)
