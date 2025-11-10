#!/usr/bin/env python3
"""
Generate comparison plots for CMMA indicators.
Creates both full and zoomed versions for each indicator.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys

def create_comparison_plot(df, indicator_name, csv_col, computed_col, output_prefix):
    """Create a 4-panel comparison plot for an indicator."""

    # Filter to valid data only
    valid_mask = np.isfinite(df[csv_col]) & np.isfinite(df[computed_col])
    df_valid = df[valid_mask].copy()

    if len(df_valid) == 0:
        print(f"WARNING: No valid data for {indicator_name}")
        return

    # Create figure with 4 subplots
    fig, axes = plt.subplots(4, 1, figsize=(16, 12))
    fig.suptitle(f'{indicator_name} - CSV vs Computed Comparison', fontsize=16, fontweight='bold')

    x = df_valid['bar'].values
    csv_vals = df_valid[csv_col].values
    computed_vals = df_valid[computed_col].values

    # Plot 1: Overlay of both lines
    axes[0].plot(x, csv_vals, 'b-', label='CSV', alpha=0.7, linewidth=1)
    axes[0].plot(x, computed_vals, 'r-', label='Computed', alpha=0.7, linewidth=1)
    axes[0].set_ylabel('Value')
    axes[0].set_title('Overlay Comparison')
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    # Plot 2: Difference (Computed - CSV)
    diff = computed_vals - csv_vals
    axes[1].plot(x, diff, 'g-', linewidth=1)
    axes[1].axhline(y=0, color='k', linestyle='--', alpha=0.3)
    axes[1].set_ylabel('Difference')
    axes[1].set_title(f'Difference (Computed - CSV), MAE = {np.mean(np.abs(diff)):.6f}')
    axes[1].grid(True, alpha=0.3)

    # Plot 3: Ratio (Computed / CSV)
    # Avoid division by zero
    ratio = np.where(np.abs(csv_vals) > 1e-10, computed_vals / csv_vals, 1.0)
    axes[2].plot(x, ratio, 'm-', linewidth=1)
    axes[2].axhline(y=1.0, color='k', linestyle='--', alpha=0.3)
    axes[2].set_ylabel('Ratio')
    axes[2].set_title('Ratio (Computed / CSV)')
    axes[2].grid(True, alpha=0.3)

    # Plot 4: Scatter plot with correlation
    axes[3].scatter(csv_vals, computed_vals, alpha=0.3, s=1)

    # Add diagonal line
    min_val = min(csv_vals.min(), computed_vals.min())
    max_val = max(csv_vals.max(), computed_vals.max())
    axes[3].plot([min_val, max_val], [min_val, max_val], 'r--', alpha=0.5, label='y=x')

    # Calculate correlation
    corr = np.corrcoef(csv_vals, computed_vals)[0, 1]
    axes[3].set_xlabel('CSV Value')
    axes[3].set_ylabel('Computed Value')
    axes[3].set_title(f'Scatter Plot (Correlation = {corr:.6f})')
    axes[3].legend()
    axes[3].grid(True, alpha=0.3)

    plt.tight_layout()

    # Save
    output_file = f"{output_prefix}_comparison.png"
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"Saved: {output_file}")
    plt.close()


def create_zoomed_plot(df, indicator_name, csv_col, computed_col, output_prefix, zoom_bars=200):
    """Create a zoomed-in comparison plot showing first zoom_bars valid bars."""

    # Filter to valid data only
    valid_mask = np.isfinite(df[csv_col]) & np.isfinite(df[computed_col])
    df_valid = df[valid_mask].copy()

    if len(df_valid) == 0:
        print(f"WARNING: No valid data for {indicator_name}")
        return

    # Take first zoom_bars
    df_zoom = df_valid.iloc[:zoom_bars].copy()

    # Create figure with 2 subplots
    fig, axes = plt.subplots(2, 1, figsize=(16, 8))
    fig.suptitle(f'{indicator_name} - Zoomed View (First {len(df_zoom)} bars)',
                 fontsize=16, fontweight='bold')

    x = df_zoom['bar'].values
    csv_vals = df_zoom[csv_col].values
    computed_vals = df_zoom[computed_col].values

    # Plot 1: Overlay
    axes[0].plot(x, csv_vals, 'b.-', label='CSV', alpha=0.7, linewidth=1.5, markersize=3)
    axes[0].plot(x, computed_vals, 'r.-', label='Computed', alpha=0.7, linewidth=1.5, markersize=3)
    axes[0].set_ylabel('Value')
    axes[0].set_title('Overlay Comparison (Zoomed)')
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    # Plot 2: Difference
    diff = computed_vals - csv_vals
    axes[1].plot(x, diff, 'g.-', linewidth=1.5, markersize=3)
    axes[1].axhline(y=0, color='k', linestyle='--', alpha=0.3)
    axes[1].set_xlabel('Bar Index')
    axes[1].set_ylabel('Difference')
    axes[1].set_title(f'Difference (Computed - CSV), MAE = {np.mean(np.abs(diff)):.6f}')
    axes[1].grid(True, alpha=0.3)

    plt.tight_layout()

    # Save
    output_file = f"{output_prefix}_comparison_zoom.png"
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"Saved: {output_file}")
    plt.close()


def main():
    if len(sys.argv) < 2:
        print("Usage: python plot_cmma.py <cmma_data.csv>")
        sys.exit(1)

    csv_file = sys.argv[1]

    print(f"Reading data from: {csv_file}")
    df = pd.read_csv(csv_file)

    print(f"Data shape: {df.shape}")
    print(f"Columns: {list(df.columns)}")

    # Generate plots for each indicator
    indicators = [
        ('CMMA_S', 'csv_CMMA_S', 'computed_CMMA_S', 'cmma_s'),
        ('CMMA_M', 'csv_CMMA_M', 'computed_CMMA_M', 'cmma_m'),
        ('CMMA_L', 'csv_CMMA_L', 'computed_CMMA_L', 'cmma_l')
    ]

    for name, csv_col, computed_col, prefix in indicators:
        print(f"\nGenerating plots for {name}...")
        create_comparison_plot(df, name, csv_col, computed_col, prefix)
        create_zoomed_plot(df, name, csv_col, computed_col, prefix, zoom_bars=200)

    print("\nAll plots generated successfully!")
    print("\nGenerated files:")
    for name, _, _, prefix in indicators:
        print(f"  - {prefix}_comparison.png")
        print(f"  - {prefix}_comparison_zoom.png")


if __name__ == '__main__':
    main()
