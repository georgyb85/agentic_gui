#include "IndicatorConfig.hpp"
#include "IndicatorId.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace tssb {

ConfigParseResult IndicatorConfigParser::parse_file(const std::string& file_path)
{
    ConfigParseResult result;
    std::ifstream file(file_path);

    if (!file.is_open()) {
        result.error_message = "Cannot open file: " + file_path;
        return result;
    }

    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;
        ++result.total_lines;

        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines
        if (line.empty()) {
            ++result.blank_lines;
            continue;
        }

        // Skip comments (lines starting with ; or #)
        if (line[0] == ';' || line[0] == '#') {
            ++result.comment_lines;
            continue;
        }

        // Parse line
        auto def = parse_line(line, line_number);
        if (def.has_value()) {
            result.definitions.push_back(def.value());
            ++result.parsed_indicators;
        } else {
            // Invalid line - could add warnings
        }
    }

    result.success = true;
    return result;
}

std::optional<IndicatorDefinition> IndicatorConfigParser::parse_line(
    const std::string& line,
    int line_number)
{
    // Find colon separator
    auto colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    IndicatorDefinition def;
    def.line_number = line_number;
    def.source_line = line;

    // Extract variable name (before colon)
    def.variable_name = line.substr(0, colon_pos);
    def.variable_name.erase(0, def.variable_name.find_first_not_of(" \t"));
    def.variable_name.erase(def.variable_name.find_last_not_of(" \t") + 1);

    if (def.variable_name.empty()) {
        return std::nullopt;
    }

    // Extract and tokenize indicator definition (after colon)
    std::string definition = line.substr(colon_pos + 1);
    auto tokens = tokenize(definition);

    if (tokens.empty()) {
        return std::nullopt;
    }

    // Build indicator type (concatenate non-numeric, non-flag tokens at start)
    std::vector<std::string> type_parts;
    std::size_t i = 0;

    while (i < tokens.size()) {
        const auto& token = tokens[i];

        // Stop at first numeric value or flag
        if (is_flag(token)) {
            break;
        }

        // Check if it's a number
        char* end;
        std::strtod(token.c_str(), &end);
        if (end != token.c_str() && *end == '\0') {
            // It's a number, stop building type
            break;
        }

        type_parts.push_back(token);
        ++i;
    }

    def.indicator_type = "";
    for (const auto& part : type_parts) {
        if (!def.indicator_type.empty()) {
            def.indicator_type += " ";
        }
        def.indicator_type += part;
    }

    // Parse remaining tokens as parameters or flags
    while (i < tokens.size()) {
        const auto& token = tokens[i];

        if (is_flag(token)) {
            auto [key, value] = parse_flag(token);
            def.flags[key] = value;
        } else {
            // Try to parse as number
            char* end;
            double value = std::strtod(token.c_str(), &end);
            if (end != token.c_str() && *end == '\0') {
                def.params.push_back(value);
            }
        }
        ++i;
    }

    return def;
}

std::vector<std::string> IndicatorConfigParser::tokenize(const std::string& str)
{
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;

    while (iss >> token) {
        tokens.push_back(token);
    }

    return tokens;
}

bool IndicatorConfigParser::is_flag(const std::string& token)
{
    if (token.size() >= 2 && token[0] == '-' && token[1] == '-') {
        return true;  // --flag or --flag=value
    }
    if (token.size() >= 3 && token[0] == '[' && token.back() == ']') {
        return true;  // [FLAG=value]
    }
    return false;
}

std::pair<std::string, std::string> IndicatorConfigParser::parse_flag(const std::string& token)
{
    std::string cleaned = token;

    // Remove -- prefix
    if (cleaned.size() >= 2 && cleaned[0] == '-' && cleaned[1] == '-') {
        cleaned = cleaned.substr(2);
    }

    // Remove [ ] brackets
    if (!cleaned.empty() && cleaned[0] == '[') {
        cleaned = cleaned.substr(1);
    }
    if (!cleaned.empty() && cleaned.back() == ']') {
        cleaned.pop_back();
    }

    // Split on =
    auto eq_pos = cleaned.find('=');
    if (eq_pos != std::string::npos) {
        std::string key = cleaned.substr(0, eq_pos);
        std::string value = cleaned.substr(eq_pos + 1);

        // Convert key to lowercase
        std::transform(key.begin(), key.end(), key.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        return {key, value};
    }

    // Flag without value (treat as boolean true)
    std::transform(cleaned.begin(), cleaned.end(), cleaned.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    return {cleaned, "true"};
}

bool IndicatorConfigParser::validate_definition(
    const IndicatorDefinition& def,
    std::string& error)
{
    if (def.variable_name.empty()) {
        error = "Variable name is empty";
        return false;
    }

    if (def.indicator_type.empty()) {
        error = "Indicator type is empty";
        return false;
    }

    // Could add per-indicator parameter count validation here

    return true;
}

bool IndicatorResultWriter::write_csv(
    const std::string& output_path,
    const std::vector<std::string>& variable_names,
    const std::vector<std::vector<double>>& results,
    const std::vector<std::string>& dates,
    const std::vector<std::string>& times)
{
    std::ofstream file(output_path);
    if (!file.is_open()) {
        return false;
    }

    // Determine number of rows
    std::size_t num_rows = results.empty() ? 0 : results[0].size();

    // Write header
    file << "bar";
    if (!dates.empty()) {
        file << ",date";
    }
    if (!times.empty()) {
        file << ",time";
    }
    for (const auto& name : variable_names) {
        file << "," << name;
    }
    file << "\n";

    // Write data rows
    for (std::size_t row = 0; row < num_rows; ++row) {
        file << row;

        if (!dates.empty() && row < dates.size()) {
            file << "," << dates[row];
        }
        if (!times.empty() && row < times.size()) {
            file << "," << times[row];
        }

        for (const auto& col : results) {
            if (row < col.size()) {
                file << "," << col[row];
            } else {
                file << ",";
            }
        }
        file << "\n";
    }

    return true;
}

bool IndicatorResultWriter::write_tssb_format(
    const std::string& output_path,
    const std::vector<std::string>& variable_names,
    const std::vector<std::vector<double>>& results,
    const std::vector<std::string>& dates,
    const std::vector<std::string>& times)
{
    std::ofstream file(output_path);
    if (!file.is_open()) {
        return false;
    }

    // Determine number of rows
    std::size_t num_rows = results.empty() ? 0 : results[0].size();

    // Write header (space-separated)
    if (!dates.empty()) {
        file << "date ";
    }
    if (!times.empty()) {
        file << "time ";
    }
    for (std::size_t i = 0; i < variable_names.size(); ++i) {
        if (i > 0) file << " ";
        file << variable_names[i];
    }
    file << "\n";

    // Write data rows
    for (std::size_t row = 0; row < num_rows; ++row) {
        if (!dates.empty() && row < dates.size()) {
            file << dates[row] << " ";
        }
        if (!times.empty() && row < times.size()) {
            file << times[row] << " ";
        }

        for (std::size_t col = 0; col < results.size(); ++col) {
            if (col > 0) file << " ";
            if (row < results[col].size()) {
                file << results[col][row];
            }
        }
        file << "\n";
    }

    return true;
}

} // namespace tssb
