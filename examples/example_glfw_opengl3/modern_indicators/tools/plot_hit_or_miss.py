#!/usr/bin/env python3
"""
Plot Hit or Miss (TGT_*) indicators comparing TSSB CSV output vs our computed values.
"""

import subprocess
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

def parse_ohlcv(filename):
    """Parse OHLCV text file."""
    dates = []
    times = []
    opens = []
    highs = []
    lows = []
    closes = []
    volumes = []

    with open(filename, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 7:
                dates.append(int(parts[0]))
                times.append(int(parts[1]))
                opens.append(float(parts[2]))
                highs.append(float(parts[3]))
                lows.append(float(parts[4]))
                closes.append(float(parts[5]))
                volumes.append(float(parts[6]))

    return {
        'date': dates,
        'time': times,
        'open': opens,
        'high': highs,
        'low': lows,
        'close': closes,
        'volume': volumes
    }

def parse_tssb_csv(filename):
    """Parse TSSB CSV output."""
    with open(filename, 'r') as f:
        # Read header
        header = f.readline().strip().split()

        # Find TGT column indices
        tgt_115_idx = header.index('TGT_115') if 'TGT_115' in header else None
        tgt_315_idx = header.index('TGT_315') if 'TGT_315' in header else None
        tgt_555_idx = header.index('TGT_555') if 'TGT_555' in header else None

        # Read data
        dates = []
        times = []
        tgt_115 = []
        tgt_315 = []
        tgt_555 = []

        for line in f:
            parts = line.strip().split()
            if len(parts) > max(filter(None, [tgt_115_idx, tgt_315_idx, tgt_555_idx])):
                dates.append(int(parts[0]))
                times.append(int(parts[1]))

                if tgt_115_idx:
                    try:
                        tgt_115.append(float(parts[tgt_115_idx]))
                    except:
                        tgt_115.append(np.nan)

                if tgt_315_idx:
                    try:
                        tgt_315.append(float(parts[tgt_315_idx]))
                    except:
                        tgt_315.append(np.nan)

                if tgt_555_idx:
                    try:
                        tgt_555.append(float(parts[tgt_555_idx]))
                    except:
                        tgt_555.append(np.nan)

    return {
        'date': dates,
        'time': times,
        'TGT_115': tgt_115,
        'TGT_315': tgt_315,
        'TGT_555': tgt_555
    }

def run_indicator_computation(build_dir, ohlcv_file, csv_file):
    """Run our test program and capture output."""
    cmd = [f"{build_dir}/test_hit_or_miss", ohlcv_file, csv_file]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.stdout

def extract_computed_values(output_text, indicator_name):
    """Extract computed values from test program output."""
    lines = output_text.split('\n')

    # Find the section for this indicator
    in_section = False
    computed_values = {}

    for line in lines:
        if indicator_name in line and 'Up=' in line:
            in_section = True
            continue

        if in_section and 'Bar' in line and 'Expected' in line:
            # This is a data line
            parts = line.split()
            if len(parts) >= 4:
                try:
                    bar_idx = int(parts[0])
                    expected = float(parts[1])
                    computed = float(parts[2])
                    computed_values[bar_idx] = computed
                except:
                    pass

        if in_section and 'Summary:' in line:
            break

    return computed_values

def align_data(ohlcv, csv_data):
    """Align OHLCV bars with CSV bars by date/time."""
    # Create a mapping from (date, time) to OHLCV index
    ohlcv_map = {}
    for i, (date, time) in enumerate(zip(ohlcv['date'], ohlcv['time'])):
        ohlcv_map[(date, time)] = i

    # Align CSV data to OHLCV indices
    aligned_115 = np.full(len(ohlcv['date']), np.nan)
    aligned_315 = np.full(len(ohlcv['date']), np.nan)
    aligned_555 = np.full(len(ohlcv['date']), np.nan)

    for i, (date, time) in enumerate(zip(csv_data['date'], csv_data['time'])):
        key = (date, time)
        if key in ohlcv_map:
            ohlcv_idx = ohlcv_map[key]
            if i < len(csv_data['TGT_115']):
                aligned_115[ohlcv_idx] = csv_data['TGT_115'][i]
            if i < len(csv_data['TGT_315']):
                aligned_315[ohlcv_idx] = csv_data['TGT_315'][i]
            if i < len(csv_data['TGT_555']):
                aligned_555[ohlcv_idx] = csv_data['TGT_555'][i]

    return aligned_115, aligned_315, aligned_555

def compute_our_indicators(build_dir, ohlcv_file):
    """Use C++ library to compute indicators directly."""
    # For now, we'll extract from test output
    # In production, we could use Python bindings
    return None

def main():
    # Paths
    base_dir = Path("/mnt/c/masters/timothy masters")
    build_dir = base_dir / "modern_indicators/build"
    ohlcv_file = base_dir / "btc25_3.txt"
    csv_file = base_dir / "BTC25_3 HM.CSV"

    print("Loading OHLCV data...")
    ohlcv = parse_ohlcv(ohlcv_file)
    n_bars = len(ohlcv['date'])
    print(f"Loaded {n_bars} OHLCV bars")

    print("Loading TSSB CSV data...")
    csv_data = parse_tssb_csv(csv_file)
    print(f"Loaded {len(csv_data['date'])} CSV bars")

    print("Aligning data...")
    csv_115, csv_315, csv_555 = align_data(ohlcv, csv_data)

    print("Running indicator computation...")
    output = run_indicator_computation(str(build_dir), str(ohlcv_file), str(csv_file))

    # Extract computed values from test output
    # We need to parse the detailed output to get all bars
    # For now, let's create a simpler approach: run a custom exporter

    print("Creating export program...")
    # Create a simple C++ program to export all computed values
    export_code = '''
#include "IndicatorEngine.hpp"
#include "SingleIndicatorLibrary.hpp"
#include "validation/DataParsers.hpp"
#include <iostream>
#include <iomanip>

using namespace tssb;
using namespace tssb::validation;

int main(int argc, char** argv) {
    auto ohlcv_bars = OHLCVParser::parse_file(argv[1]);
    SingleMarketSeries series = OHLCVParser::to_series(ohlcv_bars);

    // Compute all three indicators
    SingleIndicatorRequest req115, req315, req555;

    req115.id = SingleIndicatorId::HitOrMiss;
    req115.name = "TGT_115";
    req115.params[0] = 1; req115.params[1] = 1; req115.params[2] = 5; req115.params[3] = 0;

    req315.id = SingleIndicatorId::HitOrMiss;
    req315.name = "TGT_315";
    req315.params[0] = 3; req315.params[1] = 1; req315.params[2] = 5; req315.params[3] = 0;

    req555.id = SingleIndicatorId::HitOrMiss;
    req555.name = "TGT_555";
    req555.params[0] = 5; req555.params[1] = 5; req555.params[2] = 5; req555.params[3] = 0;

    auto result115 = compute_single_indicator(series, req115);
    auto result315 = compute_single_indicator(series, req315);
    auto result555 = compute_single_indicator(series, req555);

    // Output all values
    std::cout << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < ohlcv_bars.size(); ++i) {
        std::cout << i << " "
                  << result115.values[i] << " "
                  << result315.values[i] << " "
                  << result555.values[i] << "\\n";
    }

    return 0;
}
'''

    export_cpp = build_dir / "../tools/export_hit_or_miss.cpp"
    with open(export_cpp, 'w') as f:
        f.write(export_code)

    # Build and run the exporter
    print("Building exporter...")
    cmake_add = """
add_executable(export_hit_or_miss tools/export_hit_or_miss.cpp)
target_link_libraries(export_hit_or_miss PRIVATE tssb_modern_indicators)
"""

    # Check if already in CMakeLists
    cmake_file = build_dir / "../CMakeLists.txt"
    with open(cmake_file, 'r') as f:
        cmake_content = f.read()

    if 'export_hit_or_miss' not in cmake_content:
        with open(cmake_file, 'a') as f:
            f.write("\n" + cmake_add)

    # Build
    subprocess.run(['cmake', '--build', str(build_dir), '--target', 'export_hit_or_miss'],
                   capture_output=True)

    # Run exporter
    print("Exporting computed values...")
    result = subprocess.run([str(build_dir / 'export_hit_or_miss'), str(ohlcv_file)],
                           capture_output=True, text=True)

    # Parse output
    computed_115 = np.full(n_bars, np.nan)
    computed_315 = np.full(n_bars, np.nan)
    computed_555 = np.full(n_bars, np.nan)

    for line in result.stdout.strip().split('\n'):
        parts = line.split()
        if len(parts) == 4:
            idx = int(parts[0])
            computed_115[idx] = float(parts[1])
            computed_315[idx] = float(parts[2])
            computed_555[idx] = float(parts[3])

    # Create plots
    print("Creating plots...")

    indicators = [
        ('TGT_115', csv_115, computed_115),
        ('TGT_315', csv_315, computed_315),
        ('TGT_555', csv_555, computed_555)
    ]

    for name, csv_vals, computed_vals in indicators:
        # Find valid range
        valid = ~np.isnan(csv_vals) & ~np.isnan(computed_vals)
        valid_indices = np.where(valid)[0]

        if len(valid_indices) == 0:
            print(f"Warning: No valid data for {name}")
            continue

        start_idx = valid_indices[0]
        end_idx = valid_indices[-1]

        # Full plot
        fig, axes = plt.subplots(3, 1, figsize=(16, 12))
        fig.suptitle(f'{name} Comparison: TSSB CSV vs Computed', fontsize=16, fontweight='bold')

        # Plot 1: Full series
        ax = axes[0]
        x = np.arange(start_idx, end_idx + 1)
        ax.plot(x, csv_vals[start_idx:end_idx+1], 'b-', label='TSSB CSV', alpha=0.7, linewidth=1)
        ax.plot(x, computed_vals[start_idx:end_idx+1], 'r--', label='Computed', alpha=0.7, linewidth=1)
        ax.set_xlabel('Bar Index')
        ax.set_ylabel('Value')
        ax.set_title('Full Series')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # Plot 2: Zoomed section (first 500 bars)
        ax = axes[1]
        zoom_end = min(start_idx + 500, end_idx + 1)
        x_zoom = np.arange(start_idx, zoom_end)
        ax.plot(x_zoom, csv_vals[start_idx:zoom_end], 'b-', label='TSSB CSV', alpha=0.7, linewidth=1.5)
        ax.plot(x_zoom, computed_vals[start_idx:zoom_end], 'r--', label='Computed', alpha=0.7, linewidth=1.5)
        ax.set_xlabel('Bar Index')
        ax.set_ylabel('Value')
        ax.set_title('First 500 Valid Bars (Zoomed)')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # Plot 3: Difference
        ax = axes[2]
        diff = computed_vals[start_idx:end_idx+1] - csv_vals[start_idx:end_idx+1]
        ax.plot(x, diff, 'g-', label='Difference (Computed - CSV)', alpha=0.7, linewidth=1)
        ax.axhline(y=0, color='k', linestyle='--', alpha=0.3)
        ax.set_xlabel('Bar Index')
        ax.set_ylabel('Difference')
        ax.set_title('Difference Plot')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # Add statistics
        mae = np.nanmean(np.abs(diff))
        max_error = np.nanmax(np.abs(diff))
        mean_rel_error = np.nanmean(np.abs(diff / csv_vals[start_idx:end_idx+1])) * 100

        stats_text = f'MAE: {mae:.4f}\nMax Error: {max_error:.4f}\nMean Rel Error: {mean_rel_error:.2f}%'
        fig.text(0.02, 0.02, stats_text, fontsize=10, verticalalignment='bottom',
                bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

        plt.tight_layout(rect=[0, 0.03, 1, 0.97])

        output_file = base_dir / f'hit_or_miss_{name.lower()}_comparison.png'
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        print(f"Saved {output_file}")
        plt.close()

    # Create a combined overview plot
    fig, axes = plt.subplots(3, 1, figsize=(16, 12))
    fig.suptitle('All TGT_* Indicators: TSSB CSV vs Computed (First 1000 bars)',
                 fontsize=16, fontweight='bold')

    for idx, (name, csv_vals, computed_vals) in enumerate(indicators):
        ax = axes[idx]
        valid = ~np.isnan(csv_vals) & ~np.isnan(computed_vals)
        valid_indices = np.where(valid)[0]

        if len(valid_indices) > 0:
            start = valid_indices[0]
            end = min(start + 1000, valid_indices[-1] + 1)
            x = np.arange(start, end)

            ax.plot(x, csv_vals[start:end], 'b-', label='TSSB CSV', alpha=0.6, linewidth=1)
            ax.plot(x, computed_vals[start:end], 'r--', label='Computed', alpha=0.6, linewidth=1)
            ax.set_ylabel('Value')
            ax.set_title(name)
            ax.legend()
            ax.grid(True, alpha=0.3)

    axes[-1].set_xlabel('Bar Index')
    plt.tight_layout(rect=[0, 0, 1, 0.97])

    output_file = base_dir / 'hit_or_miss_all_comparison.png'
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"Saved {output_file}")
    plt.close()

    print("\nPlots created successfully!")
    print(f"Output files:")
    print(f"  - hit_or_miss_tgt_115_comparison.png")
    print(f"  - hit_or_miss_tgt_315_comparison.png")
    print(f"  - hit_or_miss_tgt_555_comparison.png")
    print(f"  - hit_or_miss_all_comparison.png")

if __name__ == '__main__':
    main()
