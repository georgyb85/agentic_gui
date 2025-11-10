#pragma once

#include "imgui.h"
#include <string>
#include <vector>
#include <algorithm> // Required for std::transform and std::string comparison

class TickerSelector {
public:
    TickerSelector();
    void Draw();
    const char* GetSelectedTicker() const;
    bool IsHintClickedAndPendingDataFetch(std::string& outTicker);
    void RenderPopupOutsideWindow(); // Render popup outside of parent window context

private:
    void UpdateFilteredTickers();

    char m_inputTextBuffer[64]; // Buffer for ImGui::InputText
    std::string m_selectedTicker;
    std::vector<std::string> m_allTickers;
    std::vector<std::string> m_filteredTickers;
    bool m_showSuggestions;
    int m_selectedSuggestionIndex;
    bool m_hintClickedAndPendingDataFetch; // Flag to indicate a hint was clicked
    
    // For deferred popup rendering outside window context
    bool m_deferredPopupRequested;
    ImVec2 m_deferredPopupPos;
    ImVec2 m_deferredPopupSize;
};