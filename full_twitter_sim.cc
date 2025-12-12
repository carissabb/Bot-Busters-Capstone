#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/nr-module.h"
#include "ns3/wifi-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <sys/stat.h>
#include <iomanip>
#include <set>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("TwitterNetworkSimulation");

// Headers from twitter_data.csv
struct TwitterData { 
    std::string username;
    std::string account;
    std::string description;
    std::string tweet_id;
    std::string tweet_timestamp; 
    std::string tweet_text;
    int label; // 0 = bot, 1 = human
};

// KPI data structure for each node
struct NodeKPI {
    uint32_t nodeId;
    std::string username;
    std::string nodeType; // human or bot
    std::string networkType; // 5g, wifi, or both (for humans)
    
    // Traffic patterns
    uint32_t totalPackets;
    uint32_t txPackets;
    uint32_t rxPackets;
    uint32_t lostPackets;
    double packetLossRate;
    uint64_t totalBytes;

    // Behavioral features
    uint32_t tweetCount;          
    uint32_t stormTweetCount;     
    double   avgIatReal;         
    double   stdIatReal;          
    uint32_t plannedWifiPackets; 
    uint32_t planned5gPackets;
    
    // Network characteristics
    double avgDelay;
    double avgJitter;
    double throughput;
    
    // Mobility
    double totalDistance;
    Vector lastPosition;
    bool hasLastPos = false;
    
    NodeKPI()
        : nodeId(0),
          totalPackets(0), txPackets(0), rxPackets(0),
          lostPackets(0), packetLossRate(0), totalBytes(0),
          tweetCount(0), stormTweetCount(0),
          avgIatReal(0.0), stdIatReal(0.0),
          plannedWifiPackets(0), planned5gPackets(0),
          avgDelay(0), avgJitter(0), throughput(0), totalDistance(0) {
        lastPosition = Vector(0, 0, 0);
    }
};

// Global KPI tracking
std::map<uint32_t, NodeKPI> g_nodeKPIs;
std::map<Ipv4Address, uint32_t> g_ipToNodeId;

// Helper to remove surrounding quotes for parsing input csv data
std::string StripQuotes(const std::string &s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// Function to parse a single CSV line into tokens 
std::vector<std::string> ParseCSVLine(const std::string &line) {
    std::vector<std::string> result;
    std::string cur;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            result.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    result.push_back(cur);
    return result;
}

// Function to read a logical line from CSV, handling multi-line fields
std::string ReadLogicalLine(std::ifstream &file) {
    std::string line, logicalLine;
    int quoteCount = 0;
    while (std::getline(file, line)) {
        logicalLine += line;
        // Count quotes in the line
        quoteCount += std::count(line.begin(), line.end(), '"');
        // If quotes are balanced, we have a complete logical line
        if (quoteCount % 2 == 0) break;
        // Otherwise, add a newline and continue reading
        logicalLine += "\n";
    }
    return logicalLine;
}

// Function to load tweets from CSV
std::vector<TwitterData> LoadTwitterDataFromCSV(const std::string &filename) {
    std::vector<TwitterData> tweetData; // Stores all rows from CSV
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return tweetData;
    }

    std::string line;
    bool isHeader = true;
    while (!file.eof()) {
        line = ReadLogicalLine(file);
        if (line.empty()) continue;
        if (isHeader) { isHeader = false; continue; }

        // Parse CSV line into fields
        auto fields = ParseCSVLine(line);
        if (fields.size() < 7) continue;

        TwitterData row;
        row.username = StripQuotes(fields[0]);
        row.account = StripQuotes(fields[1]);
        row.description = StripQuotes(fields[2]);
        row.tweet_id = StripQuotes(fields[3]);
        row.tweet_timestamp = StripQuotes(fields[4]);
        row.tweet_text = StripQuotes(fields[5]);

        try {
            row.label = std::stoi(fields[6]);
        } catch (...) {
            row.label = 0;
        }
        tweetData.push_back(row); // Append row to vector
    }
    file.close();
    return tweetData;
}

// Helper function to convert tweet timestamp string to seconds
double ConvertTimestampToSeconds(const std::string &timestamp) {
    std::tm timeInfo = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&timeInfo, "%Y-%m-%d %H:%M:%S"); // input format: 11/27/2016  6:15:03 AM
    if (ss.fail()) {
        return 0.0;
    }
    return static_cast<double>(std::mktime(&timeInfo));
}

// Function to send a packet
void SendPacket(Ptr<Socket> socket, Ipv4Address serverAddr, uint32_t serverPort, uint32_t pktSize) {
    Ptr<Packet> packet = Create<Packet>(pktSize);
    socket->Send(packet);
}

// Function to send Twitter packet based on node type and mobility
void SendTwitterPacket(Ptr<Node> node,
                       Ptr<Socket> wifiSocket,
                       Ptr<Socket> socket5G,
                       int label,                    
                       uint32_t pktSize,
                       Ipv4Address serverIp,
                       uint16_t serverPort)          
{
    if (!wifiSocket) return;

    uint32_t nodeId = node->GetId();

    // Retrieve KPI entry
    auto it = g_nodeKPIs.find(nodeId);
    if (it == g_nodeKPIs.end()) {
        // If no KPI object, send on WiFi
        SendPacket(wifiSocket, serverIp, serverPort, pktSize);
        return;
    }

    NodeKPI &kpi = it->second;

    //  Decide which socket to use
    Ptr<Socket> sockToUse = wifiSocket;
    
    if (label == 0) { // Bots NEVER use 5G
        socket5G = nullptr;
    }

    bool use5G = false;

    // Choose wifi or 5g for humans based on mobility
    if (label == 1 && socket5G)  // humans only
    {
        Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
        if (mob)
        {
            Vector vel = mob->GetVelocity();
            double speed = std::sqrt(vel.x*vel.x + vel.y*vel.y + vel.z*vel.z);

            // movement-based switching
            if (speed > 1.0) {
                use5G = true;
            }
        }
    }

    // Select socket and update KPI counters
    if (use5G) {
        sockToUse = socket5G;
    } else {
        sockToUse = wifiSocket;
    }

    // update kpi counters based on socket used
    if (sockToUse == socket5G) {
        kpi.planned5gPackets++;
    } else {
        kpi.plannedWifiPackets++;
    }
    SendPacket(sockToUse, serverIp, serverPort, pktSize);
}


// Callback function to track mobility changes
void CourseChangeCallback(uint32_t nodeId, Ptr<const MobilityModel> model) {
    auto it = g_nodeKPIs.find(nodeId);
    if (it == g_nodeKPIs.end()) {
        return;
    }

    NodeKPI& kpi = it->second;
    Vector currentPos = model->GetPosition();
    
    // Calculate distance traveled since last position update
    if (kpi.lastPosition != Vector(0, 0, 0)) {
        double dx = currentPos.x - kpi.lastPosition.x;
        double dy = currentPos.y - kpi.lastPosition.y;
        double dz = currentPos.z - kpi.lastPosition.z;
        double distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        kpi.totalDistance += distance;
    }
    
    kpi.lastPosition = currentPos;
    kpi.hasLastPos = true;
}

// Output elasped time in seconds
void PrintElapsedTime(std::chrono::time_point<std::chrono::high_resolution_clock> start) {
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = now - start;
    std::cout << "\rElapsed time: " << elapsed.count() << " seconds" << std::flush;
    Simulator::Schedule(Seconds(1.0), &PrintElapsedTime, start);
}

// Create directory if it doesn't already exist
void CreateDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        mkdir(path.c_str(), 0755);
    }
}

int main(int argc, char *argv[]) {
    PacketMetadata::Enable();
    
    std::string pcapDir = "pcap_output";
    std::string csvFile = "bot_detection_kpis.csv";
    std::string animFile = "twitter-bot-detection.xml";
    uint32_t maxUsers = 20;
    std::string inputCsv = "scratch/Twitter_Data.csv";
    bool enableTracing = false;
    bool enableNetAnim = false;  // Disabled by default - uses lots of memory
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("pcapDir", "Directory for PCAP files", pcapDir);
    cmd.AddValue("csvFile", "Output CSV file for KPIs", csvFile);
    cmd.AddValue("input", "Input CSV file for tweets (overrides default)", inputCsv);
    cmd.AddValue("animFile", "Output NetAnim XML file", animFile);
    cmd.AddValue("maxUsers", "Maximum number of users to simulate", maxUsers);
    cmd.AddValue("enableTracing", "Enable PCAP and NR tracing (slower)", enableTracing);
    cmd.AddValue("enableNetAnim", "Enable NetAnim output (uses memory)", enableNetAnim);
    cmd.Parse(argc, argv);

    CreateDirectory(pcapDir);

    LogComponentEnable("TwitterNetworkSimulation", LOG_LEVEL_INFO);
    NS_LOG_INFO("Starting Twitter Bot Detection Simulation...");
    NS_LOG_INFO("Max users limit: " << maxUsers);

    // Load tweet data
    // Default input CSV (can be overridden via CLI flag --input)
    // `inputCsv` may be overridden by the CommandLine option --input
    NS_LOG_INFO("Using input CSV: " << inputCsv);
    std::vector<TwitterData> tweets = LoadTwitterDataFromCSV(inputCsv);
    if (tweets.empty()) {
        NS_LOG_ERROR("Failed to load tweet data");
        return 1;
    }

    // Extract unique usernames
    std::set<std::string> uniqueUsernames;
    for (const auto &tweet : tweets) {
        uniqueUsernames.insert(tweet.username);
    }
    std::vector<std::string> usernames(uniqueUsernames.begin(), uniqueUsernames.end());
    
    // Limit number of users if specified
    if (usernames.size() > maxUsers) {
        NS_LOG_INFO("Limiting from " << usernames.size() << " to " << maxUsers << " users");
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(usernames.begin(), usernames.end(), gen);
        usernames.resize(maxUsers);
        
        // Filter tweets to only include selected users
        std::set<std::string> selectedUsers(usernames.begin(), usernames.end());
        std::vector<TwitterData> filteredTweets;
        for (const auto& tweet : tweets) {
            if (selectedUsers.find(tweet.username) != selectedUsers.end()) {
                filteredTweets.push_back(tweet);
            }
        }
        tweets = filteredTweets;
    }
    
    NS_LOG_INFO("Loaded " << tweets.size() << " tweets from " << usernames.size() << " unique users");



    std::random_device rd;
    std::mt19937 gen(rd());

    // Create helpers
    Ptr<NrHelper> nrHelper = CreateObject<NrHelper>();
    Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper>();
    nrHelper->SetEpcHelper(epcHelper);
    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // Node Creation
    NodeContainer server, humanGnb, humanNodes, botNodes, wifiGw;
    server.Create(1);
    humanGnb.Create(1);
    wifiGw.Create(1);

    // Separate users by label
    std::vector<std::pair<std::string, int>> userMapping;
    for (const auto &username : usernames) {
        int label = 0;
        for (const auto &tweet : tweets) {
            if (tweet.username == username) {
                label = tweet.label;
                break;
            }
        }
        userMapping.push_back({username, label});
        
        if (label == 0) {
            botNodes.Create(1);
        } else {
            humanNodes.Create(1);
        }
    }

    NS_LOG_INFO("Created " << humanNodes.GetN() << " human nodes (5G+WiFi) and " 
                << botNodes.GetN() << " bot nodes (WiFi only)");

    // Internet Stack Installation
    InternetStackHelper stack;
    stack.Install(server);
    stack.Install(wifiGw);
    stack.Install(botNodes);
    stack.Install(humanNodes);
    stack.Install(humanGnb);

    // WiFi Setup
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);  // Use 802.11n for better range
    
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    // Increase transmit power for better coverage
    phy.Set("TxPowerStart", DoubleValue(20.0));  // dBm
    phy.Set("TxPowerEnd", DoubleValue(20.0));
    
    WifiMacHelper mac;
    Ssid ssid = Ssid("bot-network");

    // Calculate grid dimensions based on number of bots
    // Keep bots within ~50m of WiFi AP for reliable coverage
    uint32_t numBots = botNodes.GetN();
    uint32_t gridWidth = static_cast<uint32_t>(std::ceil(std::sqrt(numBots)));
    double botSpacing = std::min(10.0, 50.0 / std::max(1u, gridWidth));  // Scale spacing to fit in ~50m radius
    
    // Bots - stationary, positioned in grid around WiFi AP
    MobilityHelper botMobility;
    double gridSize = gridWidth * botSpacing;
    double gridStartX = 50.0 - gridSize / 2.0;  // Center grid at (50, 50)
    double gridStartY = 50.0 - gridSize / 2.0;
    
    botMobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                    "MinX", DoubleValue(gridStartX),
                                    "MinY", DoubleValue(gridStartY),
                                    "DeltaX", DoubleValue(botSpacing),
                                    "DeltaY", DoubleValue(botSpacing),
                                    "GridWidth", UintegerValue(gridWidth),
                                    "LayoutType", StringValue("RowFirst"));
    botMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    botMobility.Install(botNodes);
    
    NS_LOG_INFO("Bot grid: " << gridWidth << "x" << gridWidth << " with " << botSpacing 
                << "m spacing, centered at (50,50)");

    // Mobility for WiFi gateway - positioned at center of bot grid
    MobilityHelper wifiMobility;
    Ptr<ListPositionAllocator> wifiGwPos = CreateObject<ListPositionAllocator>();
    wifiGwPos->Add(Vector(50.0, 50.0, 5.0));  // Center of bot grid
    wifiMobility.SetPositionAllocator(wifiGwPos);
    wifiMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    wifiMobility.Install(wifiGw);

    // Install WiFi devices
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer botApDevice = wifi.Install(phy, mac, wifiGw);
    
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer botDevices = wifi.Install(phy, mac, botNodes);
    NetDeviceContainer humanWifiDevices = wifi.Install(phy, mac, humanNodes);

    // WiFi IP addresses
    Ipv4AddressHelper wifiAddress;
    wifiAddress.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer botApInterfaces = wifiAddress.Assign(botApDevice);
    Ipv4InterfaceContainer botInterfaces = wifiAddress.Assign(botDevices);
    Ipv4InterfaceContainer humanWifiInterfaces = wifiAddress.Assign(humanWifiDevices);

    // 5G Setup
    MobilityHelper humanMobility;
    
    // gNB position - central in grid
    Ptr<ListPositionAllocator> gnbPos = CreateObject<ListPositionAllocator>();
    gnbPos->Add(Vector(100.0, 100.0, 30.0));
    humanMobility.SetPositionAllocator(gnbPos);
    humanMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    humanMobility.Install(humanGnb);

    // Humans - moving aroudnd grid
    humanMobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
                                      "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=200]"),
                                      "Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=200]"),
                                      "Z", StringValue("ns3::ConstantRandomVariable[Constant=1.5]"));
    humanMobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                "Mode", StringValue("Time"),
                                "Time", StringValue("2s"),
                                "Speed", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=1.5]"),
                                "Bounds", RectangleValue(Rectangle(0, 200, 0, 200)));

    humanMobility.Install(humanNodes);

    // Create NR bandwidth parts
    double centralFrequency = 3.5e9;
    double bandwidth = 40e6;
    CcBwpCreator::SimpleOperationBandConf bandConf(centralFrequency, bandwidth, 1);
    auto [operationBand, allBwps] = nrHelper->CreateBandwidthParts({bandConf}, "UMi", "Default", "ThreeGpp");

    // Install NR devices
    NetDeviceContainer humanGnbDevice = nrHelper->InstallGnbDevice(humanGnb, allBwps);
    NetDeviceContainer humanUeDevices = nrHelper->InstallUeDevice(humanNodes, allBwps);

    // Assign IPs via EPC
    Ipv4InterfaceContainer humanInterfaces = epcHelper->AssignUeIpv4Address(humanUeDevices);

    // Attach UEs to gNB
    nrHelper->AttachToClosestGnb(humanUeDevices, humanGnbDevice);

    // Activate EPS bearers
    NrEpsBearer bearer(NrEpsBearer::NGBR_VIDEO_TCP_DEFAULT);
    for (uint32_t i = 0; i < humanUeDevices.GetN(); ++i) {
        Ptr<NrUeNetDevice> ue = humanUeDevices.Get(i)->GetObject<NrUeNetDevice>();
        uint64_t imsi = ue->GetImsi();
        epcHelper->ActivateEpsBearer(humanUeDevices.Get(i), imsi, NrEpcTft::Default(), bearer);
    }

    // P2P Links
    PointToPointHelper p2pWifiPgw;
    p2pWifiPgw.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    p2pWifiPgw.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer p2pWifiPgwDevices = p2pWifiPgw.Install(wifiGw.Get(0), pgw);

    Ipv4AddressHelper wifiPgwAddress;
    wifiPgwAddress.SetBase("9.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiPgwInterfaces = wifiPgwAddress.Assign(p2pWifiPgwDevices);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gbps")));
    p2p.SetChannelAttribute("Delay", StringValue("5ms"));
    NetDeviceContainer p2pDevices = p2p.Install(pgw, server.Get(0));

    Ipv4AddressHelper internetAddress;
    internetAddress.SetBase("8.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ifPgwServer = internetAddress.Assign(p2pDevices);
    Ipv4Address serverIp = ifPgwServer.GetAddress(1);
    
    // Set constant positions for server and PGW for NetAnim - within grid
    MobilityHelper fixedMobility;
    Ptr<ListPositionAllocator> fixedPos = CreateObject<ListPositionAllocator>();
    fixedPos->Add(Vector(150.0, 150.0, 0.0)); // Server in grid
    fixedMobility.SetPositionAllocator(fixedPos);
    fixedMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    fixedMobility.Install(server);
    
    Ptr<ListPositionAllocator> pgwPos = CreateObject<ListPositionAllocator>();
    pgwPos->Add(Vector(75.0, 75.0, 0.0)); // PGW in grid
    fixedMobility.SetPositionAllocator(pgwPos);
    fixedMobility.Install(pgw);

    // Routing Setup
    Ipv4StaticRoutingHelper rt;
    
    // Bot routing through WiFi AP
    for (uint32_t i = 0; i < botNodes.GetN(); ++i) {
        Ptr<Ipv4> ipv4 = botNodes.Get(i)->GetObject<Ipv4>();
        auto s = rt.GetStaticRouting(ipv4);
        uint32_t wifiIfIndex = ipv4->GetInterfaceForDevice(botDevices.Get(i));
        s->SetDefaultRoute(botApInterfaces.GetAddress(0), wifiIfIndex);
        
        // Register bot IP for KPI tracking
        g_ipToNodeId[botInterfaces.GetAddress(i)] = botNodes.Get(i)->GetId();
        
        NS_LOG_INFO("Bot " << botNodes.Get(i)->GetId() 
                    << " WiFi IP: " << botInterfaces.GetAddress(i)
                    << " default route via " << botApInterfaces.GetAddress(0));
    }
    
    // Human routing through both WiFi and 5G
    for (uint32_t i = 0; i < humanNodes.GetN(); ++i) {
        Ptr<Node> hNode = humanNodes.Get(i);
        Ptr<Ipv4> ipv4 = hNode->GetObject<Ipv4>();
        auto s = rt.GetStaticRouting(ipv4);

        // Get interface index for WiFi + 5G devices
        uint32_t ifIndexWifi = ipv4->GetInterfaceForDevice(humanWifiDevices.Get(i));
        uint32_t ifIndex5G   = ipv4->GetInterfaceForDevice(humanUeDevices.Get(i));

        // Get next hops
        Ipv4Address wifiGwAddr = botApInterfaces.GetAddress(0);                   
        Ipv4Address epcGwAddr  = epcHelper->GetUeDefaultGatewayAddress();         

        // Default route via WiFi
        s->SetDefaultRoute(wifiGwAddr, ifIndexWifi);
        
        // Host route to server via 5G (alternative path)
        s->AddHostRouteTo(serverIp, epcGwAddr, ifIndex5G);

        // Register IPs for KPI tracking
        g_ipToNodeId[humanWifiInterfaces.GetAddress(i)] = hNode->GetId();
        g_ipToNodeId[humanInterfaces.GetAddress(i)] = hNode->GetId();

        NS_LOG_INFO("Human node " << hNode->GetId()
            << " WiFi-ifIndex=" << ifIndexWifi
            << " 5G-ifIndex=" << ifIndex5G);
    }

    // WiFi Gateway routing - forward to PGW for external traffic
    Ptr<Ipv4> wifiGwIpv4 = wifiGw.Get(0)->GetObject<Ipv4>();
    auto gwStatic = rt.GetStaticRouting(wifiGwIpv4);
    uint32_t gwToPgwIf = wifiGwIpv4->GetInterfaceForDevice(p2pWifiPgwDevices.Get(0));
    uint32_t gwToWifiIf = wifiGwIpv4->GetInterfaceForDevice(botApDevice.Get(0));
    
    // Default route to PGW for internet-bound traffic
    gwStatic->SetDefaultRoute(wifiPgwInterfaces.GetAddress(1), gwToPgwIf);
    
    // Route back to WiFi subnet stays local
    gwStatic->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"),
                                gwToWifiIf);
    
    NS_LOG_INFO("WiFi-GW routes: default via PGW (if " << gwToPgwIf 
                << "), 10.1.1.0/24 via WiFi (if " << gwToWifiIf << ")");

    // PGW routing - needs route back to WiFi subnet
    Ptr<Ipv4> pgwIpv4 = pgw->GetObject<Ipv4>();
    auto pgwStatic = rt.GetStaticRouting(pgwIpv4);
    int32_t pgwToWifiGwIf = pgwIpv4->GetInterfaceForAddress(wifiPgwInterfaces.GetAddress(1));
    
    if (pgwToWifiGwIf >= 0) {
        pgwStatic->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"),
                                     wifiPgwInterfaces.GetAddress(0), pgwToWifiGwIf);
        NS_LOG_INFO("PGW route: 10.1.1.0/24 -> " << wifiPgwInterfaces.GetAddress(0) 
                    << " via interface " << pgwToWifiGwIf);
    } else {
        NS_LOG_ERROR("Failed to find PGW interface for WiFi-GW link!");
    }
    
    // Server routing - default via PGW
    auto serverStatic = rt.GetStaticRouting(server.Get(0)->GetObject<Ipv4>());
    serverStatic->SetDefaultRoute(ifPgwServer.GetAddress(0), 1);

    // Initialize KPIs
    int botIdx = 0, humanIdx = 0;

    
    for (const auto& mapping : userMapping) {
        uint32_t nodeId;
        std::string networkType;
        
        if (mapping.second == 0) {
            nodeId = botNodes.Get(botIdx)->GetId();
            networkType = "WiFi";
            
            botIdx++;
        } else {
            nodeId = humanNodes.Get(humanIdx)->GetId();
            networkType = "5G+WiFi";
            
            // Map IP to node ID
            Ipv4Address addr = humanInterfaces.GetAddress(humanIdx);
            g_ipToNodeId[addr] = nodeId;
            
            // Set up mobility tracking for human nodes
            Ptr<MobilityModel> mobility = humanNodes.Get(humanIdx)->GetObject<MobilityModel>();
            if (mobility) {
                g_nodeKPIs[nodeId].lastPosition = mobility->GetPosition();
                mobility->TraceConnectWithoutContext("CourseChange",
                    MakeBoundCallback(&CourseChangeCallback, nodeId));
            }
            
            humanIdx++;
        }
        
        g_nodeKPIs[nodeId].nodeId = nodeId;
        g_nodeKPIs[nodeId].username = mapping.first;
        g_nodeKPIs[nodeId].nodeType = (mapping.second == 1) ? "human" : "bot";
        g_nodeKPIs[nodeId].networkType = networkType;
      
    }

    // Application Setup
    uint16_t serverPort = 8080;
    double simDuration = 60.0; 
    
    // TCP sink for all clients (both humans and bots - Twitter uses HTTPS/TCP)
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", 
                                     InetSocketAddress(Ipv4Address::GetAny(), serverPort));
    ApplicationContainer sinkApps = packetSinkHelper.Install(server.Get(0));
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(simDuration + 10.0));

    // Find time range for normalization
    double firstTweetTime = std::numeric_limits<double>::max();
    double latestTweetTime = 0.0;
    for (const auto &tweet : tweets) {
        double t = ConvertTimestampToSeconds(tweet.tweet_timestamp);
        if (t < firstTweetTime) firstTweetTime = t;
        if (t > latestTweetTime) latestTweetTime = t;
    }

    double totalTimeSpan = latestTweetTime - firstTweetTime;
    
    NS_LOG_INFO("Compressing " << totalTimeSpan << " seconds of real tweets into " 
                << simDuration << " seconds of simulation");

    // Create client sockets and schedule packets
    std::vector<Ptr<Socket>> clientSockets(usernames.size());
    std::vector<Ptr<Socket>> client5GSockets(usernames.size());
    InetSocketAddress remoteAddress(serverIp, serverPort);

    botIdx = 0;
    humanIdx = 0;
    
    NS_LOG_INFO("Setting up client applications and scheduling packets...");
    
    for (uint32_t i = 0; i < usernames.size(); ++i) {
        const std::string &username = usernames[i];
        int label = userMapping[i].second;

        Ptr<Node> clientNode = (label == 1) ? humanNodes.Get(humanIdx) : botNodes.Get(botIdx);
        
        // All clients use TCP (Twitter uses HTTPS which is TCP-based)
        Ptr<Socket> socket = Socket::CreateSocket(clientNode, TcpSocketFactory::GetTypeId());
        clientSockets[i] = socket;

        // Reference KPI entry for this node
        uint32_t nodeId = clientNode->GetId();
        NodeKPI &kpi = g_nodeKPIs[nodeId];

        // For humans, create both WiFi and 5G sockets
        if (label == 1) {
            // WiFi socket
            socket->BindToNetDevice(humanWifiDevices.Get(humanIdx));
            // Delay connection to allow WiFi association to complete
            Simulator::Schedule(Seconds(0.5), [socket, remoteAddress]() {
                socket->Connect(remoteAddress);
            });
            NS_LOG_INFO("Human node " << clientNode->GetId() << " WiFi socket bound, connecting at t=0.5s");

            // 5G socket
            Ptr<Socket> socket5G = Socket::CreateSocket(clientNode, TcpSocketFactory::GetTypeId());
            socket5G->BindToNetDevice(humanUeDevices.Get(humanIdx));
            Simulator::Schedule(Seconds(0.5), [socket5G, remoteAddress]() {
                socket5G->Connect(remoteAddress);
            });
            client5GSockets[i] = socket5G;
            NS_LOG_INFO("Human node " << clientNode->GetId() << " 5G socket bound, connecting at t=0.5s");
            humanIdx++;
        } else {
            // Bots use WiFi only with TCP
            socket->BindToNetDevice(botDevices.Get(botIdx));
            // Delay connection to allow WiFi association to complete
            Simulator::Schedule(Seconds(0.5), [socket, remoteAddress]() {
                socket->Connect(remoteAddress);
            });
            NS_LOG_INFO("Bot " << clientNode->GetId() 
                        << " TCP socket bound, connecting to " << serverIp 
                        << ":" << serverPort << " at t=0.5s");
            
            // Log routing table
            Ptr<Ipv4> ipv4 = clientNode->GetObject<Ipv4>();
            NS_LOG_INFO("Bot " << clientNode->GetId() << " has " 
                        << ipv4->GetNInterfaces() << " interfaces");
            
            for (uint32_t j = 0; j < ipv4->GetNInterfaces(); j++) {
                NS_LOG_INFO("  Interface " << j << ": " 
                            << ipv4->GetAddress(j, 0).GetLocal());
            }
            botIdx++;
        }

        // Build current user tweet list
        std::vector<TwitterData> userTweets;
        for (const auto &twt : tweets) {
            if (twt.username == username) {
                userTweets.push_back(twt);
            }
        }

        if (userTweets.empty()) {
            NS_LOG_INFO("User " << username << " has no tweets, skipping scheduling");
            continue;
        }

        // Sort this user's tweets by timestamp
        std::sort(userTweets.begin(), userTweets.end(),
                  [](const TwitterData &a, const TwitterData &b) {
                      return ConvertTimestampToSeconds(a.tweet_timestamp) <
                             ConvertTimestampToSeconds(b.tweet_timestamp);
                  });

 
        // Compute real interarrival times (IATs) between tweets
        std::vector<double> iatReal;
        for (size_t k = 1; k < userTweets.size(); ++k) {
            double tPrev = ConvertTimestampToSeconds(userTweets[k - 1].tweet_timestamp);
            double tCurr = ConvertTimestampToSeconds(userTweets[k].tweet_timestamp);
            double diff = std::max(0.0, tCurr - tPrev);
            iatReal.push_back(diff);
        }

        double sumIatReal = 0.0;
        for (double v : iatReal) sumIatReal += v;
        if (sumIatReal <= 0.0) {
            sumIatReal = 1.0; // avoid divide-by-zero for single-tweet users
        }

        // Fill KPI tweet-level stats for this node
        kpi.tweetCount = static_cast<uint32_t>(userTweets.size());
        if (!iatReal.empty()) {
            double meanIat = sumIatReal / static_cast<double>(iatReal.size());
            double var = 0.0;
            for (double v : iatReal) {
                double d = v - meanIat;
                var += d * d;
            }
            var /= static_cast<double>(iatReal.size());
            kpi.avgIatReal = meanIat;
            kpi.stdIatReal = std::sqrt(var);
        }

        // Small random offset so all users don't start at the exact same time
        std::uniform_real_distribution<double> offsetDist(0.0, 0.5);
        double sendTime = 1.0 + offsetDist(gen);

        // Distributions for jitter and storm packet counts
        std::uniform_int_distribution<uint32_t> humanJitterDist(0, 400);
        std::uniform_int_distribution<int> stormCountDist(2, 5); // 2–5 packets in a storm

        int totalPacketsScheduled = 0;

        for (size_t k = 0; k < userTweets.size(); ++k) {
            // Add scaled interarrival time for tweets after the first
            if (k > 0 && !iatReal.empty()) {
                double iatR = iatReal[k - 1];
                double iatSim = (iatR / sumIatReal) * (simDuration - 2.0);
                sendTime += iatSim;
            }

            // Tweet storm detection: very small real-world IAT
            bool isStorm = (k > 0 && !iatReal.empty() && iatReal[k - 1] < 3.0);
            // Update KPI
            if (isStorm) {
                kpi.stormTweetCount++;
            }

            // Base packet size from tweet text
            uint32_t textBytes = static_cast<uint32_t>(userTweets[k].tweet_text.size());
            uint32_t headerBytes = 40;
            uint32_t baseSize = headerBytes + textBytes;

            // How many packets to send for this tweet
            int packetCount = isStorm ? stormCountDist(gen) : 1;

                for (int p = 0; p < packetCount; ++p) {
                uint32_t pktSize;

                if (label == 0) {
                    // Bots: more uniform packet size, small fixed jitter
                    uint32_t jitter = 20;
                    pktSize = std::min<uint32_t>(512, std::max<uint32_t>(64, baseSize + jitter));
                } else {
                    // Humans: more variable packet size
                    uint32_t jitter = humanJitterDist(gen);
                    pktSize = std::min<uint32_t>(1500, std::max<uint32_t>(200, baseSize + jitter));
                }

                // Schedule helper to choose WiFi vs 5G at send time
                Simulator::Schedule(
                    Seconds(sendTime + p * 0.01),
                    &SendTwitterPacket,
                    clientNode,
                    socket,                      
                    (label == 1 ? client5GSockets[i] : Ptr<Socket>(nullptr)), 
                    label,
                    pktSize,
                    serverIp,
                    serverPort
                );

                totalPacketsScheduled++;
            }
        }

        NS_LOG_INFO("User " << username << " (" << (label == 1 ? "human" : "bot")
                    << "): scheduled " << totalPacketsScheduled
                    << " packets from " << userTweets.size() << " tweets");
    }

    

    // Setup NetAnim (optional - uses lots of memory)
    AnimationInterface* anim = nullptr;
    if (enableNetAnim) {
        NS_LOG_INFO("Setting up NetAnim...");
        anim = new AnimationInterface(animFile);
        anim->SetStartTime(Seconds(0));
        anim->SetStopTime(Seconds(simDuration + 10.0));
        anim->SetMaxPktsPerTraceFile(5000000);
        anim->UpdateNodeDescription(server.Get(0), "Server");
        anim->UpdateNodeDescription(pgw, "PGW");
        anim->UpdateNodeDescription(wifiGw.Get(0), "WiFi-GW");
        anim->UpdateNodeDescription(humanGnb.Get(0), "5G-gNB");
        
        // Color infrastructure nodes differently
        anim->UpdateNodeColor(server.Get(0), 0, 255, 0); // Green - server
        anim->UpdateNodeColor(pgw, 128, 128, 128); // Gray - PGW
        anim->UpdateNodeColor(wifiGw.Get(0), 255, 165, 0); // Orange - WiFi gateway
        anim->UpdateNodeColor(humanGnb.Get(0), 0, 128, 128); // Teal - gNB
        anim->UpdateNodeSize(server.Get(0)->GetId(), 5.0, 5.0);
        anim->UpdateNodeSize(pgw->GetId(), 4.0, 4.0);
        anim->UpdateNodeSize(wifiGw.Get(0)->GetId(), 4.0, 4.0);
        anim->UpdateNodeSize(humanGnb.Get(0)->GetId(), 4.0, 4.0);
        
        for (uint32_t i = 0; i < NodeContainer::GetGlobal().GetN(); i++) {
            Ptr<Node> node = NodeContainer::GetGlobal().Get(i);
            uint32_t nodeId = node->GetId();
            
            if (g_nodeKPIs.find(nodeId) != g_nodeKPIs.end()) {
                NodeKPI& kpi = g_nodeKPIs[nodeId];
                anim->UpdateNodeDescription(node, kpi.nodeType + "-" + std::to_string(nodeId));
                
                if (kpi.nodeType == "human") {
                    anim->UpdateNodeColor(node, 0, 0, 255); // Blue
                    anim->UpdateNodeSize(nodeId, 3.0, 3.0);
                } else {
                    anim->UpdateNodeColor(node, 255, 0, 0); // Red
                    anim->UpdateNodeSize(nodeId, 2.5, 2.5);
                }
            } else {
                // Label EPC infrastructure nodes (SGW, MME)
                if (nodeId != server.Get(0)->GetId() && 
                    nodeId != pgw->GetId() && 
                    nodeId != wifiGw.Get(0)->GetId() && 
                    nodeId != humanGnb.Get(0)->GetId()) {
                    static int epcNodeCount = 0;
                    std::string epcLabel = (epcNodeCount == 0) ? "SGW" : "MME";
                    epcNodeCount++;
                    anim->UpdateNodeDescription(node, epcLabel);
                    anim->UpdateNodeColor(node, 100, 100, 100); // Dark gray
                    anim->UpdateNodeSize(nodeId, 2.0, 2.0);
                }
            }
        }
    } else {
        NS_LOG_INFO("NetAnim disabled (use --enableNetAnim=true to enable)");
    }

    // Setup FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // Enable tracing (optional - significantly slows down simulation)
    if (enableTracing) {
        NS_LOG_INFO("Enabling PCAP and NR tracing (this will slow down simulation)...");
        
        // Bot WiFi devices - labeled as "bot-wifi"
        for (uint32_t i = 0; i < botDevices.GetN(); ++i) {
            std::stringstream ss;
            ss << pcapDir << "/bot-wifi-" << i;
            phy.EnablePcap(ss.str(), botDevices.Get(i), true);
        }
        
        // Human WiFi devices - labeled as human-wifi
        for (uint32_t i = 0; i < humanWifiDevices.GetN(); ++i) {
            std::stringstream ss;
            ss << pcapDir << "/human-wifi-" << i;
            phy.EnablePcap(ss.str(), humanWifiDevices.Get(i), true);
        }
        
        // P2P links (carry 5G traffic for humans)
        p2p.EnablePcapAll(pcapDir + "/p2p", true);
        
        // NR (5G) traces - very slow!
        nrHelper->EnableTraces();
    } else {
        NS_LOG_INFO("Tracing disabled (use --enableTracing=true to enable)");
    }

   

    // Run sim
    NS_LOG_INFO("Running simulation for " << (simDuration + 10.0) << " seconds...");
    Simulator::Stop(Seconds(simDuration + 10.0));
    
    auto start = std::chrono::high_resolution_clock::now();
    Simulator::Schedule(Seconds(0.0), &PrintElapsedTime, start);
    
    Simulator::Run();
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << std::endl;
    NS_LOG_INFO("Simulation completed in " << elapsed.count() << " seconds");

    // Collect flow statistics
    NS_LOG_INFO("Collecting flow statistics...");
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    // Update KPIs from flow monitor stats (aggregate across flows per node)
    for (auto& pair : g_nodeKPIs) {
        pair.second.txPackets = 0;
        pair.second.rxPackets = 0;
        pair.second.totalBytes = 0;
    }

    // Aggregate flow statistics per node
    for (auto& flow : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        FlowMonitor::FlowStats& fs = flow.second;
        
        auto it = g_ipToNodeId.find(t.sourceAddress);
        if (it != g_ipToNodeId.end()) {
            uint32_t nodeId = it->second;
            NodeKPI& kpi = g_nodeKPIs[nodeId];
            
            NS_LOG_INFO("Flow for Node " << nodeId << " (" << kpi.nodeType << "):");
            NS_LOG_INFO("  TxPackets: " << fs.txPackets << ", RxPackets: " << fs.rxPackets);

            // Accumulate actual sent/received counts
            uint32_t prevRx = kpi.rxPackets;
            kpi.txPackets += fs.txPackets;
            kpi.rxPackets += fs.rxPackets;
            kpi.totalBytes += fs.txBytes;
            

            // Weighted average delay/jitter
            if (fs.rxPackets > 0) {
                double flowAvgDelayMs = (fs.delaySum.GetSeconds() / fs.rxPackets) * 1000.0;
                double flowAvgJitterMs = 0.0;
                if (fs.rxPackets > 1) {
                    flowAvgJitterMs = (fs.jitterSum.GetSeconds() / (fs.rxPackets - 1)) * 1000.0;
                }

                uint32_t newRx = kpi.rxPackets;
                if (prevRx == 0) {
                    kpi.avgDelay = flowAvgDelayMs;
                    kpi.avgJitter = flowAvgJitterMs;
                } else {
                    kpi.avgDelay = (kpi.avgDelay * prevRx + flowAvgDelayMs * fs.rxPackets) / 
                                static_cast<double>(newRx);
                    kpi.avgJitter = (kpi.avgJitter * prevRx + flowAvgJitterMs * fs.rxPackets) / 
                                static_cast<double>(newRx);
                }
            }
        }
    }

    NS_LOG_INFO("\n FINAL NODE STATISTICS ");
    for (auto& pair : g_nodeKPIs) {
        NodeKPI& kpi = pair.second;

        if (kpi.txPackets > 0) {
            kpi.lostPackets = (kpi.txPackets > kpi.rxPackets) ? 
                            (kpi.txPackets - kpi.rxPackets) : 0;
            kpi.packetLossRate = static_cast<double>(kpi.lostPackets) / 
                                static_cast<double>(kpi.txPackets);
        } else {
            kpi.lostPackets = 0;
            kpi.packetLossRate = 0.0;
        }

        // Calculate throughput
        if (kpi.rxPackets > 0 && simDuration > 0.0) {
            kpi.throughput = (kpi.totalBytes * 8.0) / simDuration;
        }
        
        NS_LOG_INFO("Node " << kpi.nodeId << " [" << kpi.username << "] (" << kpi.nodeType << "):");
        NS_LOG_INFO("  TX=" << kpi.txPackets << ", RX=" << kpi.rxPackets 
                    << ", Lost=" << kpi.lostPackets);
        NS_LOG_INFO("  Loss Rate=" << (kpi.packetLossRate * 100) << "%");
        NS_LOG_INFO("  Throughput=" << (kpi.throughput/1000) << " Kbps");
    }


    // Finalize packet loss rate and throughput per node
    for (auto& pair : g_nodeKPIs) {
        NodeKPI& kpi = pair.second;

        if (kpi.txPackets > 0) {
            kpi.packetLossRate =
            static_cast<double>(kpi.txPackets - kpi.rxPackets) / 
            static_cast<double>(kpi.txPackets);
        }

        if (kpi.rxPackets > 0 && simDuration > 0.0) {
            // Approximate throughput over the simulation window
            kpi.throughput = (kpi.totalBytes * 8.0) / simDuration;
        }
    }


    // Export KPI CSV idk here 
    // TODO: need to find more meaningful KPIs, need useragent here
    NS_LOG_INFO("Exporting KPI data to " << csvFile);
    std::ofstream kpiFile(csvFile);
    kpiFile << "NodeID,Username,NodeType,NetworkType,"
            << "TxPackets,RxPackets,LostPackets,PacketLossRate,TotalBytes,"
            << "TweetCount,StormTweetCount,AvgIAT_s,StdIAT_s,PlannedWifiPkts,Planned5GPkts,"
            << "AvgDelay_ms,AvgJitter_ms,Throughput_bps,TotalDistance_m\n";

    for (auto& pair : g_nodeKPIs) {
        NodeKPI& kpi = pair.second;
        kpiFile << kpi.nodeId << ","
                << kpi.username << ","
                << kpi.nodeType << ","
                << kpi.networkType << ","
                << kpi.txPackets << ","
                << kpi.rxPackets << ","
                << kpi.lostPackets << ","
                << kpi.packetLossRate << ","
                << kpi.totalBytes << ","
                << kpi.tweetCount << ","
                << kpi.stormTweetCount << ","
                << kpi.avgIatReal << ","
                << kpi.stdIatReal << ","
                << kpi.plannedWifiPackets << ","
                << kpi.planned5gPackets << ","
                << kpi.avgDelay << ","
                << kpi.avgJitter << ","
                << kpi.throughput << ","
                << kpi.totalDistance << "\n";
    }
    kpiFile.close();

    NS_LOG_INFO("\nOutput Files Generated");
    NS_LOG_INFO("1. KPI CSV: " << csvFile);
    if (enableNetAnim) {
        NS_LOG_INFO("2. NetAnim: " << animFile);
    } else {
        NS_LOG_INFO("2. NetAnim: DISABLED");
    }

    // Cleanup
    if (anim != nullptr) {
        delete anim;
    }

    Simulator::Destroy();
    NS_LOG_INFO("Simulation completed successfully!");

    return 0;
}