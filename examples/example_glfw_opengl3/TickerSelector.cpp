#include "TickerSelector.h"
#include "imgui_internal.h" // For ImGui::ClearActiveID

// TickerSelector constructor remains the same
TickerSelector::TickerSelector() : m_showSuggestions(false), m_selectedSuggestionIndex(-1), m_hintClickedAndPendingDataFetch(false), m_deferredPopupRequested(false) {
    m_allTickers = {
        "CVX","TSLA","NFLX","GS","V","MDT","F","NKE","T","QCOM","SCHW","MSFT","BMY","ORCL","UNH","NVDA",
        "PFE","AVGO","MA","ADP","GE","KO","INTC","BX","C","PG","COST","JNJ","CAT","WMT","MRK","XOM",
        "CSCO","HON","GOOG","LLY","JPM","UPS","DIS","CRM","BAC","MCD","META","ABT","AAPL","IBM",
        "DHR","HD","PEP"
    };
    m_inputTextBuffer[0] = '\0';
    m_selectedTicker = "TSLA";
}

void TickerSelector::UpdateFilteredTickers() {
    m_filteredTickers.clear();
    std::string currentInput = m_inputTextBuffer;
    std::transform(currentInput.begin(), currentInput.end(), currentInput.begin(), ::toupper);

    if (currentInput.empty()) {
        m_showSuggestions = false;
        return;
    }

    for (const auto& ticker : m_allTickers) {
        if (ticker.rfind(currentInput, 0) == 0) {
            m_filteredTickers.push_back(ticker);
        }
    }
    m_showSuggestions = !m_filteredTickers.empty();
}

void TickerSelector::Draw() {
    ImGui::PushID(this);

    // Simple approach: always ensure the input field is responsive
    bool enter_pressed = ImGui::InputText("Ticker", m_inputTextBuffer, IM_ARRAYSIZE(m_inputTextBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    
    // Reset state when user clicks on the input field to start fresh
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // Reset all internal state to ensure responsiveness
        m_selectedSuggestionIndex = -1;
        m_hintClickedAndPendingDataFetch = false;
        // When user clicks, they want to interact - always try to show suggestions
        UpdateFilteredTickers();
        // Force show suggestions if we have any matches, regardless of focus state
        m_showSuggestions = !m_filteredTickers.empty();
    }
    
    // If the input text buffer is changed, update our filtered list
    if (ImGui::IsItemEdited()) {
        UpdateFilteredTickers();
        m_selectedSuggestionIndex = -1; // Reset selection when typing
    }
    
    // Ensure the input field remains responsive when focused
    if (ImGui::IsItemFocused()) {
        // Reset any problematic state that might interfere with typing
        m_hintClickedAndPendingDataFetch = false;
        // Always try to show suggestions when focused and there's text
        if (strlen(m_inputTextBuffer) > 0) {
            UpdateFilteredTickers();
            // Force suggestions to show if we have matches
            if (!m_filteredTickers.empty()) {
                m_showSuggestions = true;
            }
        }
    }
    
    // --- Core Logic for Suggestions ---
    const bool isInputFocused = ImGui::IsItemFocused();
    const bool isInputActive = ImGui::IsItemActive();

    // Always ensure the widget is visible and responding
    if (!ImGui::IsItemVisible()) {
        // Reset state if widget becomes invisible
        m_showSuggestions = false;
        ImGui::PopID();
        return;
    }

    // 1. Handle Keyboard Navigation
    if (m_showSuggestions && isInputFocused) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            m_selectedSuggestionIndex = (m_selectedSuggestionIndex < (int)m_filteredTickers.size() - 1) ? m_selectedSuggestionIndex + 1 : 0;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            m_selectedSuggestionIndex = (m_selectedSuggestionIndex > 0) ? m_selectedSuggestionIndex - 1 : (int)m_filteredTickers.size() - 1;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_selectedSuggestionIndex != -1) {
            // Select with Enter key
            m_selectedTicker = m_filteredTickers[m_selectedSuggestionIndex];
            m_hintClickedAndPendingDataFetch = true;
            m_showSuggestions = false; // Hide after selection
            // Clear the input buffer so suggestions don't reappear immediately
            m_inputTextBuffer[0] = '\0';
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_showSuggestions = false; // Hide on Escape
            ImGui::ClearActiveID(); // Escape should clear focus
        }
    }
    
    // Handle Enter key press on raw input (no suggestion selected)
    if (enter_pressed && !m_showSuggestions && strlen(m_inputTextBuffer) > 0) {
        // Check if the input text exactly matches any of the suggestions, if so, it's not "raw" input in that sense.
        // This check is to prevent double-submission if Enter was pressed on a suggestion that also happened to be the raw text.
        // However, the current logic for suggestion selection already handles Enter.
        // So, this block should primarily trigger if suggestions are not shown OR no suggestion is highlighted.
        // A simple check is if m_selectedSuggestionIndex is -1 when suggestions *could* be showing.
        // Or, more simply, if !m_showSuggestions is true.

        bool isRawEnter = true;
        if (m_showSuggestions && m_selectedSuggestionIndex != -1) {
            // If suggestions are shown and one is selected, Enter is handled above.
            isRawEnter = false;
        }

        if (isRawEnter) {
            m_selectedTicker = m_inputTextBuffer;
            m_hintClickedAndPendingDataFetch = true; // Signal that a selection was made
            m_showSuggestions = false; // Hide suggestions after selection
            // Clear the input buffer so suggestions don't reappear immediately
            m_inputTextBuffer[0] = '\0';
        }
    }


    // Store popup position for deferred rendering outside window context
    if (m_showSuggestions && !m_filteredTickers.empty()) {
        ImVec2 inputPos = ImGui::GetItemRectMin();
        ImVec2 inputSize = ImGui::GetItemRectSize();
        m_deferredPopupPos = ImVec2(inputPos.x, inputPos.y + inputSize.y);
        m_deferredPopupSize = ImVec2(inputSize.x, 0);
        m_deferredPopupRequested = true;
    } else {
        m_deferredPopupRequested = false;
    }

    ImGui::PopID();
}

// GetSelectedTicker and IsHintClickedAndPendingDataFetch remain the same
const char* TickerSelector::GetSelectedTicker() const {
    return m_selectedTicker.c_str();
}

bool TickerSelector::IsHintClickedAndPendingDataFetch(std::string& outTicker) {
    if (m_hintClickedAndPendingDataFetch) {
        outTicker = m_selectedTicker;
        m_hintClickedAndPendingDataFetch = false;
        return true;
    }
    return false;
}

void TickerSelector::RenderPopupOutsideWindow() {
    if (!m_deferredPopupRequested || m_filteredTickers.empty()) {
        return;
    }
    
    // Create a floating window that appears on top
    ImGui::SetNextWindowPos(m_deferredPopupPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(m_deferredPopupSize, ImGuiCond_Always);
    
    // Use window flags that ensure it appears on top and is interactable
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoFocusOnAppearing;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    
    if (ImGui::Begin("##TickerSuggestionsDeferred", nullptr, windowFlags)) {
        // Bring this window to the front to ensure it's on top
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        for (size_t i = 0; i < m_filteredTickers.size(); ++i) {
            bool isSelected = (m_selectedSuggestionIndex == (int)i);
            if (ImGui::Selectable(m_filteredTickers[i].c_str(), isSelected)) {
                // Select with mouse click
                strncpy(m_inputTextBuffer, m_filteredTickers[i].c_str(), sizeof(m_inputTextBuffer) - 1);
                m_inputTextBuffer[sizeof(m_inputTextBuffer) - 1] = '\0';
                m_selectedTicker = m_filteredTickers[i];
                m_hintClickedAndPendingDataFetch = true;
                m_showSuggestions = false;
                m_deferredPopupRequested = false;
                // Clear the input buffer so suggestions don't reappear immediately
                m_inputTextBuffer[0] = '\0';
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
    }
    ImGui::End();
    
    ImGui::PopStyleVar(2);
    
    // Hide suggestions if mouse is not over the suggestions window and input is not focused
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        // We can't check input focus here since we're outside the input context
        // So we'll rely on the main Draw() method to manage this
    }
}
