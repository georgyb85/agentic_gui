#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>

namespace tssb {

/// Represents a single indicator definition from config file
struct IndicatorDefinition {
    std::string variable_name;      // e.g., "RSI_S"
    std::string indicator_type;     // e.g., "RSI"
    std::vector<double> params;     // Numeric parameters
    std::map<std::string, std::string> flags;  // Optional flags/modifiers

    // Original line for error reporting
    std::string source_line;
    int line_number = 0;
};

/// Result of parsing a config file
struct ConfigParseResult {
    bool success = false;
    std::vector<IndicatorDefinition> definitions;
    std::string error_message;

    // Statistics
    int total_lines = 0;
    int parsed_indicators = 0;
    int comment_lines = 0;
    int blank_lines = 0;
};

/// Parser for TSSB-style config files with extensions
class IndicatorConfigParser {
public:
    /// Parse config file (var.txt format)
    ///
    /// Basic syntax:
    ///   VARIABLE_NAME: INDICATOR_TYPE param1 param2 ...
    ///
    /// Extended syntax with flags:
    ///   VARIABLE_NAME: INDICATOR_TYPE param1 param2 --flag1=value --flag2=value
    ///   VARIABLE_NAME: INDICATOR_TYPE param1 param2 [FLAG1=value]
    ///
    /// Examples:
    ///   RSI_S: RSI 10
    ///   TREND_S100: LINEAR PER ATR 10 100
    ///   ATR_RATIO_S: ATR RATIO 10 2.5 --method=normal_cdf
    ///   VOL_MOM_S: VOLUME MOMENTUM 10 5 --order=down_first
    ///   ADX_S: ADX 14 --method=wilder
    static ConfigParseResult parse_file(const std::string& file_path);

    /// Parse a single line
    static std::optional<IndicatorDefinition> parse_line(
        const std::string& line,
        int line_number = 0
    );

    /// Validate that all required parameters are present
    static bool validate_definition(const IndicatorDefinition& def, std::string& error);

private:
    /// Tokenize a line into words
    static std::vector<std::string> tokenize(const std::string& str);

    /// Check if a token is a flag (starts with -- or enclosed in [])
    static bool is_flag(const std::string& token);

    /// Parse a flag token into key-value pair
    static std::pair<std::string, std::string> parse_flag(const std::string& token);
};

/// Write indicator results to CSV file
class IndicatorResultWriter {
public:
    /// Write results to CSV with proper alignment
    /// Format: bar_index, date, time, var1, var2, var3, ...
    static bool write_csv(
        const std::string& output_path,
        const std::vector<std::string>& variable_names,
        const std::vector<std::vector<double>>& results,
        const std::vector<std::string>& dates = {},
        const std::vector<std::string>& times = {}
    );

    /// Write in TSSB-compatible format (space-separated)
    static bool write_tssb_format(
        const std::string& output_path,
        const std::vector<std::string>& variable_names,
        const std::vector<std::vector<double>>& results,
        const std::vector<std::string>& dates = {},
        const std::vector<std::string>& times = {}
    );
};

} // namespace tssb
