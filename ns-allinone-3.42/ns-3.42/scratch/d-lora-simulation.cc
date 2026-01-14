
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lorawan-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/random-variable-stream.h"
#include "ns3/config-store-module.h"
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DLoRaSimulation");

// Global variables for metrics
double g_totalPacketsSent = 0;
double g_totalPacketsReceived = 0;
double g_totalEnergyConsumed = 0;
double g_totalDataReceived = 0;
double g_totalSimulationTime = 0;
double g_totalTimeOnAir = 0;
double g_totalRSSI = 0;
double g_totalSNR = 0;
uint32_t g_rssiMeasurements = 0;
uint32_t g_snrMeasurements = 0;
uint32_t g_collisions = 0;

// Output files
std::ofstream g_intervalFile;
std::ofstream g_detailsFile;

// LoRaWAN parameters from the paper
const double LPL_D0 = 128.95; // dB
const double D0 = 1000; // m
const double GAMMA = 2.32;
const double DELTA1 = 7.8;
const double DELTA2 = 1.0;
const double C_WEIGHT_FACTOR = 2.0;

// LoRa parameters sets
std::vector<int> SF_SET = {7, 8, 9, 10, 11, 12};
std::vector<double> BW_SET = {125e3, 250e3, 500e3}; // in Hz
std::vector<double> CF_SET = {470.1e6, 470.3e6, 470.5e6, 470.7e6, 470.9e6, 471.1e6, 471.3e6, 471.5e6}; // in Hz
std::vector<double> TP_SET = {2, 4, 6, 8, 10, 12, 14}; // in dBm

// Receiver Sensitivities (TABLE I from paper)
std::map<int, std::map<double, double>> RS_TABLE = {
    {7, {{125e3, -123}, {250e3, -120}, {500e3, -116}}},
    {8, {{125e3, -126}, {250e3, -123}, {500e3, -119}}},
    {9, {{125e3, -129}, {250e3, -125}, {500e3, -122}}},
    {10, {{125e3, -132}, {250e3, -128}, {500e3, -125}}},
    {11, {{125e3, -133}, {250e3, -130}, {500e3, -128}}},
    {12, {{125e3, -136}, {250e3, -133}, {500e3, -130}}}
};

// SINR Thresholds (TABLE II from paper)
std::map<int, double> SINR_REQ_TABLE = {
    {7, -7.5},
    {8, -10.0},
    {9, -12.5},
    {10, -15.0},
    {11, -17.5},
    {12, -20.0}
};

// Packet size
const uint32_t PAYLOAD_SIZE = 20; // bytes

// D-LoRa variant parameters
double g_xi = 0;    // ξ
double g_zeta = 0;  // ζ
double g_eta = 0;   // η

// Helper function for distance calculation
double MyCalculateDistance (Vector p1, Vector p2)
{
    return std::sqrt (std::pow (p1.x - p2.x, 2) + std::pow (p1.y - p2.y, 2));
}

// Helper function to calculate RSSI with improved path loss model
double CalculateRSSI (Ptr<Node> endDevice, Ptr<Node> gateway, double txPower, double pathLossExponent, double shadowFadingStdDev)
{
    // Get positions
    Ptr<MobilityModel> endDeviceMobility = endDevice->GetObject<MobilityModel> ();
    Ptr<MobilityModel> gatewayMobility = gateway->GetObject<MobilityModel> ();
    
    if (!endDeviceMobility || !gatewayMobility)
    {
        NS_LOG_WARN("Mobility models not found, using default RSSI");
        return -100.0; // Default RSSI
    }
    
    Vector endDevicePosition = endDeviceMobility->GetPosition ();
    Vector gatewayPosition = gatewayMobility->GetPosition ();

    // Calculate distance
    double distance = MyCalculateDistance (endDevicePosition, gatewayPosition);
    distance = std::max(distance, 1.0); // Minimum 1m distance

    // Log-Distance Path Loss Model with shadow fading
    Ptr<NormalRandomVariable> normalRv = CreateObject<NormalRandomVariable> ();
    normalRv->SetAttribute ("Mean", DoubleValue (0.0));
    normalRv->SetAttribute ("Variance", DoubleValue (shadowFadingStdDev * shadowFadingStdDev));
    double xDelta = normalRv->GetValue ();

    double pathLoss = LPL_D0 + 10 * pathLossExponent * std::log10 (distance / D0) + xDelta;
    double rssi = txPower - pathLoss;
    
    return rssi;
}

// Helper function to calculate ToA (Time on Air) - Corrected according to paper
double CalculateToA (int sf, double bw, uint32_t payloadSize)
{
    // Based on LoRaWAN specification and paper
    // Symbol duration: T_sym = 2^SF / BW
    double symbolDuration = std::pow(2, sf) / bw;
    
    // Preamble duration: T_pre = (n_pre + 4.25) * T_sym
    double npre = 8; // Default preamble size
    double preambleDuration = (npre + 4.25) * symbolDuration;

    // Calculate payload symbols according to equation (9) from paper
    // n_pay = 8 + max(ceil((8*PS - 4*SF + 28 + 16*CRC - 20*H) / (4*(SF - 2*DE))) * (CR + 4), 0)
    double PS = payloadSize; // Payload size in bytes
    double CRC = 1; // CRC enabled
    double H = 0;   // Header enabled
    double DE = 0;  // LowDataRateOptimization disabled
    double CR = 1;  // Coding rate 4/5
    
    double term1 = 8.0 * PS - 4.0 * sf + 28.0 + 16.0 * CRC - 20.0 * H;
    double term2 = 4.0 * (sf - 2.0 * DE);
    double npay = 8.0 + std::max(std::ceil(term1 / term2) * (CR + 4.0), 0.0);
    
    double payloadDuration = npay * symbolDuration;

    return preambleDuration + payloadDuration;
}

// Corrected helper function to calculate energy consumption according to paper
double CalculateEnergyConsumption (double txPowerDbm, double toa)
{
    // According to paper equation (10): Ej = TPj · ToAj
    // Where TPj is in dBm and ToAj is in seconds
    // Result should be in mJ (millijoules)
    
    // Convert dBm to mW: P_mW = 10^(P_dBm / 10)
    double txPowerMw = std::pow(10.0, txPowerDbm / 10.0);
    
    // Energy = Power × Time
    // Since we want result in mJ and toa is in seconds, power is in mW
    double energyMj = txPowerMw * toa; // mJ
    
    return energyMj;
}

// Helper function to check collision (simplified)
bool CheckCollision(int sf1, double cf1, int sf2, double cf2)
{
    // Same SF and same frequency = collision
    if (sf1 == sf2 && std::abs(cf1 - cf2) < 1e6) // 1 MHz tolerance
    {
        return true;
    }
    
    // Different SFs on same frequency may still collide under certain conditions
    if (std::abs(cf1 - cf2) < 1e6)
    {
        // Simplified collision model: higher probability for similar SFs
        int sf_diff = std::abs(sf1 - sf2);
        if (sf_diff <= 1)
        {
            Ptr<UniformRandomVariable> uniform = CreateObject<UniformRandomVariable>();
            return uniform->GetValue() < 0.3; // 30% collision probability
        }
        else if (sf_diff <= 2)
        {
            Ptr<UniformRandomVariable> uniform = CreateObject<UniformRandomVariable>();
            return uniform->GetValue() < 0.1; // 10% collision probability
        }
    }
    
    return false; // No collision
}

// Base class for parameter selection algorithms
class BaseAlgorithm : public Object
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::BaseAlgorithm")
            .SetParent<Object> ()
        ;
        return tid;
    }

    BaseAlgorithm () 
        : m_totalSelections(0),
          m_successfulSelections(0)
    {}

    virtual std::tuple<int, double, double, double> SelectParameters (Ptr<Node> node, Ptr<Node> gateway) = 0;
    virtual void UpdateRewards (int sf, double bw, double cf, double tp, bool success, double dataRate, double energyConsumption) = 0;
    
    // Statistics
    uint32_t GetTotalSelections() { return m_totalSelections; }
    uint32_t GetSuccessfulSelections() { return m_successfulSelections; }
    double GetSuccessRate() { return m_totalSelections > 0 ? (double)m_successfulSelections / m_totalSelections : 0.0; }

protected:
    uint32_t m_totalSelections;
    uint32_t m_successfulSelections;
};

// D-LoRa Agent implementation
class DLoRaAgent : public Object
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::DLoRaAgent")
            .SetParent<Object> ()
            .AddConstructor<DLoRaAgent> ()
        ;
        return tid;
    }

    DLoRaAgent ()
        : m_rng (CreateObject<UniformRandomVariable> ())
    {
        // Initialize expected rewards and number of selections for each base arm
        for (int sf : SF_SET)
        {
            m_expectedRewardsSF[sf] = 0.0;
            m_numSelectionsSF[sf] = 0;
        }
        for (double bw : BW_SET)
        {
            m_expectedRewardsBW[bw] = 0.0;
            m_numSelectionsBW[bw] = 0;
        }
        for (double cf : CF_SET)
        {
            m_expectedRewardsCF[cf] = 0.0;
            m_numSelectionsCF[cf] = 0;
        }
        for (double tp : TP_SET)
        {
            m_expectedRewardsTP[tp] = 0.0;
            m_numSelectionsTP[tp] = 0;
        }
    }

    void SetNodeAndGateway (Ptr<Node> node, Ptr<Node> gateway)
    {
        m_node = node;
        m_gateway = gateway;
    }

    std::tuple<int, double, double, double> SelectParameters ()
    {
        int selectedSF = SelectArm (m_expectedRewardsSF, m_numSelectionsSF, SF_SET);
        double selectedBW = SelectArm (m_expectedRewardsBW, m_numSelectionsBW, BW_SET);
        double selectedCF = SelectArm (m_expectedRewardsCF, m_numSelectionsCF, CF_SET);
        double selectedTP = SelectArm (m_expectedRewardsTP, m_numSelectionsTP, TP_SET);

        return std::make_tuple (selectedSF, selectedBW, selectedCF, selectedTP);
    }

    void UpdateRewards (int sf, double bw, double cf, double tp, bool success, double dataRate, double energyConsumption)
    {
        // Calculate rewards based on success and metric factors
        double rewardSF = CalculateRewardSF (sf, success);
        double rewardBW = CalculateRewardBW (bw, success);
        double rewardCF = CalculateRewardCF (cf, success);
        double rewardTP = CalculateRewardTP (tp, success);

        UpdateArm (m_expectedRewardsSF, m_numSelectionsSF, sf, rewardSF);
        UpdateArm (m_expectedRewardsBW, m_numSelectionsBW, bw, rewardBW);
        UpdateArm (m_expectedRewardsCF, m_numSelectionsCF, cf, rewardCF);
        UpdateArm (m_expectedRewardsTP, m_numSelectionsTP, tp, rewardTP);
    }

private:
    Ptr<Node> m_node;
    Ptr<Node> m_gateway;
    Ptr<UniformRandomVariable> m_rng;

    std::map<int, double> m_expectedRewardsSF;
    std::map<int, uint32_t> m_numSelectionsSF;
    std::map<double, double> m_expectedRewardsBW;
    std::map<double, uint32_t> m_numSelectionsBW;
    std::map<double, double> m_expectedRewardsCF;
    std::map<double, uint32_t> m_numSelectionsCF;
    std::map<double, double> m_expectedRewardsTP;
    std::map<double, uint32_t> m_numSelectionsTP;

    template <typename T>
    T SelectArm (std::map<T, double>& expectedRewards, std::map<T, uint32_t>& numSelections, const std::vector<T>& armSet)
    {
        double maxUCB = -1.0;
        T selectedArm = armSet[0];

        uint32_t totalSelections = 0;
        for (auto const& [key, val] : numSelections)
        {
            totalSelections += val;
        }

        for (T arm : armSet)
        {
            double ucbValue;
            if (numSelections[arm] == 0)
            {
                ucbValue = std::numeric_limits<double>::max();
            }
            else
            {
                ucbValue = expectedRewards[arm] + 
                          C_WEIGHT_FACTOR * std::sqrt (std::log (totalSelections + 1) / (2.0 * numSelections[arm]));
            }

            if (ucbValue > maxUCB)
            {
                maxUCB = ucbValue;
                selectedArm = arm;
            }
        }
        return selectedArm;
    }

    template <typename T>
    void UpdateArm (std::map<T, double>& expectedRewards, std::map<T, uint32_t>& numSelections, T arm, double reward)
    {
        numSelections[arm]++;
        expectedRewards[arm] = expectedRewards[arm] + (reward - expectedRewards[arm]) / numSelections[arm];
    }

    // Reward functions based on D-LoRa variants (equations 20-23 from paper)
    double CalculateRewardSF (int sf, bool success)
    {
        double r_sf = success ? 1.0 : 0.0;
        if (g_xi > 0)
        {
            double sum_2_sf = 0;
            for (int s : SF_SET)
            {
                sum_2_sf += std::pow (2, s);
            }
            r_sf += g_xi * (std::pow (2, sf) / sum_2_sf);
        }
        return r_sf;
    }

    double CalculateRewardBW (double bw, bool success)
    {
        double r_bw = success ? 1.0 : 0.0;
        if (g_zeta > 0)
        {
            double sum_bw = 0;
            for (double b : BW_SET)
            {
                sum_bw += b;
            }
            r_bw += g_zeta * (bw / sum_bw);
        }
        return r_bw;
    }

    double CalculateRewardCF (double cf, bool success)
    {
        return success ? 1.0 : 0.0;
    }

    double CalculateRewardTP (double tp, bool success)
    {
        double r_tp = success ? 1.0 : 0.0;
        if (g_eta > 0)
        {
            double sum_tp = 0;
            for (double t : TP_SET)
            {
                sum_tp += t;
            }
            r_tp += g_eta * (1.0 - (tp / sum_tp));
        }
        return r_tp;
    }
};

// Random Algorithm
class RandomAlgorithm : public BaseAlgorithm
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::RandomAlgorithm")
            .SetParent<BaseAlgorithm> ()
            .AddConstructor<RandomAlgorithm> ()
        ;
        return tid;
    }

    RandomAlgorithm ()
        : m_rng (CreateObject<UniformRandomVariable> ())
    {
    }

    std::tuple<int, double, double, double> SelectParameters (Ptr<Node> node, Ptr<Node> gateway) override
    {
        m_totalSelections++;
        
        int sf = SF_SET[m_rng->GetInteger(0, SF_SET.size() - 1)];
        double bw = BW_SET[m_rng->GetInteger(0, BW_SET.size() - 1)];
        double cf = CF_SET[m_rng->GetInteger(0, CF_SET.size() - 1)];
        double tp = TP_SET[m_rng->GetInteger(0, TP_SET.size() - 1)];
        
        return std::make_tuple (sf, bw, cf, tp);
    }

    void UpdateRewards (int sf, double bw, double cf, double tp, bool success, double dataRate, double energyConsumption) override
    {
        if (success) m_successfulSelections++;
    }

private:
    Ptr<UniformRandomVariable> m_rng;
};

// Round-Robin Algorithm
class RoundRobinAlgorithm : public BaseAlgorithm
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::RoundRobinAlgorithm")
            .SetParent<BaseAlgorithm> ()
            .AddConstructor<RoundRobinAlgorithm> ()
        ;
        return tid;
    }

    RoundRobinAlgorithm ()
        : m_rng (CreateObject<UniformRandomVariable> ())
    {
    }

    std::tuple<int, double, double, double> SelectParameters (Ptr<Node> node, Ptr<Node> gateway) override
    {
        m_totalSelections++;
        
        uint32_t nodeId = node->GetId ();
        int sf = SF_SET[nodeId % SF_SET.size ()];
        double cf = CF_SET[nodeId % CF_SET.size ()];
        double bw = BW_SET[m_rng->GetInteger(0, BW_SET.size() - 1)];
        double tp = TP_SET[m_rng->GetInteger(0, TP_SET.size() - 1)];
        
        return std::make_tuple (sf, bw, cf, tp);
    }

    void UpdateRewards (int sf, double bw, double cf, double tp, bool success, double dataRate, double energyConsumption) override
    {
        if (success) m_successfulSelections++;
    }

private:
    Ptr<UniformRandomVariable> m_rng;
};

// ADR Algorithm - Corrected to use minimum power and optimize for energy efficiency
class ADRAlgorithm : public BaseAlgorithm
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::ADRAlgorithm")
            .SetParent<BaseAlgorithm> ()
            .AddConstructor<ADRAlgorithm> ()
        ;
        return tid;
    }

    ADRAlgorithm () {}

    std::tuple<int, double, double, double> SelectParameters (Ptr<Node> node, Ptr<Node> gateway) override
    {
        m_totalSelections++;
        
        // Calculate distance to gateway and select parameters based on it
        Ptr<MobilityModel> nodeMobility = node->GetObject<MobilityModel> ();
        Ptr<MobilityModel> gatewayMobility = gateway->GetObject<MobilityModel> ();
        
        if (!nodeMobility || !gatewayMobility)
        {
            return std::make_tuple (7, BW_SET[0], CF_SET[0], TP_SET[0]);
        }
        
        Vector nodePosition = nodeMobility->GetPosition ();
        Vector gatewayPosition = gatewayMobility->GetPosition ();
        double distance = MyCalculateDistance (nodePosition, gatewayPosition);

        // ADR logic: closer nodes use lower SF, farther nodes use higher SF
        // This is the key to ADR's energy efficiency
        int sf;
        if (distance < 500) sf = 7;
        else if (distance < 800) sf = 8;
        else if (distance < 1100) sf = 9;
        else if (distance < 1400) sf = 10;
        else if (distance < 1700) sf = 11;
        else sf = 12;

        double bw = BW_SET[0]; // 125 kHz for better sensitivity
        
        Ptr<UniformRandomVariable> rngCF = CreateObject<UniformRandomVariable> ();
        double cf = CF_SET[rngCF->GetInteger(0, CF_SET.size() - 1)];
        
        // ADR always uses minimum transmission power to save energy
        double tp = TP_SET[0]; // Minimum power (2 dBm)
        
        return std::make_tuple (sf, bw, cf, tp);
    }

    void UpdateRewards (int sf, double bw, double cf, double tp, bool success, double dataRate, double energyConsumption) override
    {
        if (success) m_successfulSelections++;
    }
};

// RS-LoRa Algorithm
class RSLoRaAlgorithm : public BaseAlgorithm
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::RSLoRaAlgorithm")
            .SetParent<BaseAlgorithm> ()
            .AddConstructor<RSLoRaAlgorithm> ()
        ;
        return tid;
    }

    RSLoRaAlgorithm ()
        : m_rng (CreateObject<UniformRandomVariable> ())
    {
    }

    std::tuple<int, double, double, double> SelectParameters (Ptr<Node> node, Ptr<Node> gateway) override
    {
        m_totalSelections++;
        
        // Prefer smaller SFs (lower collision probability, higher data rate)
        double rand = m_rng->GetValue();
        
        int sf;
        if (rand < 0.4) sf = 7;
        else if (rand < 0.7) sf = 8;
        else if (rand < 0.85) sf = 9;
        else if (rand < 0.93) sf = 10;
        else if (rand < 0.98) sf = 11;
        else sf = 12;

        // Prefer larger BW for higher data rate
        double randBW = m_rng->GetValue();
        double bw;
        if (randBW < 0.5) bw = BW_SET[2]; // 500 kHz
        else if (randBW < 0.8) bw = BW_SET[1]; // 250 kHz
        else bw = BW_SET[0]; // 125 kHz

        double cf = CF_SET[m_rng->GetInteger(0, CF_SET.size() - 1)];
        double tp = TP_SET[m_rng->GetInteger(0, TP_SET.size() - 1)];

        return std::make_tuple (sf, bw, cf, tp);
    }

    void UpdateRewards (int sf, double bw, double cf, double tp, bool success, double dataRate, double energyConsumption) override
    {
        if (success) m_successfulSelections++;
    }

private:
    Ptr<UniformRandomVariable> m_rng;
};

// D-LoRa wrapper algorithm
class DLoRaAlgorithm : public BaseAlgorithm
{
public:
    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::DLoRaAlgorithm")
            .SetParent<BaseAlgorithm> ()
            .AddConstructor<DLoRaAlgorithm> ()
        ;
        return tid;
    }

    DLoRaAlgorithm ()
        : m_agent (CreateObject<DLoRaAgent> ())
    {
    }

    void SetNodeAndGateway (Ptr<Node> node, Ptr<Node> gateway)
    {
        m_agent->SetNodeAndGateway (node, gateway);
    }

    std::tuple<int, double, double, double> SelectParameters (Ptr<Node> node, Ptr<Node> gateway) override
    {
        m_totalSelections++;
        return m_agent->SelectParameters ();
    }

    void UpdateRewards (int sf, double bw, double cf, double tp, bool success, double dataRate, double energyConsumption) override
    {
        if (success) m_successfulSelections++;
        m_agent->UpdateRewards (sf, bw, cf, tp, success, dataRate, energyConsumption);
    }

private:
    Ptr<DLoRaAgent> m_agent;
};

// Application to send packets and interact with algorithms
class LoRaEndDeviceApp : public Application
{
public:
    LoRaEndDeviceApp ()
        : m_packetsSent (0),
          m_packetsReceived (0),
          m_totalEnergy (0.0),
          m_totalData (0),
          m_packetSize (PAYLOAD_SIZE),
          m_sendEvent (EventId ()),
          m_interval (Seconds (4.0)),
          m_expRandomVariable (CreateObject<ExponentialRandomVariable> ()),
          m_fixedInterval (0.0),
          m_intervalSet (false)
    {
        m_expRandomVariable->SetAttribute ("Mean", DoubleValue (m_interval.GetSeconds ()));
    }

    void SetPacketInterval (double interval)
    {
        m_interval = Seconds (interval);
        m_expRandomVariable->SetAttribute ("Mean", DoubleValue (interval));
    }

    void SetPacketSize (uint32_t size)
    {
        m_packetSize = size;
    }

    static TypeId GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::LoRaEndDeviceApp")
            .SetParent<Application> ()
            .AddConstructor<LoRaEndDeviceApp> ()
            .AddAttribute ("Interval",
                           "The time interval between packets.",
                           TimeValue (Seconds (4.0)),
                           MakeTimeAccessor (&LoRaEndDeviceApp::m_interval),
                           MakeTimeChecker ())
            .AddAttribute ("PacketSize", "The size of packets.",
                           UintegerValue (PAYLOAD_SIZE),
                           MakeUintegerAccessor (&LoRaEndDeviceApp::m_packetSize),
                           MakeUintegerChecker<uint32_t> (0, std::numeric_limits<uint32_t>::max()));
        return tid;
    }

    void SetGatewayAndAlgorithm (Ptr<Node> gateway, Ptr<BaseAlgorithm> algorithm)
    {
        m_gateway = gateway;
        m_algorithm = algorithm;
    }

    void StartApplication ()
    {
        // Schedule first transmission with random delay to avoid synchronization
        Ptr<UniformRandomVariable> uniform = CreateObject<UniformRandomVariable>();
        double startDelay = uniform->GetValue(0.0, 1.0);
        m_sendEvent = Simulator::Schedule (Seconds (startDelay), &LoRaEndDeviceApp::SendPacket, this);
    }

    void StopApplication ()
    {
        Simulator::Cancel (m_sendEvent);
    }

    void SendPacket ()
    {
        g_totalPacketsSent++;
        m_packetsSent++;

        // Set interval for this device (once)
        if (!m_intervalSet)
        {
            m_fixedInterval = m_expRandomVariable->GetValue ();
            m_intervalSet = true;
            
            if (g_intervalFile.is_open())
            {
                g_intervalFile << GetNode()->GetId() << "," << m_fixedInterval << std::endl;
            }
        }

        // Select LoRa parameters using the algorithm
        int sf; double bw, cf, tp;
        std::tie (sf, bw, cf, tp) = m_algorithm->SelectParameters (GetNode (), m_gateway);

        // Create packet
        Ptr<Packet> packet = Create<Packet> (m_packetSize);

        // Calculate transmission metrics
        double rssi = CalculateRSSI (GetNode (), m_gateway, tp, GAMMA, DELTA1);
        double noise_power_dBm = -174.0 + 10 * std::log10(bw) + 6.0; // 6 dB NF
        double snr = rssi - noise_power_dBm;
        
        g_totalRSSI += rssi;
        g_totalSNR += snr;
        g_rssiMeasurements++;
        g_snrMeasurements++;

        // Check receiver sensitivity
        bool rssi_ok = (rssi >= RS_TABLE[sf][bw]);

        // Check SINR requirement
        bool sinr_ok = (snr >= SINR_REQ_TABLE[sf]);

        // Simple collision check (could be improved with global collision detection)
        bool collision_occurred = false;
        Ptr<UniformRandomVariable> uniform = CreateObject<UniformRandomVariable>();
        
        // Collision probability increases with network density
        double collision_prob = std::min(0.3, g_totalPacketsSent / 10000.0);
        collision_occurred = uniform->GetValue() < collision_prob;
        
        if (collision_occurred) g_collisions++;

        bool success = rssi_ok && sinr_ok && !collision_occurred;

        double toa = CalculateToA (sf, bw, m_packetSize);
        double energyConsumed = CalculateEnergyConsumption (tp, toa);

        g_totalEnergyConsumed += energyConsumed;
        m_totalEnergy += energyConsumed;
        g_totalTimeOnAir += toa;

        if (success)
        {
            g_totalPacketsReceived++;
            m_packetsReceived++;
            g_totalDataReceived += m_packetSize;
            m_totalData += m_packetSize;
        }

        // Log detailed information
        if (g_detailsFile.is_open())
        {
            g_detailsFile << GetNode()->GetId() << "," 
                         << Simulator::Now().GetSeconds() << ","
                         << sf << "," << bw << "," << cf << "," << tp << ","
                         << rssi << "," << snr << "," << success << ","
                         << energyConsumed << "," << toa << std::endl;
        }

        // Update algorithm with outcome
        double dataRate = success ? (m_packetSize * 8.0 / toa) : 0.0;
        m_algorithm->UpdateRewards (sf, bw, cf, tp, success, dataRate, energyConsumed);

        // Schedule next packet
        m_sendEvent = Simulator::Schedule (Seconds (m_fixedInterval), &LoRaEndDeviceApp::SendPacket, this);
    }

    // Getters for statistics
    uint32_t GetPacketsSent() { return m_packetsSent; }
    uint32_t GetPacketsReceived() { return m_packetsReceived; }
    double GetTotalEnergy() { return m_totalEnergy; }
    uint32_t GetTotalData() { return m_totalData; }

private:
    Ptr<Node> m_gateway;
    Ptr<BaseAlgorithm> m_algorithm;
    uint32_t m_packetsSent;
    uint32_t m_packetsReceived;
    double m_totalEnergy;
    uint32_t m_totalData;
    uint32_t m_packetSize;
    EventId m_sendEvent;
    Time m_interval;
    Ptr<ExponentialRandomVariable> m_expRandomVariable;
    double m_fixedInterval;
    bool m_intervalSet;
};

// Main simulation function
int main (int argc, char *argv[])
{
    // Default command line arguments
    uint32_t numNodes = 50;
    double simulationTime = 7200.0; // 2 hours
    double topologyRadius = 1000.0; // meters
    std::string algorithm = "DLoRa";
    double packetInterval = 4.0; // seconds
    uint32_t payloadSize = PAYLOAD_SIZE;
    uint32_t mobilityPercentage = 0;
    uint32_t spreadingFactor = 0; // 0 = adaptive, otherwise fixed SF
    bool enableDetailedLog = false;

    CommandLine cmd (__FILE__);
    cmd.AddValue ("numNodes", "Number of LoRa end devices", numNodes);
    cmd.AddValue ("simulationTime", "Total simulation time in seconds", simulationTime);
    cmd.AddValue ("topologyRadius", "Radius of the circular network topology in meters", topologyRadius);
    cmd.AddValue ("algorithm", "Algorithm to use (DLoRa, DLoRa-PDR, DLoRa-EE, DLoRa-TH, Random, RoundRobin, ADR, RSLoRa)", algorithm);
    cmd.AddValue ("packetInterval", "Average packet transmission interval in seconds", packetInterval);
    cmd.AddValue ("payloadSize", "Payload size in bytes", payloadSize);
    cmd.AddValue ("mobilityPercentage", "Percentage of mobile nodes (0-100)", mobilityPercentage);
    cmd.AddValue ("spreadingFactor", "Fixed spreading factor (0 for adaptive)", spreadingFactor);
    cmd.AddValue ("enableDetailedLog", "Enable detailed per-packet logging", enableDetailedLog);
    cmd.Parse (argc, argv);

    // Set up logging
    LogComponentEnable ("DLoRaSimulation", LOG_LEVEL_INFO);

    // Determine scenario name based on parameters
    std::string scenario = "GeneralTest";
    std::string variableParameter = "NumNodes";
    std::string parameterValue = std::to_string(numNodes);
    
    // Auto-detect scenario based on patterns
    if (topologyRadius == 1129) {
        scenario = "Scenario5_NetworkDensity";
        variableParameter = "NetworkDensity";
        parameterValue = std::to_string(numNodes);
    } else if (spreadingFactor > 0) {
        scenario = "Scenario2_SF";
        variableParameter = "SpreadingFactor";
        parameterValue = std::to_string(spreadingFactor);
    } else if (mobilityPercentage > 0) {
        scenario = "Scenario4_Mobility";
        variableParameter = "MobilityPercentage";
        parameterValue = std::to_string(mobilityPercentage);
    } else if (packetInterval != 4.0) {
        scenario = "Scenario3_Intervals";
        variableParameter = "PacketInterval";
        parameterValue = std::to_string(packetInterval);
    } else if (numNodes >= 50) {
        scenario = "Scenario1_Density";
        variableParameter = "NumDevices";
        parameterValue = std::to_string(numNodes);
    }
    
    // Create unified CSV output filename
    std::string prefix = algorithm + "_" + std::to_string(numNodes) + "nodes";
    std::string csvFileName = "simulation_results_" + prefix + ".csv";
    
    // Open unified CSV file with standardized header
    std::ofstream csvFile(csvFileName);
    if (csvFile.is_open())
    {
        csvFile << "Scenario,NumDevices,Algorithm,Packet_Index,Succeed,Lost,Success_Rate,PayloadSize,PacketInterval,MobilityPercentage,SpreadingFactor,SimulationDuration,PDR,EnergyEfficiency,AverageToA,AverageSNR,AverageRSSI,TotalEnergyConsumption,VariableParameter,ParameterValue" << std::endl;
    }

    // Set D-LoRa variant parameters
    if (algorithm == "DLoRa")
    {
        g_xi = 0; g_zeta = 0; g_eta = 1.8;
    }
    else if (algorithm == "DLoRa-PDR")
    {
        g_xi = 0; g_zeta = 0; g_eta = 0;
    }
    else if (algorithm == "DLoRa-EE")
    {
        g_xi = 0; g_zeta = 0; g_eta = 3.5;
    }
    else if (algorithm == "DLoRa-TH")
    {
        g_xi = 10; g_zeta = 10; g_eta = 0;
    }

    // Override SF_SET if fixed spreading factor is specified
    if (spreadingFactor > 0)
    {
        SF_SET = {(int)spreadingFactor};
    }

    // Create nodes
    NodeContainer endDevices;
    endDevices.Create (numNodes);
    NodeContainer gateways;
    gateways.Create (1);

    // Install mobility model
    Ptr<UniformDiscPositionAllocator> positionAlloc = CreateObject<UniformDiscPositionAllocator> ();
    positionAlloc->SetX (0.0);
    positionAlloc->SetY (0.0);
    positionAlloc->SetRho (topologyRadius);

    MobilityHelper mobility;
    mobility.SetPositionAllocator (positionAlloc);
    
    // Configure mobility for mobile nodes
    uint32_t numMobileNodes = (numNodes * mobilityPercentage) / 100;
    
    if (numMobileNodes > 0)
    {
        // Mobile nodes
        NodeContainer mobileNodes;
        for (uint32_t i = 0; i < numMobileNodes; ++i)
        {
            mobileNodes.Add (endDevices.Get (i));
        }
        
        mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                                  "Bounds", RectangleValue (Rectangle (-topologyRadius, topologyRadius, -topologyRadius, topologyRadius)),
                                  "Speed", StringValue ("ns3::UniformRandomVariable[Min=1.39|Max=8.33]"), // 5-30 km/h
                                  "Distance", DoubleValue (100.0));
        mobility.Install (mobileNodes);
        
        // Static nodes
        if (numMobileNodes < numNodes)
        {
            NodeContainer staticNodes;
            for (uint32_t i = numMobileNodes; i < numNodes; ++i)
            {
                staticNodes.Add (endDevices.Get (i));
            }
            
            mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
            mobility.Install (staticNodes);
        }
    }
    else
    {
        // All nodes are static
        mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
        mobility.Install (endDevices);
    }
    
    // Gateway is always static at center
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (gateways);
    gateways.Get (0)->GetObject<MobilityModel> ()->SetPosition (Vector (0.0, 0.0, 0.0));

    // Create applications with selected algorithm
    ApplicationContainer apps;
    std::vector<Ptr<BaseAlgorithm>> algorithmInstances;
    
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        Ptr<BaseAlgorithm> selectedAlgorithm;

        if (algorithm == "DLoRa" || algorithm == "DLoRa-PDR" || algorithm == "DLoRa-EE" || algorithm == "DLoRa-TH")
        {
            Ptr<DLoRaAlgorithm> dloraAlg = CreateObject<DLoRaAlgorithm> ();
            dloraAlg->SetNodeAndGateway (endDevices.Get (i), gateways.Get (0));
            selectedAlgorithm = dloraAlg;
        }
        else if (algorithm == "Random")
        {
            selectedAlgorithm = CreateObject<RandomAlgorithm> ();
        }
        else if (algorithm == "RoundRobin")
        {
            selectedAlgorithm = CreateObject<RoundRobinAlgorithm> ();
        }
        else if (algorithm == "ADR")
        {
            selectedAlgorithm = CreateObject<ADRAlgorithm> ();
        }
        else if (algorithm == "RSLoRa")
        {
            selectedAlgorithm = CreateObject<RSLoRaAlgorithm> ();
        }
        else
        {
            NS_FATAL_ERROR ("Unknown algorithm: " << algorithm);
        }

        algorithmInstances.push_back(selectedAlgorithm);

        Ptr<LoRaEndDeviceApp> app = CreateObject<LoRaEndDeviceApp> ();
        app->SetGatewayAndAlgorithm (gateways.Get (0), selectedAlgorithm);
        app->SetPacketInterval (packetInterval);
        app->SetPacketSize (payloadSize);
        endDevices.Get (i)->AddApplication (app);
        app->SetStartTime (Seconds (0.0));
        app->SetStopTime (Seconds (simulationTime));
        apps.Add (app);
    }

    // Run simulation
    NS_LOG_INFO("Starting simulation with " << numNodes << " nodes, algorithm: " << algorithm);
    
    Simulator::Stop (Seconds (simulationTime));
    Simulator::Run ();
    Simulator::Destroy ();

    g_totalSimulationTime = simulationTime;

    // Calculate and print metrics
    double pdr = (g_totalPacketsSent > 0) ? (g_totalPacketsReceived / g_totalPacketsSent) * 100.0 : 0.0;
    double ee = (g_totalEnergyConsumed > 0) ? (g_totalDataReceived * 8.0 / g_totalEnergyConsumed) : 0.0;
    double th = (g_totalTimeOnAir > 0) ? (g_totalDataReceived * 8.0 / g_totalTimeOnAir) : 0.0;
    double avgToA = (g_totalPacketsSent > 0) ? (g_totalTimeOnAir / g_totalPacketsSent) * 1000.0 : 0.0;
    double avgRSSI = (g_rssiMeasurements > 0) ? (g_totalRSSI / g_rssiMeasurements) : 0.0;
    double avgSNR = (g_snrMeasurements > 0) ? (g_totalSNR / g_snrMeasurements) : 0.0;
    double collisionRate = (g_totalPacketsSent > 0) ? (g_collisions / g_totalPacketsSent) * 100.0 : 0.0;

    // Print results
    std::cout << "Simulation Results for " << algorithm << " (Radius: " << (int)topologyRadius << "m)" << std::endl;
    std::cout << "PDR: " << std::fixed << std::setprecision(2) << pdr << " %" << std::endl;
    std::cout << "EE: " << std::fixed << std::setprecision(2) << ee << " bits/mJ" << std::endl;
    std::cout << "TH: " << std::fixed << std::setprecision(2) << th << " bps" << std::endl;
    std::cout << "AvgToA: " << std::fixed << std::setprecision(2) << avgToA << " ms" << std::endl;
    std::cout << "AvgRSSI: " << std::fixed << std::setprecision(2) << avgRSSI << " dBm" << std::endl;
    std::cout << "AvgSNR: " << std::fixed << std::setprecision(2) << avgSNR << " dB" << std::endl;
    std::cout << "CollisionRate: " << std::fixed << std::setprecision(2) << collisionRate << " %" << std::endl;
    
    // Additional statistics
    std::cout << "TotalPacketsSent: " << (int)g_totalPacketsSent << std::endl;
    std::cout << "TotalPacketsReceived: " << (int)g_totalPacketsReceived << std::endl;
    std::cout << "TotalEnergyConsumed: " << std::fixed << std::setprecision(3) << g_totalEnergyConsumed << " mJ" << std::endl;

    // Write results to CSV file
    if (csvFile.is_open())
    {
        csvFile << scenario << ","
                << numNodes << ","
                << algorithm << ","
                << (int)g_totalPacketsSent << ","
                << (int)g_totalPacketsReceived << ","
                << (int)(g_totalPacketsSent - g_totalPacketsReceived) << ","
                << std::fixed << std::setprecision(2) << pdr << ","
                << payloadSize << ","
                << packetInterval << ","
                << mobilityPercentage << ","
                << (spreadingFactor > 0 ? spreadingFactor : 0) << ","
                << simulationTime << ","
                << pdr << ","
                << ee << ","
                << avgToA << ","
                << avgSNR << ","
                << avgRSSI << ","
                << g_totalEnergyConsumed << ","
                << variableParameter << ","
                << parameterValue << std::endl;
        csvFile.close();
    }

    // Close detailed log files if they were opened
    if (enableDetailedLog)
    {
        if (g_intervalFile.is_open())
        {
            g_intervalFile.close();
        }
        
        if (g_detailsFile.is_open())
        {
            g_detailsFile.close();
        }
    }

    return 0;
}