#include "NewsWindow.h"
#include "utils.h"
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

// Days in each month (non-leap year)
const int NewsWindow::DAYS_IN_MONTH[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// Callback function for CURL to write received data
static size_t NewsWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

NewsWindow::NewsWindow() 
    : m_isVisible(false)
    , m_isLoading(false)
    , m_hasError(false)
    , m_tableHeight(300.0f)
{
    // Initialize ticker buffer
    strcpy_s(m_tickerBuffer, sizeof(m_tickerBuffer), "TSLA");
    
    // Initialize dates to current date (simplified to 2025-01-01 for demo)
    m_fromYear = 2025;
    m_fromMonth = 1;
    m_fromDay = 1;
    
    m_toYear = 2025;
    m_toMonth = 1;
    m_toDay = 2;
}

void NewsWindow::Draw() {
    if (!m_isVisible) {
        return;
    }
    
    ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("News Window", &m_isVisible)) {
        // Ticker input
        ImGui::Text("Ticker Symbol:");
        ImGui::SameLine();
        ImGui::InputText("##ticker", m_tickerBuffer, sizeof(m_tickerBuffer));
        
        ImGui::Separator();
        
        // Date pickers
        ImGui::Text("Date Range:");
        DrawDatePicker("From Date", m_fromYear, m_fromMonth, m_fromDay);
        ImGui::SameLine();
        DrawDatePicker("To Date", m_toYear, m_toMonth, m_toDay);
        
        ImGui::Separator();
        
        // Get News button
        if (ImGui::Button("Get News") && !m_isLoading) {
            FetchNewsData();
        }
        
        ImGui::SameLine();
        if (m_isLoading) {
            ImGui::Text("Loading...");
        }
        
        ImGui::Separator();
        
        // Error display
        if (m_hasError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::Text("Error: %s", m_errorMessage.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        
        // News data table
        if (!m_newsData.empty()) {
            ImGui::Text("News Articles (%zu found):", m_newsData.size());
            
            if (ImGui::BeginTable("NewsTable", 5, 
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
                
                // Setup columns
                ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                ImGui::TableSetupColumn("Publisher", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("URL", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row
                ImGui::TableHeadersRow();
                
                // Display news data
                for (size_t i = 0; i < m_newsData.size(); ++i) {
                    const NewsData& news = m_newsData[i];
                    
                    ImGui::TableNextRow();
                    
                    // Title
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", TruncateText(news.title, 100).c_str());
                    
                    // Publisher
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", news.publisher.c_str());
                    
                    // Published date
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", news.published_date.substr(0, 10).c_str()); // Show just date part
                    
                    // News text (truncated)
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", TruncateText(news.news_text, 200).c_str());
                    
                    // URL (as clickable link)
                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::SmallButton(("Link##" + std::to_string(i)).c_str())) {
                        // In a real application, you would open the URL in a browser
                        // For now, just copy to clipboard or show in console
                        std::cout << "Opening URL: " << news.news_url << std::endl;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", news.news_url.c_str());
                    }
                }
                
                ImGui::EndTable();
            }
        } else if (!m_isLoading && !m_hasError) {
            ImGui::Text("No news data. Click 'Get News' to fetch articles.");
        }
    }
    ImGui::End();
}

void NewsWindow::DrawDatePicker(const char* label, int& year, int& month, int& day) {
    ImGui::PushID(label);
    
    ImGui::Text("%s", label);
    
    // Year input
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("##year", &year)) {
        if (year < MIN_YEAR) year = MIN_YEAR;
        if (year > MAX_YEAR) year = MAX_YEAR;
    }
    ImGui::SameLine();
    
    // Month combo
    ImGui::SetNextItemWidth(80);
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (ImGui::Combo("##month", &month, [](void* data, int idx, const char** out_text) {
        *out_text = ((const char**)data)[idx];
        return true;
    }, (void*)months, 12)) {
        // Month changed, validate day
        int maxDay = DAYS_IN_MONTH[month];
        if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            maxDay = 29; // Leap year February
        }
        if (day > maxDay) day = maxDay;
    }
    ImGui::SameLine();
    
    // Day input
    ImGui::SetNextItemWidth(60);
    if (ImGui::InputInt("##day", &day)) {
        int maxDay = DAYS_IN_MONTH[month];
        if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            maxDay = 29; // Leap year February
        }
        if (day < 1) day = 1;
        if (day > maxDay) day = maxDay;
    }
    
    ImGui::PopID();
}

void NewsWindow::FetchNewsData() {
    m_isLoading = true;
    m_hasError = false;
    m_errorMessage.clear();
    m_newsData.clear();
    
    // Initialize CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        m_hasError = true;
        m_errorMessage = "Failed to initialize CURL";
        m_isLoading = false;
        return;
    }
    
    // Construct URL
    std::stringstream url;
    url << "https://agenticresearch.info/news?symbol=" << m_tickerBuffer
        << "&from=" << FormatDateForAPI(m_fromYear, m_fromMonth, m_fromDay, false)
        << "&to=" << FormatDateForAPI(m_toYear, m_toMonth, m_toDay, true);
    
    std::string readBuffer;
    
    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NewsWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // 30 second timeout
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
    
    std::cout << "[NewsWindow] Fetching news from: " << url.str() << std::endl;
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up CURL
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        m_hasError = true;
        m_errorMessage = "Network request failed: " + std::string(curl_easy_strerror(res));
        m_isLoading = false;
        return;
    }
    
    std::cout << "[NewsWindow] Received " << readBuffer.size() << " bytes" << std::endl;
    
    // Parse JSON response using RapidJSON
    rapidjson::Document document;
    rapidjson::ParseResult parseResult = document.Parse(readBuffer.c_str());
    
    if (!parseResult) {
        m_hasError = true;
        m_errorMessage = "JSON parse error: " + std::string(rapidjson::GetParseError_En(parseResult.Code()));
        m_isLoading = false;
        return;
    }
    
    // Check if the response is an array
    if (!document.IsArray()) {
        m_hasError = true;
        m_errorMessage = "Expected JSON array response";
        m_isLoading = false;
        return;
    }
    
    // Parse each news entry
    for (rapidjson::SizeType i = 0; i < document.Size(); i++) {
        const rapidjson::Value& item = document[i];
        
        if (!item.IsObject()) {
            continue;
        }
        
        NewsData news;
        
        // Extract values with error checking
        if (item.HasMember("image_url") && item["image_url"].IsString()) {
            news.image_url = item["image_url"].GetString();
        }
        if (item.HasMember("ingested_at") && item["ingested_at"].IsString()) {
            news.ingested_at = item["ingested_at"].GetString();
        }
        if (item.HasMember("news_id") && item["news_id"].IsString()) {
            news.news_id = item["news_id"].GetString();
        }
        if (item.HasMember("news_text") && item["news_text"].IsString()) {
            news.news_text = item["news_text"].GetString();
        }
        if (item.HasMember("news_url") && item["news_url"].IsString()) {
            news.news_url = item["news_url"].GetString();
        }
        if (item.HasMember("published_date") && item["published_date"].IsString()) {
            news.published_date = item["published_date"].GetString();
        }
        if (item.HasMember("publisher") && item["publisher"].IsString()) {
            news.publisher = item["publisher"].GetString();
        }
        if (item.HasMember("site") && item["site"].IsString()) {
            news.site = item["site"].GetString();
        }
        if (item.HasMember("symbol") && item["symbol"].IsString()) {
            news.symbol = item["symbol"].GetString();
        }
        if (item.HasMember("title") && item["title"].IsString()) {
            news.title = item["title"].GetString();
        }
        
        m_newsData.push_back(news);
    }
    
    std::cout << "[NewsWindow] Parsed " << m_newsData.size() << " news articles" << std::endl;
    m_isLoading = false;
}

std::string NewsWindow::FormatDateForAPI(int year, int month, int day, bool isEndDate) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(4) << year << "-"
       << std::setfill('0') << std::setw(2) << (month + 1) << "-"  // month is 0-based in our picker
       << std::setfill('0') << std::setw(2) << day;
    
    if (isEndDate) {
        ss << "+23:59:59";
    } else {
        ss << "+00:00:00";
    }
    
    return ss.str();
}

std::string NewsWindow::TruncateText(const std::string& text, size_t maxLength) {
    if (text.length() <= maxLength) {
        return text;
    }
    
    return text.substr(0, maxLength - 3) + "...";
}