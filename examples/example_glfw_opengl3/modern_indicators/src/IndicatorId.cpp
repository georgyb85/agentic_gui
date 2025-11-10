#include "IndicatorId.hpp"

namespace tssb {

std::string_view to_string(SingleIndicatorId id)
{
    switch (id) {
        case SingleIndicatorId::RSI: return "RSI";
        case SingleIndicatorId::DetrendedRsi: return "DETRENDED RSI";
        case SingleIndicatorId::Stochastic: return "STOCHASTIC";
        case SingleIndicatorId::StochasticRsi: return "STOCHRSI";
        case SingleIndicatorId::MovingAverageDifference: return "MA DIFFERENCE";
        case SingleIndicatorId::Macd: return "MACD";
        case SingleIndicatorId::Ppo: return "PPO";
        case SingleIndicatorId::LinearTrend: return "LINEAR TREND";
        case SingleIndicatorId::QuadraticTrend: return "QUADRATIC TREND";
        case SingleIndicatorId::CubicTrend: return "CUBIC TREND";
        case SingleIndicatorId::PriceIntensity: return "PRICE INTENSITY";
        case SingleIndicatorId::Adx: return "ADX";
        case SingleIndicatorId::AroonUp: return "AROON UP";
        case SingleIndicatorId::AroonDown: return "AROON DOWN";
        case SingleIndicatorId::AroonDiff: return "AROON DIFF";
        case SingleIndicatorId::CloseMinusMovingAverage: return "CLOSE MINUS MOVING AVERAGE";
        case SingleIndicatorId::LinearDeviation: return "LINEAR DEVIATION";
        case SingleIndicatorId::QuadraticDeviation: return "QUADRATIC DEVIATION";
        case SingleIndicatorId::CubicDeviation: return "CUBIC DEVIATION";
        case SingleIndicatorId::PriceChangeOscillator: return "PRICE CHANGE OSCILLATOR";
        case SingleIndicatorId::PriceVarianceRatio: return "PRICE VARIANCE RATIO";
        case SingleIndicatorId::ChangeVarianceRatio: return "CHANGE VARIANCE RATIO";
        case SingleIndicatorId::BollingerWidth: return "BOLLINGER WIDTH";
        case SingleIndicatorId::AtrRatio: return "ATR RATIO";
        case SingleIndicatorId::IntradayIntensity: return "INTRADAY INTENSITY";
        case SingleIndicatorId::MoneyFlow: return "MONEY FLOW";
        case SingleIndicatorId::Reactivity: return "REACTIVITY";
        case SingleIndicatorId::PriceVolumeFit: return "PRICE VOLUME FIT";
        case SingleIndicatorId::VolumeWeightedMaRatio: return "VOLUME WEIGHTED MA RATIO";
        case SingleIndicatorId::NormalizedOnBalanceVolume: return "NORMALIZED ON BALANCE VOLUME";
        case SingleIndicatorId::DeltaOnBalanceVolume: return "DELTA ON BALANCE VOLUME";
        case SingleIndicatorId::NormalizedPositiveVolumeIndex: return "NORMALIZED POSITIVE VOLUME INDEX";
        case SingleIndicatorId::NormalizedNegativeVolumeIndex: return "NORMALIZED NEGATIVE VOLUME INDEX";
        case SingleIndicatorId::VolumeMomentum: return "VOLUME MOMENTUM";
        case SingleIndicatorId::Entropy: return "ENTROPY";
        case SingleIndicatorId::MutualInformation: return "MUTUAL INFORMATION";
        case SingleIndicatorId::FtiLowpass: return "FTI LOWPASS";
        case SingleIndicatorId::FtiBestPeriod: return "FTI BEST PERIOD";
        case SingleIndicatorId::FtiBestWidth: return "FTI BEST WIDTH";
        case SingleIndicatorId::FtiBestFti: return "FTI BEST FTI";
        case SingleIndicatorId::FtiLargest: return "FTI LARGEST FTI";
        // Morlet wavelets
        case SingleIndicatorId::RealMorlet: return "REAL MORLET";
        case SingleIndicatorId::ImagMorlet: return "IMAG MORLET";
        case SingleIndicatorId::RealDiffMorlet: return "REAL DIFF MORLET";
        case SingleIndicatorId::ImagDiffMorlet: return "IMAG DIFF MORLET";
        case SingleIndicatorId::RealProductMorlet: return "REAL PRODUCT MORLET";
        case SingleIndicatorId::ImagProductMorlet: return "IMAG PRODUCT MORLET";
        case SingleIndicatorId::PhaseMorlet: return "PHASE MORLET";
        // Daubechies wavelets
        case SingleIndicatorId::DaubMean: return "DAUB MEAN";
        case SingleIndicatorId::DaubMin: return "DAUB MIN";
        case SingleIndicatorId::DaubMax: return "DAUB MAX";
        case SingleIndicatorId::DaubStd: return "DAUB STD";
        case SingleIndicatorId::DaubEnergy: return "DAUB ENERGY";
        case SingleIndicatorId::DaubNlEnergy: return "DAUB NL ENERGY";
        case SingleIndicatorId::DaubCurve: return "DAUB CURVE";
        // Additional RSI variants
        case SingleIndicatorId::CondRsi: return "COND_RSI";
        case SingleIndicatorId::HiRsi: return "HI_RSI";
        case SingleIndicatorId::LoRsi: return "LO_RSI";
        case SingleIndicatorId::ThresholdedRsi: return "THRESHOLDED RSI";
        // Price momentum
        case SingleIndicatorId::PriceMomentum: return "PRICE MOMENTUM";
        // ADX variants
        case SingleIndicatorId::MinAdx: return "MIN ADX";
        case SingleIndicatorId::MaxAdx: return "MAX ADX";
        case SingleIndicatorId::ResidualMinAdx: return "RESIDUAL MIN ADX";
        case SingleIndicatorId::ResidualMaxAdx: return "RESIDUAL MAX ADX";
        case SingleIndicatorId::DeltaAdx: return "DELTA ADX";
        case SingleIndicatorId::AccelAdx: return "ACCEL ADX";
        // Volatility indicators
        case SingleIndicatorId::MinPriceVarianceRatio: return "MIN PRICE VARIANCE RATIO";
        case SingleIndicatorId::MaxPriceVarianceRatio: return "MAX PRICE VARIANCE RATIO";
        case SingleIndicatorId::MinChangeVarianceRatio: return "MIN CHANGE VARIANCE RATIO";
        case SingleIndicatorId::MaxChangeVarianceRatio: return "MAX CHANGE VARIANCE RATIO";
        case SingleIndicatorId::DeltaPriceVarianceRatio: return "DELTA PRICE VARIANCE RATIO";
        case SingleIndicatorId::DeltaChangeVarianceRatio: return "DELTA CHANGE VARIANCE RATIO";
        case SingleIndicatorId::DeltaBollingerWidth: return "DELTA BOLLINGER WIDTH";
        case SingleIndicatorId::PriceSkewness: return "PRICE SKEWNESS";
        case SingleIndicatorId::ChangeSkewness: return "CHANGE SKEWNESS";
        case SingleIndicatorId::PriceKurtosis: return "PRICE KURTOSIS";
        case SingleIndicatorId::ChangeKurtosis: return "CHANGE KURTOSIS";
        case SingleIndicatorId::DeltaPriceSkewness: return "DELTA PRICE SKEWNESS";
        case SingleIndicatorId::DeltaChangeSkewness: return "DELTA CHANGE SKEWNESS";
        case SingleIndicatorId::DeltaPriceKurtosis: return "DELTA PRICE KURTOSIS";
        case SingleIndicatorId::DeltaChangeKurtosis: return "DELTA CHANGE KURTOSIS";
        case SingleIndicatorId::DeltaAtrRatio: return "DELTA ATR RATIO";
        // Volume indicators
        case SingleIndicatorId::DeltaVolumeMomentum: return "DELTA VOLUME MOMENTUM";
        case SingleIndicatorId::DeltaIntradayIntensity: return "DELTA INTRADAY INTENSITY";
        case SingleIndicatorId::ProductPriceVolume: return "PRODUCT PRICE VOLUME";
        case SingleIndicatorId::SumPriceVolume: return "SUM PRICE VOLUME";
        case SingleIndicatorId::DeltaProductPriceVolume: return "DELTA PRODUCT PRICE VOLUME";
        case SingleIndicatorId::DeltaSumPriceVolume: return "DELTA SUM PRICE VOLUME";
        case SingleIndicatorId::DeltaReactivity: return "DELTA REACTIVITY";
        case SingleIndicatorId::MinReactivity: return "MIN REACTIVITY";
        case SingleIndicatorId::MaxReactivity: return "MAX REACTIVITY";
        case SingleIndicatorId::DeltaPriceVolumeFit: return "DELTA PRICE VOLUME FIT";
        case SingleIndicatorId::DiffVolumeWeightedMaRatio: return "DIFF VOLUME WEIGHTED MA OVER MA";
        case SingleIndicatorId::NegativeVolumeIndex: return "NEGATIVE VOLUME INDEX";
        case SingleIndicatorId::DeltaPositiveVolumeIndex: return "DELTA POSITIVE VOLUME INDICATOR";
        case SingleIndicatorId::DeltaNegativeVolumeIndex: return "DELTA NEGATIVE VOLUME INDICATOR";
        // FTI variants
        case SingleIndicatorId::FtiMinorLowpass: return "FTI MINOR LOWPASS";
        case SingleIndicatorId::FtiMajorLowpass: return "FTI MAJOR LOWPASS";
        case SingleIndicatorId::FtiMinorFti: return "FTI MINOR FTI";
        case SingleIndicatorId::FtiMajorFti: return "FTI MAJOR FTI";
        case SingleIndicatorId::FtiLargestPeriod: return "FTI LARGEST PERIOD";
        case SingleIndicatorId::FtiMinorPeriod: return "FTI MINOR PERIOD";
        case SingleIndicatorId::FtiMajorPeriod: return "FTI MAJOR PERIOD";
        case SingleIndicatorId::FtiCrat: return "FTI CRAT";
        case SingleIndicatorId::FtiMinorBestCrat: return "FTI MINOR BEST CRAT";
        case SingleIndicatorId::FtiMajorBestCrat: return "FTI MAJOR BEST CRAT";
        case SingleIndicatorId::FtiBothBestCrat: return "FTI BOTH BEST CRAT";
        // Position indicators
        case SingleIndicatorId::NDayHigh: return "N DAY HIGH";
        case SingleIndicatorId::NDayLow: return "N DAY LOW";
        case SingleIndicatorId::NDayNarrower: return "N DAY NARROWER";
        case SingleIndicatorId::NDayWider: return "N DAY WIDER";
        case SingleIndicatorId::NewHigh: return "NEW HIGH";
        case SingleIndicatorId::NewLow: return "NEW LOW";
        case SingleIndicatorId::NewExtreme: return "NEW EXTREME";
        case SingleIndicatorId::OffHigh: return "OFF HIGH";
        case SingleIndicatorId::AboveLow: return "ABOVE LOW";
        case SingleIndicatorId::AboveMaBinary: return "ABOVE MA BI";
        case SingleIndicatorId::AboveMaTrinary: return "ABOVE MA TRI";
        case SingleIndicatorId::RocPositiveBinary: return "ROC POSITIVE BI";
        case SingleIndicatorId::RocPositiveTrinary: return "ROC POSITIVE TRI";
        // Persistence
        case SingleIndicatorId::UpPersist: return "UP PERSIST";
        case SingleIndicatorId::DownPersist: return "DOWN PERSIST";
        // Fit indicators
        case SingleIndicatorId::CubicFitValue: return "CUBIC FIT VALUE";
        case SingleIndicatorId::CubicFitError: return "CUBIC FIT ERROR";
        // Target variables
        case SingleIndicatorId::HitOrMiss: return "HIT OR MISS";
    }
    return "UNKNOWN";
}

std::string_view to_string(MultiIndicatorId id)
{
    switch (id) {
        case MultiIndicatorId::TrendRank: return "TREND RANK";
        case MultiIndicatorId::CmmaRank: return "CMMA RANK";
        case MultiIndicatorId::TrendMedian: return "TREND MEDIAN";
        case MultiIndicatorId::CmmaMedian: return "CMMA MEDIAN";
        case MultiIndicatorId::TrendRange: return "TREND RANGE";
        case MultiIndicatorId::CmmaRange: return "CMMA RANGE";
        case MultiIndicatorId::TrendIqr: return "TREND IQR";
        case MultiIndicatorId::CmmaIqr: return "CMMA IQR";
        case MultiIndicatorId::TrendClump: return "TREND CLUMP";
        case MultiIndicatorId::CmmaClump: return "CMMA CLUMP";
        case MultiIndicatorId::Mahalanobis: return "MAHAL";
        case MultiIndicatorId::AbsRatio: return "ABS RATIO";
        case MultiIndicatorId::AbsShift: return "ABS SHIFT";
        case MultiIndicatorId::Coherence: return "COHERENCE";
        case MultiIndicatorId::DeltaCoherence: return "DELTA COHERENCE";
        case MultiIndicatorId::JanusIndexMarket: return "JANUS INDEX MARKET";
        case MultiIndicatorId::JanusIndexDom: return "JANUS INDEX DOM";
        case MultiIndicatorId::JanusRawRs: return "JANUS RAW RS";
        case MultiIndicatorId::JanusFractileRs: return "JANUS FRACTILE RS";
        case MultiIndicatorId::JanusDeltaFractileRs: return "JANUS DELTA FRACTILE RS";
        case MultiIndicatorId::JanusRss: return "JANUS RSS";
        case MultiIndicatorId::JanusDeltaRss: return "JANUS DELTA RSS";
        case MultiIndicatorId::JanusDom: return "JANUS DOM";
        case MultiIndicatorId::JanusDoe: return "JANUS DOE";
        case MultiIndicatorId::JanusRawRm: return "JANUS RAW RM";
        case MultiIndicatorId::JanusFractileRm: return "JANUS FRACTILE RM";
        case MultiIndicatorId::JanusDeltaFractileRm: return "JANUS DELTA FRACTILE RM";
        case MultiIndicatorId::JanusRsLeaderEquity: return "JANUS RS LEADER EQUITY";
        case MultiIndicatorId::JanusRsLaggardEquity: return "JANUS RS LAGGARD EQUITY";
        case MultiIndicatorId::JanusRsLeaderAdvantage: return "JANUS RS LEADER ADVANTAGE";
        case MultiIndicatorId::JanusRsLaggardAdvantage: return "JANUS RS LAGGARD ADVANTAGE";
        case MultiIndicatorId::JanusRsPerformanceSpread: return "JANUS RS PS";
        case MultiIndicatorId::JanusRmLeaderEquity: return "JANUS RM LEADER EQUITY";
        case MultiIndicatorId::JanusRmLaggardEquity: return "JANUS RM LAGGARD EQUITY";
        case MultiIndicatorId::JanusRmLeaderAdvantage: return "JANUS RM LEADER ADVANTAGE";
        case MultiIndicatorId::JanusRmLaggardAdvantage: return "JANUS RM LAGGARD ADVANTAGE";
        case MultiIndicatorId::JanusRmPerformanceSpread: return "JANUS RM PS";
        case MultiIndicatorId::JanusCmaOos: return "JANUS CMA OOS";
        case MultiIndicatorId::JanusLeaderCmaOos: return "JANUS LEADER CMA OOS";
        case MultiIndicatorId::JanusOosAvg: return "JANUS OOS AVG";
    }
    return "UNKNOWN";
}

} // namespace tssb
