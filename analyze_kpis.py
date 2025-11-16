import subprocess, csv, glob, os
from collections import Counter
import math

pcap_files = glob.glob("pcap_output/*.pcap")
output = open("pcap_kpis_detailed.csv", "w", newline='')
writer = csv.writer(output)

# Write header with all KPIs
writer.writerow([
    "file", "node_type", "network_type",
    # Basic metrics
    "total_packets", "avg_pkt_size", "std_pkt_size", "min_pkt_size", "max_pkt_size",
    # Timing metrics (SIP REGISTER interval equivalent)
    "avg_inter_pkt_time", "std_inter_pkt_time", "min_inter_pkt_time", "max_inter_pkt_time",
    "timing_regularity", "timing_cv",
    # Session metrics (call duration profile)
    "session_duration", "packets_per_sec", "burst_factor",
    # Entropy metrics (session length entropy)
    "pkt_size_entropy", "pkt_size_uniformity",
    "src_ip_entropy", "dst_ip_entropy", "src_port_entropy", "dst_port_entropy",
    # Protocol diversity (User-Agent diversity equivalent)
    "tcp_packets", "udp_packets", "icmp_packets", "protocol_diversity",
    "tcp_ratio", "udp_ratio",
    # TCP behavior (keypad tone timing, voice packet variability)
    "tcp_syn_count", "tcp_fin_count", "tcp_rst_count",
    "tcp_retrans", "avg_tcp_window", "std_tcp_window", "tcp_flag_diversity",
    # Network layer (IMEI uniqueness, handover frequency equivalent)
    "avg_ttl", "std_ttl", "min_ttl", "max_ttl", "unique_ttl_values",
    "unique_src_ips", "unique_dst_ips", "unique_src_ports", "unique_dst_ports",
    # QoS/DSCP (IP source type, DSCP/QoS markings)
    "unique_tos_values", "avg_dscp", "unique_dscp", "non_zero_dscp", "dscp_marking_ratio",
    # DNS (DNS server used)
    "dns_queries", "dns_responses", "dns_ratio"
])

def calculate_entropy(items):
    """Calculate Shannon entropy"""
    if not items:
        return 0
    counts = Counter(items)
    total = len(items)
    return -sum((c/total) * math.log2(c/total) for c in counts.values())

for pcap in sorted(pcap_files):
    print(f"Analyzing {os.path.basename(pcap)}...")
    
    # Determine node type and network from filename
    filename = os.path.basename(pcap)
    
    # Node type and network type classification
    if "bot" in filename:
        node_type = "bot"
        network_type = "wifi"  # Bots only use WiFi
    elif "human" in filename:
        node_type = "human"
        network_type = "wifi"  # Human WiFi traffic
    elif "p2p" in filename:
        # P2P files contain 5G traffic for humans (GTP tunneled)
        node_type = "human"
        network_type = "5g"
    else:
        node_type = "unknown"
        network_type = "unknown"
    
    # 1. BASIC METRICS: Packet sizes
    sizes = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "frame.len"]
    ).decode().splitlines()
    sizes = [int(s) for s in sizes if s.isdigit()]
    
    if not sizes:
        print(f"  Skipping {filename} - no packets")
        continue
    
    total_packets = len(sizes)
    avg_size = sum(sizes) / len(sizes)
    std_size = (sum((x - avg_size)**2 for x in sizes) / len(sizes))**0.5 if len(sizes) > 1 else 0
    min_size = min(sizes)
    max_size = max(sizes)
    
    # 2. TIMING METRICS: Inter-packet times (SIP REGISTER interval equivalent)
    times = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "frame.time_epoch"]
    ).decode().splitlines()
    times = [float(t) for t in times if t]
    
    if len(times) > 1:
        ipd = [t2 - t1 for t1, t2 in zip(times, times[1:])]
        avg_ipd = sum(ipd) / len(ipd)
        std_ipd = (sum((x - avg_ipd)**2 for x in ipd) / len(ipd))**0.5 if len(ipd) > 1 else 0
        min_ipd = min(ipd)
        max_ipd = max(ipd)
        cv = std_ipd / avg_ipd if avg_ipd > 0 else 0  # Coefficient of variation
        regularity = 1 / (1 + cv) if cv > 0 else 1  # Bots are more regular (lower CV)
    else:
        avg_ipd = std_ipd = min_ipd = max_ipd = cv = regularity = 0
    
    # 3. SESSION METRICS: Duration and burst patterns (call duration profile)
    session_duration = max(times) - min(times) if len(times) > 1 else 0
    pps = total_packets / session_duration if session_duration > 0 else 0
    burst_factor = (std_ipd**2 / avg_ipd) if avg_ipd > 0 else 0
    
    # 4. ENTROPY METRICS: Packet size entropy (session length entropy)
    pkt_size_entropy = calculate_entropy(sizes)
    size_range = max_size - min_size if max_size > min_size else 1
    pkt_size_uniformity = 1 - (std_size / size_range) if size_range > 0 else 0
    
    # 5. IP ENTROPY: Source/destination diversity
    src_ips = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "ip.src"]
    ).decode().splitlines()
    src_ips = [ip for ip in src_ips if ip]
    
    dst_ips = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "ip.dst"]
    ).decode().splitlines()
    dst_ips = [ip for ip in dst_ips if ip]
    
    src_ip_entropy = calculate_entropy(src_ips)
    dst_ip_entropy = calculate_entropy(dst_ips)
    unique_src_ips = len(set(src_ips))
    unique_dst_ips = len(set(dst_ips))
    
    # 6. PORT ENTROPY: TCP ports
    src_ports = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.srcport", "-Y", "tcp"]
    ).decode().splitlines()
    src_ports = [p for p in src_ports if p]
    
    dst_ports = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.dstport", "-Y", "tcp"]
    ).decode().splitlines()
    dst_ports = [p for p in dst_ports if p]
    
    src_port_entropy = calculate_entropy(src_ports)
    dst_port_entropy = calculate_entropy(dst_ports)
    unique_src_ports = len(set(src_ports))
    unique_dst_ports = len(set(dst_ports))
    
    # 7. PROTOCOL DIVERSITY: TCP/UDP/ICMP (User-Agent diversity equivalent)
    protocols = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "ip.proto"]
    ).decode().splitlines()
    protocols = [int(p) for p in protocols if p.isdigit()]
    
    protocol_counts = Counter(protocols)
    tcp_packets = protocol_counts.get(6, 0)
    udp_packets = protocol_counts.get(17, 0)
    icmp_packets = protocol_counts.get(1, 0)
    protocol_diversity = calculate_entropy(protocols)
    tcp_ratio = tcp_packets / total_packets if total_packets > 0 else 0
    udp_ratio = udp_packets / total_packets if total_packets > 0 else 0
    
    # 8. TCP BEHAVIOR: Flags, retransmissions (keypad tone timing)
    tcp_syn = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.flags.syn", "-Y", "tcp.flags.syn==1"]
    ).decode().splitlines()
    tcp_syn_count = len([s for s in tcp_syn if s])
    
    tcp_fin = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.flags.fin", "-Y", "tcp.flags.fin==1"]
    ).decode().splitlines()
    tcp_fin_count = len([f for f in tcp_fin if f])
    
    tcp_rst = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.flags.reset", "-Y", "tcp.flags.reset==1"]
    ).decode().splitlines()
    tcp_rst_count = len([r for r in tcp_rst if r])
    
    tcp_seq = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.seq", "-Y", "tcp"]
    ).decode().splitlines()
    tcp_seq = [s for s in tcp_seq if s]
    tcp_retrans = len(tcp_seq) - len(set(tcp_seq)) if tcp_seq else 0
    
    tcp_windows = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.window_size", "-Y", "tcp"]
    ).decode().splitlines()
    tcp_windows = [int(w) for w in tcp_windows if w.isdigit()]
    avg_tcp_window = sum(tcp_windows) / len(tcp_windows) if tcp_windows else 0
    std_tcp_window = (sum((x - avg_tcp_window)**2 for x in tcp_windows) / len(tcp_windows))**0.5 if len(tcp_windows) > 1 else 0
    
    tcp_flags = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "tcp.flags", "-Y", "tcp"]
    ).decode().splitlines()
    tcp_flag_diversity = len(set(f for f in tcp_flags if f))
    
    # 9. NETWORK LAYER: TTL values (handover frequency, IMEI uniqueness equivalent)
    ttl_values = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "ip.ttl"]
    ).decode().splitlines()
    ttl_values = [int(t) for t in ttl_values if t.isdigit()]
    
    if ttl_values:
        avg_ttl = sum(ttl_values) / len(ttl_values)
        std_ttl = (sum((x - avg_ttl)**2 for x in ttl_values) / len(ttl_values))**0.5 if len(ttl_values) > 1 else 0
        min_ttl = min(ttl_values)
        max_ttl = max(ttl_values)
        unique_ttl = len(set(ttl_values))
    else:
        avg_ttl = std_ttl = min_ttl = max_ttl = unique_ttl = 0
    
    # 10. QoS/DSCP: TOS field markings (DSCP/QoS markings)
    tos_values = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "ip.dsfield"]
    ).decode().splitlines()
    tos_values = [t for t in tos_values if t]
    unique_tos = len(set(tos_values))
    
    dscp_values = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "ip.dsfield.dscp"]
    ).decode().splitlines()
    
    # Parse DSCP values (handle comma-separated values and various formats)
    dscp_parsed = []
    for d in dscp_values:
        if d:
            # Split by comma and take first value
            d = d.split(',')[0].strip()
            try:
                if 'x' in d:
                    dscp_parsed.append(int(d, 16))
                else:
                    dscp_parsed.append(int(d))
            except ValueError:
                pass  # Skip invalid values
    
    if dscp_parsed:
        avg_dscp = sum(dscp_parsed) / len(dscp_parsed)
        unique_dscp = len(set(dscp_parsed))
        non_zero_dscp = sum(1 for d in dscp_parsed if d != 0)
        dscp_marking_ratio = non_zero_dscp / len(dscp_parsed)
    else:
        avg_dscp = unique_dscp = non_zero_dscp = dscp_marking_ratio = 0
    
    # 11. DNS: DNS server usage (DNS server used)
    dns_queries = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "dns.qry.name", "-Y", "dns.flags.response==0"]
    ).decode().splitlines()
    dns_queries_count = len([q for q in dns_queries if q])
    
    dns_responses = subprocess.check_output(
        ["tshark", "-r", pcap, "-T", "fields", "-e", "dns.qry.name", "-Y", "dns.flags.response==1"]
    ).decode().splitlines()
    dns_responses_count = len([r for r in dns_responses if r])
    
    dns_ratio = dns_queries_count / total_packets if total_packets > 0 else 0
    
    # Write all KPIs to CSV
    writer.writerow([
        filename, node_type, network_type,
        # Basic
        total_packets, avg_size, std_size, min_size, max_size,
        # Timing
        avg_ipd, std_ipd, min_ipd, max_ipd, regularity, cv,
        # Session
        session_duration, pps, burst_factor,
        # Entropy
        pkt_size_entropy, pkt_size_uniformity,
        src_ip_entropy, dst_ip_entropy, src_port_entropy, dst_port_entropy,
        # Protocol
        tcp_packets, udp_packets, icmp_packets, protocol_diversity, tcp_ratio, udp_ratio,
        # TCP
        tcp_syn_count, tcp_fin_count, tcp_rst_count,
        tcp_retrans, avg_tcp_window, std_tcp_window, tcp_flag_diversity,
        # Network
        avg_ttl, std_ttl, min_ttl, max_ttl, unique_ttl,
        unique_src_ips, unique_dst_ips, unique_src_ports, unique_dst_ports,
        # QoS
        unique_tos, avg_dscp, unique_dscp, non_zero_dscp, dscp_marking_ratio,
        # DNS
        dns_queries_count, dns_responses_count, dns_ratio
    ])

output.close()
print(f"\n✓ Analysis complete! Results saved to pcap_kpis_detailed.csv")
print(f"  Analyzed {len(pcap_files)} PCAP files")
