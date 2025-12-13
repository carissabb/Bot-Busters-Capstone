import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Set style
plt.rcParams['figure.figsize'] = (20, 12)
plt.rcParams['font.sans-serif'] = ['Arial']

# Read and clean data
try:
    df = pd.read_csv('bot_detection_kpis.csv', on_bad_lines='skip')
except:
    df = pd.read_csv('bot_detection_kpis.csv', error_bad_lines=False, warn_bad_lines=True)

df_clean = df[df['NodeType'].notna()].copy()
print(f"Successfully loaded {len(df_clean)} rows")

# Separate bot and human data
bot_df = df_clean[df_clean['NodeType'] == 'bot']
human_df = df_clean[df_clean['NodeType'] == 'human']

# Color scheme
bot_color = '#FF6B6B'
human_color = '#4ECDC4'

print(f"Analyzing {len(bot_df)} bots and {len(human_df)} humans...")

# Create figure with simple plots
fig, axes = plt.subplots(3, 3, figsize=(20, 12))

# 1. BAR CHART - Average Packet Loss Rate
ax1 = axes[0, 0]
categories = ['Bot', 'Human']
values = [bot_df['PacketLossRate'].mean(), human_df['PacketLossRate'].mean()]
bars = ax1.bar(categories, values, color=[bot_color, human_color], 
               edgecolor='black', linewidth=2, width=0.6)
ax1.set_ylabel('Packet Loss Rate', fontsize=12)
ax1.set_title('Average Packet Loss Rate', fontweight='bold', fontsize=14)
ax1.grid(True, alpha=0.3, axis='y')
# Add value labels on bars
for bar in bars:
    height = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width()/2., height,
            f'{height:.4f}', ha='center', va='bottom', fontsize=11, fontweight='bold')

# 2. BAR CHART - Average Delay (Latency)
ax2 = axes[0, 1]
values = [bot_df['AvgDelay_ms'].mean(), human_df['AvgDelay_ms'].mean()]
bars = ax2.bar(categories, values, color=[bot_color, human_color], 
               edgecolor='black', linewidth=2, width=0.6)
ax2.set_ylabel('Delay (ms)', fontsize=12)
ax2.set_title('Average Delay (Latency)', fontweight='bold', fontsize=14)
ax2.grid(True, alpha=0.3, axis='y')
for bar in bars:
    height = bar.get_height()
    ax2.text(bar.get_x() + bar.get_width()/2., height,
            f'{height:.2f}', ha='center', va='bottom', fontsize=11, fontweight='bold')

# 3. BAR CHART - Average Throughput
ax3 = axes[0, 2]
values = [bot_df['Throughput_bps'].mean()/1000, human_df['Throughput_bps'].mean()/1000]
bars = ax3.bar(categories, values, color=[bot_color, human_color], 
               edgecolor='black', linewidth=2, width=0.6)
ax3.set_ylabel('Throughput (Kbps)', fontsize=12)
ax3.set_title('Average Throughput', fontweight='bold', fontsize=14)
ax3.grid(True, alpha=0.3, axis='y')
for bar in bars:
    height = bar.get_height()
    ax3.text(bar.get_x() + bar.get_width()/2., height,
            f'{height:.1f}', ha='center', va='bottom', fontsize=11, fontweight='bold')

# 4. BAR CHART - Average Jitter
ax4 = axes[1, 0]
values = [bot_df['AvgJitter_ms'].mean(), human_df['AvgJitter_ms'].mean()]
bars = ax4.bar(categories, values, color=[bot_color, human_color], 
               edgecolor='black', linewidth=2, width=0.6)
ax4.set_ylabel('Jitter (ms)', fontsize=12)
ax4.set_title('Average Jitter', fontweight='bold', fontsize=14)
ax4.grid(True, alpha=0.3, axis='y')
for bar in bars:
    height = bar.get_height()
    ax4.text(bar.get_x() + bar.get_width()/2., height,
            f'{height:.3f}', ha='center', va='bottom', fontsize=11, fontweight='bold')

# 5. GROUPED BAR CHART - Transmitted vs Received Packets
ax5 = axes[1, 1]
x = np.arange(2)
width = 0.35
tx_values = [bot_df['TxPackets'].mean(), human_df['TxPackets'].mean()]
rx_values = [bot_df['RxPackets'].mean(), human_df['RxPackets'].mean()]
bars1 = ax5.bar(x - width/2, tx_values, width, label='Transmitted', 
                color='#FF9999', edgecolor='black', linewidth=1.5)
bars2 = ax5.bar(x + width/2, rx_values, width, label='Received', 
                color='#99CCFF', edgecolor='black', linewidth=1.5)
ax5.set_ylabel('Packet Count', fontsize=12)
ax5.set_title('Transmitted vs Received Packets', fontweight='bold', fontsize=14)
ax5.set_xticks(x)
ax5.set_xticklabels(categories)
ax5.legend(fontsize=11)
ax5.grid(True, alpha=0.3, axis='y')

# 6. BAR CHART - Total Distance (Mobility)
ax6 = axes[1, 2]
values = [bot_df['TotalDistance_m'].mean(), human_df['TotalDistance_m'].mean()]
bars = ax6.bar(categories, values, color=[bot_color, human_color], 
               edgecolor='black', linewidth=2, width=0.6)
ax6.set_ylabel('Distance (meters)', fontsize=12)
ax6.set_title('Average Total Distance Traveled', fontweight='bold', fontsize=14)
ax6.grid(True, alpha=0.3, axis='y')
for bar in bars:
    height = bar.get_height()
    ax6.text(bar.get_x() + bar.get_width()/2., height,
            f'{height:.1f}', ha='center', va='bottom', fontsize=11, fontweight='bold')

# 7. LINE PLOT - Packet Loss Rate Distribution
ax7 = axes[2, 0]
bot_loss = bot_df['PacketLossRate'].dropna().sort_values().reset_index(drop=True)
human_loss = human_df['PacketLossRate'].dropna().sort_values().reset_index(drop=True)
ax7.plot(bot_loss.values, linewidth=2, label='Bot', color=bot_color, marker='o', 
         markersize=4, markevery=max(1, len(bot_loss)//20))
ax7.plot(human_loss.values, linewidth=2, label='Human', color=human_color, marker='s', 
         markersize=4, markevery=max(1, len(human_loss)//20))
ax7.set_xlabel('Node Index (sorted)', fontsize=12)
ax7.set_ylabel('Packet Loss Rate', fontsize=12)
ax7.set_title('Packet Loss Rate Distribution', fontweight='bold', fontsize=14)
ax7.legend(fontsize=11)
ax7.grid(True, alpha=0.3)

# 8. LINE PLOT - Throughput Distribution
ax8 = axes[2, 1]
bot_tp = (bot_df['Throughput_bps'].dropna()/1000).sort_values().reset_index(drop=True)
human_tp = (human_df['Throughput_bps'].dropna()/1000).sort_values().reset_index(drop=True)
ax8.plot(bot_tp.values, linewidth=2, label='Bot', color=bot_color, marker='o', 
         markersize=4, markevery=max(1, len(bot_tp)//20))
ax8.plot(human_tp.values, linewidth=2, label='Human', color=human_color, marker='s', 
         markersize=4, markevery=max(1, len(human_tp)//20))
ax8.set_xlabel('Node Index (sorted)', fontsize=12)
ax8.set_ylabel('Throughput (Kbps)', fontsize=12)
ax8.set_title('Throughput Distribution', fontweight='bold', fontsize=14)
ax8.legend(fontsize=11)
ax8.grid(True, alpha=0.3)

# 9. BAR CHART - Network Type Usage
ax9 = axes[2, 2]
network_labels = ['Bot\n(WiFi)', 'Human\n(WiFi)', 'Human\n(5G)']
network_values = [
    bot_df['PlannedWifiPkts'].sum(),
    human_df['PlannedWifiPkts'].sum(),
    human_df['Planned5GPkts'].sum()
]
colors = [bot_color, human_color, '#95E1D3']
bars = ax9.bar(network_labels, network_values, color=colors, 
               edgecolor='black', linewidth=2, width=0.6)
ax9.set_ylabel('Total Packets', fontsize=12)
ax9.set_title('Network Type Usage', fontweight='bold', fontsize=14)
ax9.grid(True, alpha=0.3, axis='y')
# Add value labels on bars
for bar in bars:
    height = bar.get_height()
    ax9.text(bar.get_x() + bar.get_width()/2., height,
            f'{int(height):,}', ha='center', va='bottom', fontsize=10, fontweight='bold')

plt.tight_layout()
plt.savefig('simple_bot_detection_analysis.png', dpi=300, bbox_inches='tight')
print("\nVisualization saved as 'simple_bot_detection_analysis.png'")
plt.show()

# Print summary statistics
print("\n" + "="*80)
print("SUMMARY STATISTICS")
print("="*80)
print(f"\nPacket Loss Rate:")
print(f"  Bot:   {bot_df['PacketLossRate'].mean():.4f}")
print(f"  Human: {human_df['PacketLossRate'].mean():.4f}")
print(f"  Ratio: {bot_df['PacketLossRate'].mean() / human_df['PacketLossRate'].mean():.2f}x")

print(f"\nThroughput (Kbps):")
print(f"  Bot:   {bot_df['Throughput_bps'].mean()/1000:.1f}")
print(f"  Human: {human_df['Throughput_bps'].mean()/1000:.1f}")
print(f"  Ratio: {human_df['Throughput_bps'].mean() / bot_df['Throughput_bps'].mean():.2f}x")

print(f"\nLatency (ms):")
print(f"  Bot:   {bot_df['AvgDelay_ms'].mean():.2f}")
print(f"  Human: {human_df['AvgDelay_ms'].mean():.2f}")
print(f"  Ratio: {human_df['AvgDelay_ms'].mean() / bot_df['AvgDelay_ms'].mean():.2f}x")

print(f"\nJitter (ms):")
print(f"  Bot:   {bot_df['AvgJitter_ms'].mean():.4f}")
print(f"  Human: {human_df['AvgJitter_ms'].mean():.4f}")
print(f"  Ratio: {human_df['AvgJitter_ms'].mean() / bot_df['AvgJitter_ms'].mean():.1f}x")

print(f"\nMobility (meters):")
print(f"  Bot:   {bot_df['TotalDistance_m'].mean():.1f}")
print(f"  Human: {human_df['TotalDistance_m'].mean():.1f}")

print("="*80)