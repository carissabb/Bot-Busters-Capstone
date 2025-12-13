# Human-Bot Behavioral Network Simulation  
**ns-3.45 | C++ | Network Behavior Analysis**

This project simulates human vs. bot communication behavior on a Twitter-like platform using the ns-3.45 network simulator. It replays real posting patterns from a dataset to generate realistic network traffic and logs KPIs that can support bot-detection research.

The simulation outputs:
- `bot_detection_kpis.csv`
- `.pcap` files in `pcap_output/`
- `twitter-bot-detection.xml` (NetAnim trace)
- `flowmon-results.xml` (FlowMonitor export)

---

## Overview

The simulation models two types of users:

- **Humans** — tweet timing from real data, mobility enabled, varied packet sizes, can send via Wi-Fi or 5G  
- **Bots** — tweet timing from real bot accounts, stationary, more uniform packet sizes, Wi-Fi only  

All tweet activity is generated from real timestamps by converting them into inter-arrival times and scaling the full timeline into the simulation duration (default 60s). Tweets that occur within a short real-world interval are grouped into “tweet storms,” where a single tweet event generates multiple packets.

---

## System Requirements

These are the dependencies used for this project:

- **NS3** — 3.45 (not allinone)
- 5G LENA https://cttc-lena.gitlab.io/nr/html/ (additional ns-3 module)
- **Ubuntu** — 24.04
- **WSL** — WSL2 (several memory issues with WSL2, for better performance deployments from an Ubuntu VM is ideal)
- **VSCode**

---

## Input Data & Project Structure
This simulation does not do input validation. You MUST use this file to generate simulations.
**Required input file:**
- `scratch/Twitter_Data.csv`  
- Required columns:  
  `username, account, description, tweet_id, tweet_timestamp, tweet_text, label`  
  where `label = 0` (bot) or `1` (human).

**Project files:**
```bash
scratch/
├── full_twitter_sim.cc
└── Twitter_Data.csv
```

Both files must be placed inside the ns-3 `scratch/` directory for the simulation to compile and run correctly.

---

## How the Simulation Works

### 1. User Selection & Node Creation
- Loads all users from the dataset.  
- Randomly selects up to `maxUsers`.  
- Creates human or bot nodes based on `label`.  
- Humans move (mobility model); bots stay stationary.

### 2. Network Setup
- All users connect to Wi-Fi.  
- Humans additionally get 5G NR devices via the ns-3 5G module.  
- A single server node receives all traffic.

### 3. Behavior Replay & Traffic Generation
- Tweets are sorted by timestamp → real inter-arrival times computed → scaled.  
- Tweet storms generate multiple packets.  
- Packet size = tweet text length + jitter (humans have more variation).  
- Humans choose network:
  - Moving quickly → 5G
  - Otherwise → Wi-Fi
- Bots always send via Wi-Fi.  
- All traffic uses TCP to the server on port 8080.

### 4. KPI Logging & Traces
Each node logs:
- Tweet count, storm tweet count  
- Average inter-arrival time  
- Packets sent/received/lost, loss rate  
- Average delay, jitter, throughput  
- Planned Wi-Fi/5G packet counts  
- Movement distance (humans only)

Additional outputs:
- PCAPs for Wi-Fi and P2P links  
- FlowMonitor XML  
- NetAnim XML  

---

## Running the Simulation (ns-3.45)

### 1️⃣ Build
```bash
./ns3 build
```

### 2️⃣ Run with default (maxUsers = 20)
```bash
./ns3 run "full_twitter_sim"
```

### (Optional) Run with custom number of users (default=20)
```bash
./ns3 run "full_twitter_sim --maxUsers=10"
```

Additionally, users are able to opt out of the pcap tracing or animation generation. These options are DISABLED by default in order to save resources and memory. To enable 
### (Optional) Run with tracing and netanim enabled
```bash
./ns3 run "full_twitter_sim --maxUsers=10 --enableTracing=true --enableNetAnim=true"
```

## Viewing the Results
Using the notebook in this repository, users will be able to create visualizations based on the outputted bot_detection_kpis.csv file from the sim. Below are commands to get started and run the code in the Jupyter Notebook.

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```
Here are a few examples:
![Latency Comparison](outputs/average_delay_bot_vs_human.png)


NetAnim is used to view the twitter-bot-detection.xml:
https://www.nsnam.org/wiki/NetAnim




