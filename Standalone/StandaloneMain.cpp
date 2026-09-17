#include <JuceHeader.h>
#include "StandaloneWindow.h"
#include "../source/network/UpdateChecker.h"

#include <atomic>

// ============================================================================
//  平台标识（与遥测 / 插件侧口径一致，<os>-<arch>）
// ============================================================================
namespace
{
    juce::String getUpdatePlatformString()
    {
    #if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || defined(__arm64__)
        const juce::String arch = "arm64";
    #elif defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
        const juce::String arch = "x64";
    #else
        const juce::String arch = "x86";
    #endif

    #if JUCE_WINDOWS
        const juce::String os = "win";
    #elif JUCE_MAC
        const juce::String os = "mac";
    #elif JUCE_LINUX
        const juce::String os = "linux";
    #elif JUCE_BSD
        const juce::String os = "bsd";
    #else
        const juce::String os = "unknown";
    #endif

        return os + "-" + arch;
    }
}

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
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> ("SpectrumTag");

        // 启动后延迟 5 秒检查一次更新（进程级去重；Standalone 不创建 Processor，
        // 因此在应用入口单独触发一次）
        static std::atomic<bool> updateOnceFlag { false };
        if (! updateOnceFlag.exchange (true, std::memory_order_acquire))
        {
            juce::Timer::callAfterDelay (5000, []
            {
                spectrumtag::network::CheckForUpdatesAsync (
                    "spectrumtag",
                    juce::String (ProjectInfo::versionString),
                    getUpdatePlatformString(),
                    [] (const spectrumtag::network::UpdateInfo& info)
                    {
                        if (info.has_update)
                            spectrumtag::network::ShowUpdateDialog (info);
                    });
            });
        }
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
