#include "LFSWindow.h"
#include "TimeSeriesWindow.h"
#include "analytics_dataframe.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <chrono>
#include <random>
#include <set>
#include <thread>
#include <arrow/api.h>

// LFS core includes
#include "lfs/const.h"
#include "lfs/classes.h"
#include "lfs/funcdefs.h"
#include "lfs/data_matrix.h"

// Global variables required by LFS framework
extern double* database;
extern int n_cases;
extern int n_vars;
extern int cuda_present;
extern int cuda_enable;
extern int max_threads_limit;
extern void* hwndProgress;
extern bool g_use_highs_solver;

// Timing variables
extern int LFStimeTotal;
extern int LFStimeRealToBinary;
extern int LFStimeBetaCrit;
extern int LFStimeWeights;
extern int LFStimeCUDA;

LFSWindow::LFSWindow()
    : m_isVisible(false)
    , m_dataSource(nullptr)
    , m_isRunning(false)
    , m_stopRequested(false)
    , m_hasResults(false)
    , m_progress(0.0f)
    , m_database(nullptr)
    , m_n_cases(0)
    , m_n_vars(0)
    , m_maxKept(3)
    , m_iterations(3)
    , m_nRand(500)
    , m_nBeta(20)
    , m_maxThreads(std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 8)
    , m_targetBins(3)   // Default: 3 bins for target discretization
    , m_mcptReps(0)     // Default: no MCPT
    , m_mcptType(0)     // Default: None
    , m_useCUDA(true)   // Default: CUDA enabled
    , m_solverType(0)   // Default: Legacy solver
    , m_startRow(0)     // Default: start from beginning
    , m_endRow(-1) {    // Default: read all rows
    
    // Configure feature selector for financial data
    m_featureSelector.SetTargetPrefix("tgt_");
    m_featureSelector.SetShowOnlyTargetsWithPrefix(true);
    m_featureSelector.SetSortAlphabetically(true);
    
    // Initialize results buffer
    m_resultsBuffer.clear();
}

LFSWindow::~LFSWindow() {
    // Stop any running analysis
    m_stopRequested = true;
    if (m_analysisFuture.valid()) {
        m_analysisFuture.wait();
    }
    
    // Clean up database
    if (m_database) {
        delete[] m_database;
        m_database = nullptr;
    }
}

void LFSWindow::SetDataSource(const TimeSeriesWindow* dataSource) {
    m_dataSource = dataSource;
    UpdateColumnList();
}

void LFSWindow::UpdateColumnList() {
    if (!m_dataSource || !m_dataSource->HasData()) {
        m_availableColumns.clear();
        return;
    }
    
    auto df = m_dataSource->GetDataFrame();
    if (df) {
        m_availableColumns = df->column_names();
        m_featureSelector.SetAvailableColumns(m_availableColumns);
    }
}

void LFSWindow::AppendToResults(const std::string& text) {
    m_resultsBuffer += text;
}

void LFSWindow::Draw() {
    if (!m_isVisible) return;
    
    ImGui::SetNextWindowSize(ImVec2(1400, 900), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Local Feature Selection (LFS)", &m_isVisible)) {
        
        // Check data availability
        if (!m_dataSource || !m_dataSource->HasData()) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "No data loaded. Please load data first.");
            ImGui::End();
            return;
        }
        
        // Main layout - split into left config panel and right results area
        ImGui::Columns(2, "LFSColumns", true);
        ImGui::SetColumnWidth(0, 500);
        
        // Left Column - Configuration
        ImGui::BeginChild("ConfigPanel", ImVec2(0, 0), true);
        ImGui::Text("LFS Configuration");
        ImGui::Separator();
        
        // Feature and Target Selection
        ImGui::Text("Feature and Target Selection:");
        m_featureSelector.Draw();
        
        ImGui::Separator();
        ImGui::Text("LFS Parameters:");
        
        // LFS parameters in a neat grid
        ImGui::PushItemWidth(150);
        
        ImGui::InputInt("Max Variables Kept", &m_maxKept);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Maximum number of variables used as the metric space for each case");
        m_maxKept = std::max(1, std::min(m_maxKept, 100));
        
        ImGui::InputInt("Iterations", &m_iterations);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Number of LFS iterations (2-3 typically sufficient)");
        m_iterations = std::max(1, std::min(m_iterations, 10));
        
        ImGui::InputInt("Monte-Carlo Trials", &m_nRand);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Number of random tries converting real f to binary (500+ recommended)");
        m_nRand = std::max(100, std::min(m_nRand, 10000));
        
        ImGui::InputInt("Beta Trials", &m_nBeta);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Number of trial values for best beta (10-30 typical)");
        m_nBeta = std::max(5, std::min(m_nBeta, 100));
        
        ImGui::InputInt("Max Threads", &m_maxThreads);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Maximum number of threads to use");
        m_maxThreads = std::max(1, std::min(m_maxThreads, 64));
        
        ImGui::InputInt("Target Bins", &m_targetBins);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Number of bins for target discretization (2-10 typical)");
        m_targetBins = std::max(2, std::min(m_targetBins, 10));
        
        ImGui::PopItemWidth();
        
        ImGui::Separator();
        ImGui::Text("Data Range:");
        
        ImGui::PushItemWidth(150);
        
        // Get total rows from data source if available
        int totalRows = 0;
        if (m_dataSource && m_dataSource->HasData()) {
            auto df = m_dataSource->GetDataFrame();
            if (df) {
                totalRows = df->num_rows();
            }
        }
        
        ImGui::InputInt("Start Row", &m_startRow);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Starting row for analysis (0-based index)");
        m_startRow = std::max(0, m_startRow);
        
        ImGui::InputInt("End Row", &m_endRow);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Ending row for analysis (-1 = all rows)");
        
        if (totalRows > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Total: %d rows", totalRows);
            if (m_endRow == -1 || m_endRow > totalRows) {
                m_endRow = totalRows;
            }
        }
        
        ImGui::PopItemWidth();
        
        ImGui::Separator();
        ImGui::Text("MCPT Configuration:");
        
        ImGui::PushItemWidth(150);
        ImGui::InputInt("MCPT Replications", &m_mcptReps);
        if (ImGui::IsItemHovered()) 
            ImGui::SetTooltip("Number of permutation test replications (0 = disabled)");
        m_mcptReps = std::max(0, std::min(m_mcptReps, 10000));
        ImGui::PopItemWidth();
        
        // Always show MCPT type selector
        ImGui::Text("MCPT Type:");
        ImGui::RadioButton("None", &m_mcptType, 0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("No permutation testing");
        ImGui::SameLine();
        ImGui::RadioButton("Complete", &m_mcptType, 1);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Complete random shuffling of target labels");
        ImGui::SameLine();
        ImGui::RadioButton("Cyclic", &m_mcptType, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Cyclic shift of target labels by random offset");
        
        if (m_mcptReps > 0 && m_mcptType > 0) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), 
                "MCPT enabled: %d %s replications", 
                m_mcptReps, 
                m_mcptType == 1 ? "complete" : "cyclic");
        } else if (m_mcptReps > 0 && m_mcptType == 0) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), 
                "Warning: MCPT replications set but type is None");
        }
        
        ImGui::Separator();
        
        // Solver selection
        ImGui::Text("Solver:");
        ImGui::RadioButton("Legacy (Original)", &m_solverType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("HiGHS (Modern)", &m_solverType, 1);
        
        // CUDA option
        ImGui::Checkbox("Enable CUDA", &m_useCUDA);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Use CUDA for acceleration if available");
        
        ImGui::Separator();
        
        // Control buttons
        if (!m_isRunning) {
            if (ImGui::Button("Run LFS Analysis", ImVec2(-1, 30))) {
                StartAnalysis();
            }
        } else {
            if (ImGui::Button("Stop Analysis", ImVec2(-1, 30))) {
                m_stopRequested = true;
            }
            
            // Progress bar
            ImGui::ProgressBar(m_progress, ImVec2(-1, 0), m_progressText.c_str());
        }
        
        ImGui::EndChild();
        
        // Right Column - Results
        ImGui::NextColumn();
        ImGui::BeginChild("ResultsPanel", ImVec2(0, 0), true);
        
        // Results header with Clear button
        ImGui::Text("Analysis Results");
        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        if (ImGui::Button("Clear Output")) {
            m_resultsBuffer.clear();
            m_hasResults = false;
        }
        ImGui::Separator();
        
        // Results text area
        ImGui::InputTextMultiline("##Results", 
            const_cast<char*>(m_resultsBuffer.c_str()), 
            m_resultsBuffer.size() + 1,
            ImVec2(-1, -1), 
            ImGuiInputTextFlags_ReadOnly);
        
        ImGui::EndChild();
        
        ImGui::Columns(1);
    }
    ImGui::End();
}

void LFSWindow::StartAnalysis() {
    if (m_isRunning) return;
    
    // Get selected features and target
    auto selectedFeatures = m_featureSelector.GetSelectedFeatures();
    auto selectedTarget = m_featureSelector.GetSelectedTarget();
    
    if (selectedFeatures.empty()) {
        AppendToResults("Error: No features selected\n");
        return;
    }
    
    if (selectedTarget.empty()) {
        AppendToResults("Error: No target variable selected\n");
        return;
    }
    
    // Don't clear results - append to existing
    AppendToResults("\n========================================\n");
    AppendToResults("Starting New LFS Analysis\n");
    AppendToResults("========================================\n\n");
    
    m_isRunning = true;
    m_stopRequested = false;
    m_progress = 0.0f;
    
    // Run analysis in background thread
    m_analysisFuture = std::async(std::launch::async, [this, selectedFeatures, selectedTarget]() {
        RunLFSAnalysis(selectedFeatures, selectedTarget);
    });
}

void LFSWindow::ShuffleTargetComplete(double* data, int nCases, int nVars) {
    // Complete shuffling - Fisher-Yates shuffle of target column
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int i = nCases;
    while (i > 1) {
        std::uniform_int_distribution<> dis(0, i - 1);
        int j = dis(gen);
        --i;
        // Swap target values (last column) at positions i and j
        std::swap(data[i * (nVars + 1) + nVars], data[j * (nVars + 1) + nVars]);
    }
}

void LFSWindow::ShuffleTargetCyclic(double* data, int nCases, int nVars) {
    // Cyclic shuffling - shift all target values by random offset
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, nCases - 1);
    
    int offset = dis(gen);
    
    // Copy target column to temp vector
    std::vector<double> temp(nCases);
    for (int i = 0; i < nCases; i++) {
        temp[i] = data[((i + offset) % nCases) * (nVars + 1) + nVars];
    }
    
    // Copy back
    for (int i = 0; i < nCases; i++) {
        data[i * (nVars + 1) + nVars] = temp[i];
    }
}

void LFSWindow::RunLFSAnalysis(const std::vector<std::string>& features, const std::string& target) {
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Prepare data
        m_progressText = "Preparing data...";
        m_progress = 0.1f;
        
        if (!PrepareData(features, target)) {
            AppendToResults("Error: Failed to prepare data\n");
            m_isRunning = false;
            return;
        }
        
        // Print configuration
        std::stringstream ss;
        ss << "Configuration:\n";
        ss << "  Features: " << features.size() << " selected\n";
        ss << "  Target: " << target << "\n";
        ss << "  Cases: " << m_n_cases;
        if (m_startRow > 0 || m_endRow != -1) {
            ss << " (rows " << m_startRow << " to " << (m_endRow == -1 ? "end" : std::to_string(m_endRow)) << ")";
        }
        ss << "\n";
        ss << "  Max kept: " << m_maxKept << "\n";
        ss << "  Iterations: " << m_iterations << "\n";
        ss << "  Monte-Carlo trials: " << m_nRand << "\n";
        ss << "  Beta trials: " << m_nBeta << "\n";
        ss << "  Max threads: " << m_maxThreads << "\n";
        ss << "  Target bins: " << m_targetBins << "\n";
        if (m_mcptReps > 0 && m_mcptType > 0) {
            ss << "  MCPT: " << m_mcptReps << " " 
               << (m_mcptType == 1 ? "complete" : "cyclic") << " replications\n";
        } else {
            ss << "  MCPT: Disabled\n";
        }
        ss << "  Solver: " << (m_solverType == 0 ? "Legacy" : "HiGHS") << "\n";
        ss << "  CUDA: " << (m_useCUDA ? "Enabled" : "Disabled") << "\n\n";
        AppendToResults(ss.str());
        
        // Set global variables for LFS
        ::database = m_database;
        ::n_cases = m_n_cases;
        ::n_vars = m_n_vars;
        ::max_threads_limit = m_maxThreads;
        ::cuda_enable = m_useCUDA ? 1 : 0;
        ::g_use_highs_solver = (m_solverType == 1);
        
        // Prepare for MCPT if enabled
        int actualReps = (m_mcptReps > 0 && m_mcptType > 0) ? m_mcptReps : 1;
        std::vector<double> originalCrits(m_n_vars);
        std::vector<int> mcptSolo(m_n_vars, 1);     // Initialize to 1 (includes original)
        std::vector<int> mcptBestof(m_n_vars, 1);   // Initialize to 1 (includes original)
        std::vector<int> featureCounts(m_n_vars);
        
        // Make a working copy of data for permutations
        std::vector<double> workingData(m_database, m_database + m_n_cases * (m_n_vars + 1));
        
        // Run LFS for each replication (including original)
        for (int irep = 0; irep < actualReps; irep++) {
            if (m_stopRequested) break;
            
            // Update progress
            if (m_mcptReps > 0 && m_mcptType > 0) {
                ss.str("");
                ss << "MCPT replication " << (irep + 1) << " of " << actualReps;
                m_progressText = ss.str();
                m_progress = 0.2f + 0.7f * irep / actualReps;
            } else {
                m_progressText = "Running LFS analysis...";
                m_progress = 0.3f;
            }
            
            // Shuffle target if in permutation run (irep > 0)
            if (irep > 0 && m_mcptReps > 0 && m_mcptType > 0) {
                // Copy original data
                std::copy(m_database, m_database + m_n_cases * (m_n_vars + 1), workingData.begin());
                
                // Shuffle based on type
                if (m_mcptType == 1) {
                    ShuffleTargetComplete(workingData.data(), m_n_cases, m_n_vars);
                } else if (m_mcptType == 2) {
                    ShuffleTargetCyclic(workingData.data(), m_n_cases, m_n_vars);
                }
            } else {
                // Use original data
                std::copy(m_database, m_database + m_n_cases * (m_n_vars + 1), workingData.begin());
            }
            
            // Create LFS instance
            LFS lfs(m_n_cases, m_n_vars, m_maxKept, m_maxThreads, workingData.data(), 
                    irep == 0 ? 1 : 0); // Progress only for first run
            
            if (!lfs.ok) {
                AppendToResults("Error: Failed to initialize LFS\n");
                m_isRunning = false;
                return;
            }
            
            // Run LFS
            int result = lfs.run(m_iterations, m_nRand, m_nBeta, irep, actualReps);
            
            if (result != 0) {
                ss.str("");
                ss << "Error: LFS failed with error code " << result << " at replication " << irep << "\n";
                AppendToResults(ss.str());
                continue;
            }
            
            // Get results
            int* f_binary = lfs.get_f();
            
            // Count feature usage
            std::fill(featureCounts.begin(), featureCounts.end(), 0);
            for (int i = 0; i < m_n_cases; i++) {
                for (int j = 0; j < m_n_vars; j++) {
                    if (f_binary[i * m_n_vars + j]) {
                        featureCounts[j]++;
                    }
                }
            }
            
            // Calculate criterion (percent of times selected) and update MCPT counts
            double bestCrit = 0.0;
            for (int j = 0; j < m_n_vars; j++) {
                double crit = 100.0 * featureCounts[j] / m_n_cases;
                
                if (j == 0 || crit > bestCrit) {
                    bestCrit = crit;
                }
                
                if (irep == 0) {
                    // Original, unpermuted data
                    originalCrits[j] = crit;
                } else if (m_mcptReps > 0 && m_mcptType > 0) {
                    // Permuted data - update solo p-value
                    if (crit >= originalCrits[j]) {
                        mcptSolo[j]++;
                    }
                }
            }
            
            // Update unbiased p-value (best of permuted >= each original)
            if (irep > 0 && m_mcptReps > 0 && m_mcptType > 0) {
                for (int j = 0; j < m_n_vars; j++) {
                    if (bestCrit >= originalCrits[j]) {
                        mcptBestof[j]++;
                    }
                }
            }
        }
        
        m_progress = 0.9f;
        m_progressText = "Processing results...";
        
        // Display results
        if (m_mcptReps > 0 && m_mcptType > 0) {
            ProcessMCPTResults(features, originalCrits, mcptSolo, mcptBestof);
        } else {
            // Create sorted pairs for display
            std::vector<std::pair<double, int>> sortedFeatures;
            for (int i = 0; i < m_n_vars; i++) {
                sortedFeatures.push_back({originalCrits[i], i});
            }
            std::sort(sortedFeatures.begin(), sortedFeatures.end(), std::greater<>());
            
            // Display simple results
            ss.str("");
            ss << "\n--- LFS Results ---\n\n";
            ss << "Feature Importance (ALL " << m_n_vars << " features):\n";
            ss << std::string(60, '-') << "\n";
            ss << std::setw(5) << "Rank" << " | " 
               << std::setw(30) << std::left << "Feature" << " | "
               << std::setw(12) << "Percent" << "\n";
            ss << std::string(60, '-') << "\n";
            
            for (int i = 0; i < m_n_vars; i++) {
                int idx = sortedFeatures[i].second;
                
                ss << std::setw(5) << (i + 1) << " | "
                   << std::setw(30) << std::left << features[idx] << " | "
                   << std::setw(11) << std::fixed << std::setprecision(2) 
                   << sortedFeatures[i].first << "%\n";
            }
            AppendToResults(ss.str());
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        ss.str("");
        ss << "\nAnalysis completed in " << std::fixed << std::setprecision(2) 
           << duration.count() / 1000.0 << " seconds\n";
        AppendToResults(ss.str());
        
        m_hasResults = true;
        
    } catch (const std::exception& e) {
        std::stringstream ss;
        ss << "Error during analysis: " << e.what() << "\n";
        AppendToResults(ss.str());
    }
    
    m_isRunning = false;
    m_progress = 0.0f;
    m_progressText = "";
}

void LFSWindow::ProcessMCPTResults(const std::vector<std::string>& selectedFeatures,
                                   const std::vector<double>& originalCrits,
                                   const std::vector<int>& mcptSolo,
                                   const std::vector<int>& mcptBestof) {
    // Create feature info structure for smart ranking
    struct FeatureInfo {
        int index;
        double percent;
        double soloPval;
        double unbiasedPval;
        int category; // 0=highly significant, 1=significant, 2=marginal, 3=noise
    };
    
    std::vector<FeatureInfo> features;
    for (int i = 0; i < m_n_vars; i++) {
        FeatureInfo info;
        info.index = i;
        info.percent = originalCrits[i];
        info.soloPval = static_cast<double>(mcptSolo[i]) / static_cast<double>(m_mcptReps + 1);
        info.unbiasedPval = static_cast<double>(mcptBestof[i]) / static_cast<double>(m_mcptReps + 1);
        
        // Categorize based on p-values
        if (info.soloPval <= 0.05) {
            info.category = 0; // Highly significant (p <= 0.05)
        } else if (info.soloPval <= 0.10) {
            info.category = 1; // Significant (0.05 < p <= 0.10)
        } else if (info.soloPval <= 0.20) {
            info.category = 2; // Marginal (0.10 < p <= 0.20)
        } else {
            info.category = 3; // Likely noise (p > 0.20)
        }
        
        features.push_back(info);
    }
    
    // Sort by percentage (matching legacy behavior)
    std::sort(features.begin(), features.end(), 
        [](const FeatureInfo& a, const FeatureInfo& b) { 
            return a.percent > b.percent; 
        });
    
    // Display MCPT results with significance indicators
    std::stringstream ss;
    ss << "\n--- LFS Results with MCPT ---\n\n";
    ss << "Monte-Carlo Permutation Test Results:\n";
    ss << "  Type: " << (m_mcptType == 1 ? "Complete" : "Cyclic") << "\n";
    ss << "  Replications: " << m_mcptReps << "\n\n";
    
    // Legend for significance levels
    ss << "Significance Legend:\n";
    ss << "  *** Highly significant (p ≤ 0.05) - STRONG predictors\n";
    ss << "  **  Significant (0.05 < p ≤ 0.10) - Good predictors\n";
    ss << "  *   Marginal (0.10 < p ≤ 0.20) - Weak predictors\n";
    ss << "  !   Likely noise (p > 0.20) - CAUTION: may be spurious\n\n";
    
    ss << std::string(95, '-') << "\n";
    ss << std::setw(5) << "Rank" << " | " 
       << std::setw(4) << "Sig" << " | "
       << std::setw(25) << std::left << "Variable" << " | "
       << std::setw(10) << "Pct" << " | "
       << std::setw(12) << "Solo p-val" << " | "
       << std::setw(14) << "Unbiased p-val" << "\n";
    ss << std::string(95, '-') << "\n";
    
    // Display all features with significance markers
    for (size_t i = 0; i < features.size(); i++) {
        const auto& f = features[i];
        
        // Determine significance marker
        std::string marker;
        if (f.category == 0) marker = "***";
        else if (f.category == 1) marker = "** ";
        else if (f.category == 2) marker = "*  ";
        else marker = "!  ";
        
        ss << std::setw(5) << (i + 1) << " | "
           << std::setw(4) << marker << " | "
           << std::setw(25) << std::left << selectedFeatures[f.index] << " | "
           << std::setw(9) << std::fixed << std::setprecision(2) << f.percent << "% | "
           << std::setw(12) << std::fixed << std::setprecision(4) << f.soloPval << " | "
           << std::setw(14) << std::fixed << std::setprecision(4) << f.unbiasedPval << "\n";
    }
    
    // Summary statistics
    int highSig = 0, sig = 0, marginal = 0, noise = 0;
    for (const auto& f : features) {
        if (f.category == 0) highSig++;
        else if (f.category == 1) sig++;
        else if (f.category == 2) marginal++;
        else noise++;
    }
    
    ss << "\nSummary:\n";
    ss << "  Highly significant features (p ≤ 0.05): " << highSig << "\n";
    ss << "  Significant features (p ≤ 0.10): " << sig << "\n";
    ss << "  Marginal features (p ≤ 0.20): " << marginal << "\n";
    ss << "  Likely noise (p > 0.20): " << noise << "\n";
    
    // Smart recommendations
    ss << "\nRECOMMENDATIONS:\n";
    ss << std::string(60, '-') << "\n";
    
    // Find top N significant features
    std::vector<std::string> recommended;
    std::vector<std::string> caution;
    
    for (const auto& f : features) {
        if (f.category <= 1 && recommended.size() < 10) { // p <= 0.10
            recommended.push_back(selectedFeatures[f.index]);
        }
        if (f.percent > 20.0 && f.soloPval > 0.30) { // High % but high p-value
            caution.push_back(selectedFeatures[f.index] + 
                " (" + std::to_string(static_cast<int>(f.percent)) + "%, p=" + 
                std::to_string(f.soloPval).substr(0, 5) + ")");
        }
    }
    
    if (!recommended.empty()) {
        ss << "Top statistically significant features for modeling:\n";
        for (size_t i = 0; i < recommended.size(); i++) {
            ss << "  " << (i + 1) << ". " << recommended[i] << "\n";
        }
    }
    
    if (!caution.empty()) {
        ss << "\nCAUTION - High percentage but likely noise:\n";
        for (const auto& feat : caution) {
            ss << "  ! " << feat << "\n";
        }
    }
    
    if (m_mcptReps < 100) {
        ss << "\nNote: Only " << m_mcptReps << " MCPT replications used.\n";
        ss << "      Consider using 100-1000 replications for more reliable p-values.\n";
    }
    
    ss << "\nNote: Solo p-value = P(permuted >= original for this feature)\n";
    ss << "      Unbiased p-value = P(best permuted >= original for this feature)\n";
    
    AppendToResults(ss.str());
}

void LFSWindow::ProcessResults(int* f_binary, const std::vector<std::string>& selectedFeatures) {
    // This is now only called when MCPT is disabled - handled in main RunLFSAnalysis
}

bool LFSWindow::PrepareData(const std::vector<std::string>& features, const std::string& target) {
    if (!m_dataSource || !m_dataSource->HasData()) {
        return false;
    }
    
    auto df = m_dataSource->GetDataFrame();
    if (!df) {
        return false;
    }
    
    // Calculate actual row range
    int totalRows = df->num_rows();
    int startRow = std::max(0, std::min(m_startRow, totalRows - 1));
    int endRow = (m_endRow == -1 || m_endRow > totalRows) ? totalRows : m_endRow;
    endRow = std::max(startRow + 1, endRow); // Ensure at least 1 row
    
    // Get number of cases and variables
    m_n_cases = endRow - startRow;
    m_n_vars = features.size();
    
    // Clean up old database
    if (m_database) {
        delete[] m_database;
    }
    
    // Allocate database: n_cases x (n_vars + 1) for features + class
    m_database = new double[m_n_cases * (m_n_vars + 1)];
    
    // Extract feature data using get_column_view
    for (size_t j = 0; j < features.size(); j++) {
        auto col_result = df->get_column_view<double>(features[j]);
        if (!col_result.ok()) {
            AppendToResults("Error: Feature column not found: " + features[j] + "\n");
            delete[] m_database;
            m_database = nullptr;
            return false;
        }
        
        auto col = std::move(col_result).ValueOrDie();
        const double* col_data = col.data();
        
        for (int i = 0; i < m_n_cases; i++) {
            m_database[i * (m_n_vars + 1) + j] = col_data[startRow + i];
        }
    }
    
    // Extract and convert target to class IDs
    auto target_result = df->get_column_view<double>(target);
    if (!target_result.ok()) {
        AppendToResults("Error: Target column not found: " + target + "\n");
        delete[] m_database;
        m_database = nullptr;
        return false;
    }
    
    auto targetCol = std::move(target_result).ValueOrDie();
    const double* target_data = targetCol.data();
    
    // Convert target data to vector for discretization (only the selected range)
    std::vector<double> targetValues;
    for (int i = 0; i < m_n_cases; i++) {
        targetValues.push_back(target_data[startRow + i]);
    }
    
    // Discretize target into bins
    std::vector<int> classIds = DiscretizeTarget(targetValues, m_targetBins);
    
    // Store discretized class IDs in database
    for (int i = 0; i < m_n_cases; i++) {
        m_database[i * (m_n_vars + 1) + m_n_vars] = static_cast<double>(classIds[i]);
    }
    
    // Count actual number of classes (might be less than m_targetBins if ties)
    std::set<int> uniqueClasses(classIds.begin(), classIds.end());
    int actualClasses = uniqueClasses.size();
    
    std::stringstream ss;
    ss << "Data prepared: " << m_n_cases << " cases, " << m_n_vars 
       << " features, " << actualClasses << " classes (from " << m_targetBins << " bins)\n\n";
    AppendToResults(ss.str());
    
    return true;
}

std::vector<int> LFSWindow::DiscretizeTarget(const std::vector<double>& targetValues, int nbins) {
    int n = targetValues.size();
    std::vector<int> classIds(n);
    
    // Create indices for sorting
    std::vector<int> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    
    // Sort indices by target values
    std::sort(indices.begin(), indices.end(), 
        [&targetValues](int a, int b) { return targetValues[a] < targetValues[b]; });
    
    // Calculate bin boundaries - equal frequency binning
    std::vector<double> boundaries(nbins - 1);
    for (int i = 1; i < nbins; i++) {
        int idx = (i * n) / nbins - 1;
        idx = std::min(idx, n - 1);
        boundaries[i - 1] = targetValues[indices[idx]];
    }
    
    // Adjust boundaries to avoid splitting ties
    for (int i = 0; i < nbins - 1; i++) {
        double boundary = boundaries[i];
        // Find the last occurrence of values equal to boundary
        int j = (i + 1) * n / nbins - 1;
        while (j < n - 1 && targetValues[indices[j]] == targetValues[indices[j + 1]]) {
            j++;
        }
        if (j < n - 1) {
            boundaries[i] = (targetValues[indices[j]] + targetValues[indices[j + 1]]) / 2.0;
        }
    }
    
    // Assign class IDs based on boundaries
    for (int i = 0; i < n; i++) {
        double val = targetValues[i];
        int bin = 0;
        for (int j = 0; j < nbins - 1; j++) {
            if (val > boundaries[j]) {
                bin = j + 1;
            }
        }
        classIds[i] = bin;
    }
    
    return classIds;
}