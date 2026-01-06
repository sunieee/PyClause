#!/usr/bin/env python3
"""
Extract ground truth statistics from combo evaluation log files.
Reads lines starting with [IMPROVED], [DEGRADED], or [UNCHANGED] and outputs to CSV.
"""

import sys
import re
import csv
from pathlib import Path


def extract_gt_stats(log_file, output_csv):
    """
    Extract GT statistics from log file and write to CSV.
    
    Args:
        log_file: Path to input log file
        output_csv: Path to output CSV file
    """
    # Pattern to match lines starting with [status]
    pattern = re.compile(r'^\[(IMPROVED|DEGRADED|UNCHANGED)\](.+)$')
    
    extracted_lines = []
    
    # Read log file and extract matching lines
    with open(log_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            match = pattern.match(line)
            if match:
                status = match.group(1)
                data = match.group(2)
                extracted_lines.append([status] + data.split(','))
    
    if not extracted_lines:
        print(f"No statistics found in {log_file}")
        return
    
    # Write to CSV
    with open(output_csv, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        
        # Write header
        writer.writerow([
            'status',
            'query_entity',
            'relation',
            'gt_entity',
            'num_rules',
            'num_combos',
            'RR_before',
            'RR_after',
            'RR_delta',
            'aggregated_surprisal',
            'added_surprisal'
        ])
        
        # Write data
        writer.writerows(extracted_lines)
    
    print(f"Extracted {len(extracted_lines)} lines from {log_file}")
    print(f"Output written to {output_csv}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python extract_gt_stats.py <log_file> [output_csv]")
        print("\nExample:")
        print("  python extract_gt_stats.py out/dataset/eval-combo-noisymax.log")
        print("  python extract_gt_stats.py out/dataset/eval-combo-noisymax.log gt_stats.csv")
        sys.exit(1)
    
    log_file = sys.argv[1]
    
    # Generate output filename if not provided
    if len(sys.argv) >= 3:
        output_csv = sys.argv[2]
    else:
        log_path = Path(log_file)
        output_csv = log_path.parent / (log_path.stem + '_gt_stats.csv')
    
    # Check if input file exists
    if not Path(log_file).exists():
        print(f"Error: Log file '{log_file}' not found")
        sys.exit(1)
    
    extract_gt_stats(log_file, output_csv)


if __name__ == '__main__':
    main()
