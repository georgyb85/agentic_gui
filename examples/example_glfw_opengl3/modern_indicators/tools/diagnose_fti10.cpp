#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <btc25_3.txt> <BTC25_3 HM.CSV>\n";
        return 1;
    }

    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Compute FTI10
    SingleIndicatorRequest req;
    req.id = SingleIndicatorId::FtiBestFti;
    req.name = "FTI10";
    req.params[0] = 36;  // BlockSize
    req.params[1] = 6;   // HalfLength
    req.params[2] = 10;  // Period

    auto result = compute_single_indicator(series, req);

    // Read CSV directly
    std::ifstream csv_file(argv[2]);
    std::string line;

    // Read header to find FTI10 column
    std::getline(csv_file, line);
    std::istringstream header_stream(line);
    std::string col;
    int fti10_col = -1;
    int col_idx = 0;
    while (header_stream >> col) {
        if (col == "FTI10") {
            fti10_col = col_idx;
            break;
        }
        col_idx++;
    }

    std::cout << "FTI10 column index: " << fti10_col << "\n\n";

    // Read first 20 data rows
    std::cout << std::setw(6) << "Row"
              << std::setw(12) << "Date"
              << std::setw(8) << "Time"
              << std::setw(14) << "CSV_FTI10"
              << std::setw(14) << "Our_FTI10"
              << std::setw(14) << "Difference\n";
    std::cout << std::string(68, '-') << "\n";

    int row = 1;
    while (std::getline(csv_file, line) && row <= 1095) {
        std::istringstream line_stream(line);
        std::string field;
        int date, time;
        double csv_fti10 = 0.0;

        line_stream >> date >> time;

        for (int i = 2; i <= fti10_col; ++i) {
            line_stream >> field;
            if (i == fti10_col) {
                try {
                    csv_fti10 = std::stod(field);
                } catch (...) {
                    csv_fti10 = 0.0;
                }
            }
        }

        if (row >= 1078 && row <= 1095) {
            double our_value = (row < result.values.size()) ? result.values[row] : 0.0;
            double diff = our_value - csv_fti10;

            std::cout << std::setw(6) << row
                      << std::setw(12) << date
                      << std::setw(8) << time
                      << std::setw(14) << std::setprecision(6) << std::fixed << csv_fti10
                      << std::setw(14) << our_value
                      << std::setw(14) << diff << "\n";
        }

        row++;
    }

    return 0;
}
