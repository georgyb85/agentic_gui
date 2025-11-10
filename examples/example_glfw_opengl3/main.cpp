// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "candlestick_chart.h" // Added for new chart class
#include "NewsWindow.h" // Added for news window
#include "TimeSeriesWindow.h" // Added for time series window
#include "HistogramWindow.h" // Added for histogram window
#include "BivarAnalysisWidget.h" // Added for bivariate analysis widget
#include "ESSWindow.h" // Added for Enhanced Stepwise Selection window
#include "LFSWindow.h" // Added for Local Feature Selection window
#include "HMMTargetWindow.h"
#include "HMMMemoryWindow.h"
#include "StationarityWindow.h"
#include "FSCAWindow.h"
#include "SimulationWindowAdapter.h" // Adapter for new modular simulation window
#include "TradeSimulationWindow.h" // Trade simulation window
#include "Stage1ServerWindow.h"
#include "Stage1DatasetManager.h"
#include "IndicatorBuilderWindow.h"
#include <stdio.h>
#include "utils.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <ctime>
#include <future> // Added for std::async and std::future
#include <thread>  // Added for std::this_thread (potentially for sleep, or if std::async needs it implicitly)
#include <chrono>
#include <iomanip>
#include <sstream>

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

// Ensure we have OpenGL constants for multisampling
#if defined(_WIN32) && !defined(APIENTRY) && !defined(__CYGWIN__) && !defined(__SCITECH_SNAP__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE                    0x809D
#endif
#ifndef GL_SAMPLES
#define GL_SAMPLES                        0x80A9
#endif


// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

// static int find_hovered_bar_idx(...) // This function is now part of CandlestickChart class

struct CalendarDateTime {
    int year = 2025;
    int month = 1;  // 1-12
    int day = 1;    // 1-31
    int hour = 0;   // 0-23
    int minute = 0; // 0-59
    int second = 0; // 0-59
    bool show_time = false;
    
    std::time_t to_timestamp() const {
        std::tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second;
        return std::mktime(&tm);
    }
    
    std::string to_string() const {
        std::ostringstream oss;
        oss << year << "-" 
            << std::setfill('0') << std::setw(2) << month << "-" 
            << std::setfill('0') << std::setw(2) << day;
        if (show_time) {
            oss << " " 
                << std::setfill('0') << std::setw(2) << hour << ":" 
                << std::setfill('0') << std::setw(2) << minute << ":" 
                << std::setfill('0') << std::setw(2) << second;
        }
        return oss.str();
    }
    
private:
    bool is_leap_year(int y) const {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }
    
public:
    int days_in_month() const {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && is_leap_year(year)) return 29;
        return days[month - 1];
    }
};

class CompactCalendarWidget {
private:
    CalendarDateTime datetime;
    
    static const char* month_names[12];
    
public:
    bool Draw(const char* label) {
        bool changed = false;
        
        ImGui::PushID(label);
        
        // Year dropdown with +/- buttons
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("##year", &datetime.year, 1, 10)) {
            if (datetime.year < 1900) datetime.year = 1900;
            if (datetime.year > 2100) datetime.year = 2100;
            // Clamp day to valid range for new year
            int max_days = datetime.days_in_month();
            if (datetime.day > max_days) {
                datetime.day = max_days;
            }
            changed = true;
        }
        ImGui::SameLine();
        
        // Month dropdown
        ImGui::SetNextItemWidth(100);
        int month_index = datetime.month - 1;  // Convert to 0-based index
        if (ImGui::Combo("##month", &month_index, month_names, 12)) {
            datetime.month = month_index + 1;
            // Clamp day to valid range for new month
            int max_days = datetime.days_in_month();
            if (datetime.day > max_days) {
                datetime.day = max_days;
            }
            changed = true;
        }
        ImGui::SameLine();
        
        // Day dropdown
        ImGui::SetNextItemWidth(60);
        int max_days = datetime.days_in_month();
        int day_index = datetime.day - 1;  // Convert to 0-based index
        char day_preview[16];
        sprintf(day_preview, "%d", datetime.day);
        if (ImGui::BeginCombo("##day", day_preview)) {
            for (int i = 0; i < max_days; i++) {
                char day_label[8];
                sprintf(day_label, "%d", i + 1);
                bool is_selected = (day_index == i);
                if (ImGui::Selectable(day_label, is_selected)) {
                    datetime.day = i + 1;
                    changed = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        // Optional time controls
        if (ImGui::Checkbox("Show Time", &datetime.show_time)) {
            changed = true;
        }
        
        if (datetime.show_time) {
            // Hour, Minute, Second in one row with wider fields
            ImGui::SetNextItemWidth(70);
            if (ImGui::InputInt("##hour", &datetime.hour, 1, 1)) {
                if (datetime.hour < 0) datetime.hour = 0;
                if (datetime.hour > 23) datetime.hour = 23;
                changed = true;
            }
            ImGui::SameLine();
            
            ImGui::SetNextItemWidth(70);
            if (ImGui::InputInt("##minute", &datetime.minute, 1, 1)) {
                if (datetime.minute < 0) datetime.minute = 0;
                if (datetime.minute > 59) datetime.minute = 59;
                changed = true;
            }
            ImGui::SameLine();
            
            ImGui::SetNextItemWidth(70);
            if (ImGui::InputInt("##second", &datetime.second, 1, 1)) {
                if (datetime.second < 0) datetime.second = 0;
                if (datetime.second > 59) datetime.second = 59;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::Text("H:M:S");
        }
        
        ImGui::PopID();
        return changed;
    }
    
    const CalendarDateTime& GetDateTime() const { return datetime; }
    void SetDateTime(const CalendarDateTime& dt) { datetime = dt; }
};

const char* CompactCalendarWidget::month_names[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};


static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Main code
int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char* glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Enable anti-aliasing (MSAA 4x)
    glfwWindowHint(GLFW_SAMPLES, 4);

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Agentic Strategy Research", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Enable OpenGL multisampling (anti-aliasing)
    glEnable(GL_MULTISAMPLE);
    
    // Verify multisampling is supported
    int samples;
    glGetIntegerv(GL_SAMPLES, &samples);
    printf("Anti-aliasing: %d samples per pixel\n", samples);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    // Initialize simulation models for the new architecture
    simulation::InitializeSimulationModels();
    
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = true;
    bool show_implot_demo_window = true;
    bool show_another_window = false;
    bool show_calendar_window = true;
    // bool show_ohlcv_window = true; // This will be controlled by the CandlestickChart instance if needed, or removed
    // bool hide_empty_candles = false; // Moved to CandlestickChart
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    
    // OHLCV data storage - Moved to CandlestickChart
    // std::vector<OHLCVData> ohlcvData;
    // bool dataLoaded = false;
    
    // Fetch TSLA data for May 1-10, 2025
    time_t fromTime = dateToTimestamp(2025, 5, 1);
    time_t toTime = dateToTimestamp(2025, 5, 10);
    // static const char* timeframes[] = {"1m", "15m", "1h", "4h", "1d"}; // Moved to CandlestickChart
    // static int current_timeframe_idx = 0; // Moved to CandlestickChart
    // static std::string current_timeframe_str = "1m"; // Moved to CandlestickChart

    // Instantiate the CandlestickChart
    static CandlestickChart tslaChart("TSLA", fromTime, toTime);
    
    // Instantiate the NewsWindow
    static NewsWindow newsWindow;
    newsWindow.SetVisible(true); // Make it visible by default
    
    // Instantiate the TimeSeriesWindow
    static TimeSeriesWindow timeSeriesWindow;
    timeSeriesWindow.SetVisible(true); // Make it visible by default
    
    // Instantiate the HistogramWindow
    static HistogramWindow histogramWindow;
    histogramWindow.SetVisible(false); // Hidden by default, shown when data selected
    
    // Instantiate the BivarAnalysisWidget
    static BivarAnalysisWidget bivarAnalysisWidget;
    bivarAnalysisWidget.SetVisible(false); // Hidden by default, shown when requested
    
    // Instantiate the ESSWindow
    static ESSWindow essWindow;
    essWindow.SetVisible(false); // Hidden by default, shown when requested

    // Instantiate the LFSWindow
    static LFSWindow lfsWindow;
    lfsWindow.SetVisible(false); // Hidden by default, shown when requested

    // Instantiate HMM analysis windows
    static HMMTargetWindow hmmTargetWindow;
    hmmTargetWindow.SetVisible(false);

    static HMMMemoryWindow hmmMemoryWindow;
    hmmMemoryWindow.SetVisible(false);

    static StationarityWindow stationarityWindow;
    stationarityWindow.SetVisible(false);

    static FSCAWindow fscaWindow;
    fscaWindow.SetVisible(false);
    
    // Instantiate the SimulationWindow (new modular version via adapter)
    static SimulationWindow simulationWindow;
    simulationWindow.SetVisible(false); // Hidden by default, shown when requested
    
    
    // Instantiate the TradeSimulationWindow
    static TradeSimulationWindow tradeSimWindow;
    tradeSimWindow.SetCandlestickChart(&tslaChart);
    tradeSimWindow.SetSimulationWindow(&simulationWindow);
    tradeSimWindow.SetTimeSeriesWindow(&timeSeriesWindow);
    tradeSimWindow.SetVisible(false); // Hidden by default

    static Stage1ServerWindow stage1ServerWindow;
    stage1ServerWindow.SetVisible(false);
    static Stage1DatasetManager datasetManager;
    datasetManager.SetTimeSeriesWindow(&timeSeriesWindow);
    datasetManager.SetCandlestickChart(&tslaChart);
    datasetManager.SetVisible(false);
    static IndicatorBuilderWindow indicatorBuilderWindow;
    indicatorBuilderWindow.SetCandlestickChart(&tslaChart);
    indicatorBuilderWindow.SetVisible(false);
    
    // Connect TimeSeriesWindow with other widgets
    timeSeriesWindow.SetHistogramWindow(&histogramWindow);
    timeSeriesWindow.SetBivarAnalysisWidget(&bivarAnalysisWidget);
    timeSeriesWindow.SetESSWindow(&essWindow);
    timeSeriesWindow.SetLFSWindow(&lfsWindow);
    timeSeriesWindow.SetHMMTargetWindow(&hmmTargetWindow);
    timeSeriesWindow.SetHMMMemoryWindow(&hmmMemoryWindow);
    timeSeriesWindow.SetStationarityWindow(&stationarityWindow);
    timeSeriesWindow.SetFSCAWindow(&fscaWindow);
    simulationWindow.SetTimeSeriesWindow(&timeSeriesWindow);
    
    // Calendar widget instance
    static CompactCalendarWidget calendar;
    
    // Initialize calendar with current date (2025)
    static bool calendar_initialized = false;
    if (!calendar_initialized) {
        CalendarDateTime init_date;
        init_date.year = 2025;
        init_date.month = 8;  // August
        init_date.day = 9;    // Today
        calendar.SetDateTime(init_date);
        calendar_initialized = true;
    }

    // Set up news data once (not every frame for performance)
    std::vector<CandlestickChart::NewsEvent> news;
    {
        std::tm tm = {};
        tm.tm_year = 2025 - 1900;
        tm.tm_mon  = 4;    // May
        tm.tm_mday = 5;
        tm.tm_hour = 14;  // 2 PM
        tm.tm_min = 0;
        tm.tm_sec = 0;
        // Fix: Use _mkgmtime() instead of std::mktime() to match chart data timing (UTC)
        time_t t = _mkgmtime(&tm);
        news.push_back({ t, "news on TESLA stock" });
    }
    tslaChart.SetNewsSeries(news);

    // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!glfwWindowShouldClose(window))
#endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Main menu bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Demo Window", NULL, &show_demo_window);
                ImGui::MenuItem("ImPlot Demo", NULL, &show_implot_demo_window);
                ImGui::MenuItem("Another Window", NULL, &show_another_window);
                
                bool show_news_window = newsWindow.IsVisible();
                if (ImGui::MenuItem("News Window", NULL, &show_news_window)) {
                    newsWindow.SetVisible(show_news_window);
                }
                
                bool show_timeseries_window = timeSeriesWindow.IsVisible();
                if (ImGui::MenuItem("Time Series Window", NULL, &show_timeseries_window)) {
                    timeSeriesWindow.SetVisible(show_timeseries_window);
                }

                bool show_indicator_builder = indicatorBuilderWindow.IsVisible();
                if (ImGui::MenuItem("Indicator Builder", NULL, &show_indicator_builder)) {
                    indicatorBuilderWindow.SetVisible(show_indicator_builder);
                }
                
                bool show_histogram_window = histogramWindow.IsVisible();
                if (ImGui::MenuItem("Histogram Window", NULL, &show_histogram_window)) {
                    histogramWindow.SetVisible(show_histogram_window);
                }

                bool show_hmm_target = hmmTargetWindow.IsVisible();
                if (ImGui::MenuItem("HMM Target Correlation", NULL, &show_hmm_target)) {
                    hmmTargetWindow.SetVisible(show_hmm_target);
                }

                bool show_hmm_memory = hmmMemoryWindow.IsVisible();
                if (ImGui::MenuItem("HMM Memory Test", NULL, &show_hmm_memory)) {
                    hmmMemoryWindow.SetVisible(show_hmm_memory);
                }

                bool show_stationarity = stationarityWindow.IsVisible();
                if (ImGui::MenuItem("Stationarity Test", NULL, &show_stationarity)) {
                    stationarityWindow.SetVisible(show_stationarity);
                }

                bool show_fsca = fscaWindow.IsVisible();
                if (ImGui::MenuItem("FSCA", NULL, &show_fsca)) {
                    fscaWindow.SetVisible(show_fsca);
                }

                bool show_simulation_window = simulationWindow.IsVisible();
                if (ImGui::MenuItem("Trading Simulation", NULL, &show_simulation_window)) {
                    simulationWindow.SetVisible(show_simulation_window);
                }
                
                
                bool show_trade_sim = tradeSimWindow.IsVisible();
                if (ImGui::MenuItem("Trade Simulation", NULL, &show_trade_sim)) {
                    tradeSimWindow.SetVisible(show_trade_sim);
                }

                bool show_dataset_manager = datasetManager.IsVisible();
                if (ImGui::MenuItem("Dataset Manager", NULL, &show_dataset_manager)) {
                    datasetManager.SetVisible(show_dataset_manager);
                }

                bool show_stage1_db = stage1ServerWindow.IsVisible();
                if (ImGui::MenuItem("Stage1 Server Debugger", NULL, &show_stage1_db)) {
                    stage1ServerWindow.SetVisible(show_stage1_db);
                }
                
                ImGui::MenuItem("Calendar Widget", NULL, &show_calendar_window);
                
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        if (show_implot_demo_window) // <--- ADD THIS BLOCK
        {
            ImPlot::ShowDemoWindow(&show_implot_demo_window);
        }

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);
            
            // Add menu item for News Window
            bool show_news_window = newsWindow.IsVisible();
            if (ImGui::Checkbox("News Window", &show_news_window)) {
                newsWindow.SetVisible(show_news_window);
            }
            
            // Add menu item for Time Series Window
            bool show_timeseries_window = timeSeriesWindow.IsVisible();
            if (ImGui::Checkbox("Time Series Window", &show_timeseries_window)) {
                timeSeriesWindow.SetVisible(show_timeseries_window);
            }

            bool show_indicator_builder = indicatorBuilderWindow.IsVisible();
            if (ImGui::Checkbox("Indicator Builder", &show_indicator_builder)) {
                indicatorBuilderWindow.SetVisible(show_indicator_builder);
            }
            
            // Add menu item for Histogram Window
            bool show_histogram_window = histogramWindow.IsVisible();
            if (ImGui::Checkbox("Histogram Window", &show_histogram_window)) {
                histogramWindow.SetVisible(show_histogram_window);
            }

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }
        
        // Render the CandlestickChart instance
        tslaChart.Render();
        
        // Render the NewsWindow instance
        newsWindow.Draw();
        
        // Render the TimeSeriesWindow instance
        timeSeriesWindow.Draw();

        // Render the Indicator Builder window
        indicatorBuilderWindow.Draw();
        
        // Render the HistogramWindow instance
        histogramWindow.Draw();
        
        // Render the BivarAnalysisWidget instance
        bivarAnalysisWidget.Draw();
        
        // Render the ESSWindow instance
        essWindow.Draw();
        lfsWindow.Draw();
        hmmTargetWindow.Draw();
        hmmMemoryWindow.Draw();
        stationarityWindow.Draw();
        fscaWindow.Draw();

        // Render the SimulationWindow instance
        simulationWindow.Draw();
        
        
        // Render the TradeSimulationWindow
        tradeSimWindow.Draw();
        stage1ServerWindow.Draw();
        datasetManager.Draw();
        
        // Calendar test window
        if (show_calendar_window) {
            ImGui::Begin("Calendar Widget Test", &show_calendar_window);
            
            ImGui::Text("Compact Calendar Widget:");
            ImGui::Separator();
            
            bool date_changed = calendar.Draw("calendar");
            
            ImGui::Separator();
            ImGui::Text("Selected Date:");
            
            const CalendarDateTime& dt = calendar.GetDateTime();
            std::string date_str = dt.to_string();
            std::time_t timestamp = dt.to_timestamp();
            
            ImGui::Text("String: %s", date_str.c_str());
            ImGui::Text("Unix Timestamp: %lld", (long long)timestamp);
            
            // Convert back to readable date for verification (use static buffer to avoid glitches)
            static char verification_buffer[100] = "";
            static std::time_t last_timestamp = 0;
            
            // Only update verification when timestamp actually changes
            if (timestamp != last_timestamp) {
                std::tm* tm_info = std::localtime(&timestamp);
                strftime(verification_buffer, sizeof(verification_buffer), "%Y-%m-%d %H:%M:%S", tm_info);
                last_timestamp = timestamp;
            }
            
            ImGui::Text("Verification: %s", verification_buffer);
            
            if (date_changed) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Date updated!");
            }
            
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
