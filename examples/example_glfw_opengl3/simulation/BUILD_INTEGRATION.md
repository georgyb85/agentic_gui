# Build System Integration

## Makefile Updates

Add to the SOURCES list in Makefile:

```makefile
# Simulation refactored components
SOURCES += simulation/SimulationTypes.cpp
SOURCES += simulation/SimulationUtils.cpp
SOURCES += simulation/models/XGBoostModel.cpp
SOURCES += simulation/models/ModelFactory.cpp
SOURCES += simulation/models/ModelCache.cpp
SOURCES += simulation/core/SimulationEngine.cpp
SOURCES += simulation/ui/SimulationConfigWidget.cpp
SOURCES += simulation/ui/SimulationResultsWidget.cpp
SOURCES += simulation/ui/SimulationControlsWidget.cpp
# Keep old one during migration
SOURCES += SimulationWindow.cpp
# Or use new one after migration
# SOURCES += simulation/SimulationWindow.cpp
```

Add to include paths:
```makefile
CXXFLAGS += -I./simulation
```

## Visual Studio Project Updates

### Add Filter Structure
In `example_glfw_opengl3.vcxproj.filters`:

```xml
<ItemGroup>
  <Filter Include="simulation">
    <UniqueIdentifier>{generate-new-guid}</UniqueIdentifier>
  </Filter>
  <Filter Include="simulation\core">
    <UniqueIdentifier>{generate-new-guid}</UniqueIdentifier>
  </Filter>
  <Filter Include="simulation\models">
    <UniqueIdentifier>{generate-new-guid}</UniqueIdentifier>
  </Filter>
  <Filter Include="simulation\ui">
    <UniqueIdentifier>{generate-new-guid}</UniqueIdentifier>
  </Filter>
</ItemGroup>
```

### Add Header Files
```xml
<ItemGroup>
  <!-- Core headers -->
  <ClInclude Include="simulation\SimulationTypes.h">
    <Filter>simulation\core</Filter>
  </ClInclude>
  <ClInclude Include="simulation\SimulationUtils.h">
    <Filter>simulation\core</Filter>
  </ClInclude>
  <ClInclude Include="simulation\SimulationEngine.h">
    <Filter>simulation\core</Filter>
  </ClInclude>
  
  <!-- Model headers -->
  <ClInclude Include="simulation\ISimulationModel.h">
    <Filter>simulation\models</Filter>
  </ClInclude>
  <ClInclude Include="simulation\XGBoostModel.h">
    <Filter>simulation\models</Filter>
  </ClInclude>
  <ClInclude Include="simulation\XGBoostConfig.h">
    <Filter>simulation\models</Filter>
  </ClInclude>
  <ClInclude Include="simulation\ModelFactory.h">
    <Filter>simulation\models</Filter>
  </ClInclude>
  <ClInclude Include="simulation\ModelCache.h">
    <Filter>simulation\models</Filter>
  </ClInclude>
  
  <!-- UI headers -->
  <ClInclude Include="simulation\ui\SimulationConfigWidget.h">
    <Filter>simulation\ui</Filter>
  </ClInclude>
  <ClInclude Include="simulation\ui\SimulationResultsWidget.h">
    <Filter>simulation\ui</Filter>
  </ClInclude>
  <ClInclude Include="simulation\ui\SimulationControlsWidget.h">
    <Filter>simulation\ui</Filter>
  </ClInclude>
</ItemGroup>
```

### Add Source Files
```xml
<ItemGroup>
  <!-- Core sources -->
  <ClCompile Include="simulation\SimulationTypes.cpp">
    <Filter>simulation\core</Filter>
  </ClCompile>
  <ClCompile Include="simulation\SimulationUtils.cpp">
    <Filter>simulation\core</Filter>
  </ClCompile>
  <ClCompile Include="simulation\SimulationEngine.cpp">
    <Filter>simulation\core</Filter>
  </ClCompile>
  
  <!-- Model sources -->
  <ClCompile Include="simulation\models\XGBoostModel.cpp">
    <Filter>simulation\models</Filter>
  </ClCompile>
  <ClCompile Include="simulation\models\ModelFactory.cpp">
    <Filter>simulation\models</Filter>
  </ClCompile>
  <ClCompile Include="simulation\models\ModelCache.cpp">
    <Filter>simulation\models</Filter>
  </ClCompile>
  
  <!-- UI sources -->
  <ClCompile Include="simulation\ui\SimulationConfigWidget.cpp">
    <Filter>simulation\ui</Filter>
  </ClCompile>
  <ClCompile Include="simulation\ui\SimulationResultsWidget.cpp">
    <Filter>simulation\ui</Filter>
  </ClCompile>
  <ClCompile Include="simulation\ui\SimulationControlsWidget.cpp">
    <Filter>simulation\ui</Filter>
  </ClCompile>
</ItemGroup>
```

## Include Path Configuration

### For Visual Studio
Add to project settings:
- Project Properties → C/C++ → General → Additional Include Directories
- Add: `$(ProjectDir)simulation`

### For CMake (if using)
```cmake
target_include_directories(example_glfw_opengl3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/simulation
)

# Add source files
set(SIMULATION_SOURCES
    simulation/SimulationTypes.cpp
    simulation/SimulationUtils.cpp
    simulation/models/XGBoostModel.cpp
    simulation/models/ModelFactory.cpp
    simulation/models/ModelCache.cpp
    simulation/core/SimulationEngine.cpp
    simulation/ui/SimulationConfigWidget.cpp
    simulation/ui/SimulationResultsWidget.cpp
    simulation/ui/SimulationControlsWidget.cpp
)

target_sources(example_glfw_opengl3 PRIVATE ${SIMULATION_SOURCES})
```

## Compilation Order

To avoid dependency issues, compile in this order:
1. SimulationTypes.cpp (no dependencies)
2. SimulationUtils.cpp (depends on Types)
3. ISimulationModel.h (header only)
4. ModelCache.cpp (depends on IModel)
5. XGBoostConfig.h (header only)
6. XGBoostModel.cpp (depends on all above)
7. ModelFactory.cpp (depends on models)
8. SimulationEngine.cpp (depends on models)
9. UI widgets (depend on engine and types)
10. SimulationWindow.cpp (depends on everything)

## Gradual Migration Strategy

### Phase 1: Add new files without removing old
- Keep original SimulationWindow.cpp/h
- Add all new files to project
- Implement new components using code copied from original

### Phase 2: Create adapter
- Make SimulationWindow delegate to new components
- Test that everything still works

### Phase 3: Switch over
- Replace SimulationWindow with new implementation
- Remove old code

## Preprocessor Defines

If you need conditional compilation during migration:

```cpp
// In SimulationWindow.cpp
#ifdef USE_REFACTORED_SIMULATION
    #include "simulation/SimulationWindow.h"
#else
    // Original implementation
#endif
```

Add to build system:
```makefile
# Enable when ready to switch
CXXFLAGS += -DUSE_REFACTORED_SIMULATION
```

## Testing Build

Test compilation of new structure:
```bash
# Linux/Mac
make clean
make -j4

# Windows (Visual Studio)
msbuild /t:Clean
msbuild /p:Configuration=Release /p:Platform=x64
```

## Troubleshooting

### Linker Errors
- Ensure all .cpp files are added to project
- Check that XGBoost library is still linked

### Include Errors  
- Verify simulation/ is in include path
- Check relative paths in #include statements

### Undefined References
- Verify implementation files match headers
- Check namespace usage (simulation::)