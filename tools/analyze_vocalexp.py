import matplotlib.pyplot as plt
import numpy as np
import sys
import os
import csv

def main(csv_path, output_image):
    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found.")
        return

    indices = []
    inputs = []
    f0s = []
    ratios = []

    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            indices.append(int(row['sample_index']))
            inputs.append(float(row['input']))
            f0s.append(float(row['f0']))
            ratios.append(float(row['ratio']))
    
    indices = np.array(indices)
    inputs = np.array(inputs)
    f0s = np.array(f0s)
    ratios = np.array(ratios)

    # Calculate time in seconds (assuming 48kHz)
    time = indices / 48000.0
    
    fig, axes = plt.subplots(3, 1, figsize=(15, 12), sharex=True)
    
    # 1. Waveform
    axes[0].plot(time, inputs, color='gray', alpha=0.5, label='Input Waveform')
    axes[0].set_ylabel('Amplitude')
    axes[0].set_title('Internal DSP State Analysis')
    axes[0].legend(loc='upper right')
    axes[0].grid(True, alpha=0.3)
    
    # 2. Estimated Pitch
    axes[1].plot(time, f0s, color='blue', label='Estimated F0 (Hz)')
    # Filter out 0 (unvoiced) for scatter
    voiced_mask = f0s > 0
    if np.any(voiced_mask):
        axes[1].scatter(time[voiced_mask], f0s[voiced_mask], color='blue', s=1)
    
    axes[1].set_ylabel('Frequency (Hz)')
    axes[1].legend(loc='upper right')
    axes[1].grid(True, alpha=0.3)
    
    # 3. Pitch Shift Ratio
    axes[2].plot(time, ratios, color='red', label='Pitch Shift Ratio')
    axes[2].set_ylabel('Ratio')
    axes[2].set_xlabel('Time (s)')
    axes[2].legend(loc='upper right')
    axes[2].grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(output_image)
    print(f"Plot saved to {output_image}")

if __name__ == "__main__":
    path = "vocalexp_debug.csv"
    if len(sys.argv) > 1:
        path = sys.argv[1]
    
    out = "media/out/analysis.png"
    if len(sys.argv) > 2:
        out = sys.argv[2]
        
    main(path, out)
