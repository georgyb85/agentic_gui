#
# Cross Platform Makefile
# Compatible with MSYS2/MINGW, Ubuntu 14.04.1 and Mac OS X
#
# You will need GLFW (http://www.glfw.org):
# Linux:
#   apt-get install libglfw-dev
# Mac OS X:
#   brew install glfw
# MSYS2:
#   pacman -S --noconfirm --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-glfw
#

#CXX = g++
#CXX = clang++

EXE = example_glfw_opengl3
IMGUI_DIR = ../..
SOURCES = main.cpp utils.cpp candlestick_chart.cpp NewsWindow.cpp TickerSelector.cpp implot_items.cpp implot_custom_plotters.cpp \
          TimeSeriesWindow.cpp IndicatorBuilderWindow.cpp Stage1RestClient.cpp HistogramWindow.cpp BivarAnalysisWidget.cpp ESSWindow.cpp LFSWindow.cpp HMMTargetWindow.cpp HMMMemoryWindow.cpp StationarityWindow.cpp FSCAWindow.cpp FeatureSelectorWidget.cpp \
          analytics_dataframe.cpp chronosflow.cpp dataframe_io.cpp tssb_timestamp.cpp \
          feature_utils.cpp bivariate_analysis_exact.cpp modern_algorithms.cpp modern_discretizer.cpp \
          SimpleTradeExecutor.cpp \
          QuestDbExports.cpp QuestDbDataFrameGateway.cpp QuestDbImports.cpp RunConfigSerializer.cpp Stage1ServerWindow.cpp Stage1DatasetManager.cpp Stage1MetadataReader.cpp \
          modern_indicators/src/IndicatorConfig.cpp modern_indicators/src/IndicatorEngine.cpp modern_indicators/src/IndicatorId.cpp \
          modern_indicators/src/MathUtils.cpp modern_indicators/src/MultiIndicatorLibrary.cpp modern_indicators/src/SingleIndicatorLibrary.cpp \
          modern_indicators/src/TaskExecutor.cpp modern_indicators/src/validation/DataParsers.cpp \
          modern_indicators/src/helpers/Fti.cpp modern_indicators/src/helpers/InformationTheory.cpp modern_indicators/src/helpers/Janus.cpp modern_indicators/src/helpers/WaveletHelpers.cpp \
          stepwise/enhanced_stepwise.cpp stepwise/enhanced_stepwise_selector.cpp \
          stepwise/data_matrix.cpp stepwise/cross_validator.cpp stepwise/linear_quadratic_model.cpp \
          stepwise/monte_carlo_permutation_test.cpp stepwise/modern_svd.cpp stepwise/memory_pool.cpp \
          stepwise/stepwise_data_reader.cpp \
          stepwise/xgboost_config_widget.cpp stepwise/linear_quadratic_model_wrapper.cpp \
          stepwise/xgboost_model.cpp stepwise/enhanced_stepwise_selector_v2.cpp \
          stepwise/enhanced_stepwise_v2.cpp \
          hmm/HmmModel.cpp hmm/HmmTargetCorrelation.cpp hmm/HmmMemoryTest.cpp \
          stationarity/MeanBreakTest.cpp fsca/FscaAnalyzer.cpp \
          simulation/PerformanceStressTests.cpp \
          $(IMGUI_DIR)/misc/cpp/imgui_stdlib.cpp
# Include new simulation architecture files
include simulation/Makefile.include
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))
UNAME_S := $(shell uname -s)
LINUX_GL_LIBS = -lGL

CXXFLAGS = -std=c++20 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -Imodern_indicators/include
CXXFLAGS += -I./eigen-3.4.0 -I./stepwise -I./stepwise/bivar -I./hmm -I./stationarity -I./fsca
CXXFLAGS += -g -Wall -Wformat
LIBS =

##---------------------------------------------------------------------
## OPENGL ES
##---------------------------------------------------------------------

## This assumes a GL ES library available in the system, e.g. libGLESv2.so
# CXXFLAGS += -DIMGUI_IMPL_OPENGL_ES2
# LINUX_GL_LIBS = -lGLESv2

##---------------------------------------------------------------------
## BUILD FLAGS PER PLATFORM
##---------------------------------------------------------------------

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += $(LINUX_GL_LIBS) `pkg-config --static --libs glfw3`
	LIBS += -lcurl

	CXXFLAGS += `pkg-config --cflags glfw3`
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
	LIBS += -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib
	#LIBS += -lglfw3
	LIBS += -lglfw
	LIBS += -lcurl

	CXXFLAGS += -I/usr/local/include -I/opt/local/include -I/opt/homebrew/include
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(OS), Windows_NT)
	ECHO_MESSAGE = "MinGW"
	LIBS += -lglfw3 -lgdi32 -lopengl32 -limm32
	LIBS += -lcurl

	CXXFLAGS += `pkg-config --cflags glfw3`
	CFLAGS = $(CXXFLAGS)
endif

##---------------------------------------------------------------------
## BUILD RULES
##---------------------------------------------------------------------

%.o:%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:stepwise/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

all: $(EXE)
	@echo Build complete for $(ECHO_MESSAGE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

clean:
	rm -f $(EXE) $(OBJS)
