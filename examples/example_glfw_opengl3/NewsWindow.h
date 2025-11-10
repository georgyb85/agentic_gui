#pragma once

#include "imgui.h"
#include <string>
#include <vector>
#include <ctime>

// Structure to hold news data
struct NewsData {
    std::string image_url;
    std::string ingested_at;
    std::string news_id;
    std::string news_text;
    std::string news_url;
    std::string published_date;
    std::string publisher;
    std::string site;
    std::string symbol;
    std::string title;
};

class NewsWindow {
public:
    NewsWindow();
    void Draw();
    bool IsVisible() const { return m_isVisible; }
    void SetVisible(bool visible) { m_isVisible = visible; }

private:
    void DrawDatePicker(const char* label, int& year, int& month, int& day);
    void FetchNewsData();
    std::string FormatDateForAPI(int year, int month, int day, bool isEndDate = false);
    std::string TruncateText(const std::string& text, size_t maxLength);
    
    // UI state
    bool m_isVisible;
    char m_tickerBuffer[64];
    
    // Date picker state
    int m_fromYear, m_fromMonth, m_fromDay;
    int m_toYear, m_toMonth, m_toDay;
    
    // Data and loading state
    std::vector<NewsData> m_newsData;
    bool m_isLoading;
    bool m_hasError;
    std::string m_errorMessage;
    
    // Table display state
    float m_tableHeight;
    
    // Constants for date validation
    static const int MIN_YEAR = 2020;
    static const int MAX_YEAR = 2030;
    static const int DAYS_IN_MONTH[12];
};