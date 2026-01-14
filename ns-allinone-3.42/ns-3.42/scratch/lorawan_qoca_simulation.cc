#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lorawan-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include <algorithm>
#include <random>
#include <fstream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <memory>

using namespace ns3;
using namespace lorawan;

NS_LOG_COMPONENT_DEFINE("LoRaWANQoCSimulation");

// Classe pour implémenter les algorithmes QoC-A et DQoC-A exactement selon l'article
class BanditAlgorithm
{
public:
    enum AlgorithmType {
        UNIFORM,    // Allocation uniforme (Round-Robin)
        UCB,        // Upper Confidence Bounds
        QOC_A,      // Quality of Channel Allocation
        DQOC_A      // Discounted QoC-A
    };

protected:
    uint32_t m_K;  // Number of channels (K=8)
    uint32_t m_n;  // Packet index
    std::vector<uint32_t> m_T_i;  // T_i(n): times channel i selected
    std::vector<double> m_R_i;    // R_i(n): empirical mean of rewards
    std::vector<double> m_G_i;    // G_i(n): empirical mean of quality
    std::vector<std::vector<double>> m_rewards;   // All rewards for each channel
    std::vector<std::vector<double>> m_qualities; // All qualities for each channel
    std::vector<uint32_t> m_channelHistory;       // History of selected channels
    double m_alpha;    // Exploration factor
    double m_beta;     // Quality weight factor
    double m_lambda;   // Discount factor for rewards
    double m_lambdaG;  // Discount factor for quality
    AlgorithmType m_type;
    uint32_t m_currentChannel; // Pour Round-Robin

public:
    BanditAlgorithm(uint32_t K, AlgorithmType type, 
                   double alpha = 0.6, double beta = 0.2, 
                   double lambda = 0.98, double lambdaG = 0.90)
        : m_K(K), m_n(0), m_alpha(alpha), m_beta(beta), 
          m_lambda(lambda), m_lambdaG(lambdaG), m_type(type),
          m_currentChannel(0)
    {
        m_T_i.resize(K, 0);
        m_R_i.resize(K, 0.0);
        m_G_i.resize(K, 0.0);
        m_rewards.resize(K);
        m_qualities.resize(K);
    }

    uint32_t SelectChannel()
    {
        m_n++;
        
        switch(m_type)
        {
            case UNIFORM:
                return SelectChannelUniform();
            case UCB:
                return SelectChannelUCB();
            case QOC_A:
                return SelectChannelQoCA();
            case DQOC_A:
                return SelectChannelDQoCA();
            default:
                return 0;
        }
    }

    void UpdateReward(uint32_t channel, double reward, double quality)
    {
        // Update statistics
        m_T_i[channel]++;
        m_rewards[channel].push_back(reward);
        m_qualities[channel].push_back(quality);
        m_channelHistory.push_back(channel);
        
        // Update empirical means
        UpdateEmpiricalMeans(channel);
    }

    void Reset()
    {
        m_n = 0;
        m_currentChannel = 0;
        std::fill(m_T_i.begin(), m_T_i.end(), 0);
        std::fill(m_R_i.begin(), m_R_i.end(), 0.0);
        std::fill(m_G_i.begin(), m_G_i.end(), 0.0);
        for(auto& rewards : m_rewards) rewards.clear();
        for(auto& qualities : m_qualities) qualities.clear();
        m_channelHistory.clear();
    }

private:
    uint32_t SelectChannelUniform()
    {
        uint32_t channel = m_currentChannel;
        m_currentChannel = (m_currentChannel + 1) % m_K;
        return channel;
    }

    uint32_t SelectChannelUCB()
    {
        // Algorithm 1: UCB policy as depicted in [23]
        if(m_n <= m_K)
        {
            // Initialize policy by trying each channel for at least one time
            return (m_n - 1);
        }

        double maxUCB = -std::numeric_limits<double>::infinity();
        uint32_t bestChannel = 0;

        for(uint32_t i = 0; i < m_K; i++)
        {
            if(m_T_i[i] == 0)
            {
                return i;  // Never tried channel
            }

            // B_i(n) = R_i(n) + α * sqrt(ln(n) / T_i(n))
            double B_i = m_R_i[i] + m_alpha * sqrt(log(m_n) / m_T_i[i]);

            if(B_i > maxUCB)
            {
                maxUCB = B_i;
                bestChannel = i;
            }
        }

        return bestChannel;
    }

    uint32_t SelectChannelQoCA()
    {
        // Algorithm 2: QoC-A policy
        if(m_n <= m_K)
        {
            // Initialize policy by trying each channel for at least one time
            return (m_n - 1);
        }

        double maxScore = -std::numeric_limits<double>::infinity();
        uint32_t bestChannel = 0;

        // Calculate G_max(n)
        double G_max = CalculateGmax();

        for(uint32_t i = 0; i < m_K; i++)
        {
            if(m_T_i[i] == 0)
            {
                return i;
            }

            // Q_i(n) = β * (G_i(n)/G_max(n) - 1) * ln(n)/T_i(n)
            double Q_i = 0.0;
            if(G_max > 0.0)
            {
                Q_i = m_beta * (m_G_i[i] / G_max - 1.0) * log(m_n) / m_T_i[i];
            }

            // B_i(n) = R_i(n) + Q_i(n) + α * sqrt(ln(n) / T_i(n))
            double B_i = m_R_i[i] + Q_i + m_alpha * sqrt(log(m_n) / m_T_i[i]);

            if(B_i > maxScore)
            {
                maxScore = B_i;
                bestChannel = i;
            }
        }

        return bestChannel;
    }

    uint32_t SelectChannelDQoCA()
    {
        // Algorithm 3: DQoC-A policy
        if(m_n <= m_K)
        {
            // Initialize policy by trying each channel for at least one time
            return (m_n - 1);
        }

        double maxScore = -std::numeric_limits<double>::infinity();
        uint32_t bestChannel = 0;

        // Calculate discounted counts N_i(n) and total discounted time W(n)
        std::vector<double> N_i(m_K, 0.0);
        std::vector<double> N_g_i(m_K, 0.0);
        double W_n = 0.0;

        // Simplification: utiliser les dernières observations avec pondération
        for(uint32_t i = 0; i < m_K; i++)
        {
            double sum_discount_r = 0.0;
            double sum_discount_g = 0.0;
            
            for(size_t j = 0; j < m_channelHistory.size(); j++)
            {
                if(m_channelHistory[j] == i)
                {
                    double discount_r = pow(m_lambda, m_channelHistory.size() - 1 - j);
                    double discount_g = pow(m_lambdaG, m_channelHistory.size() - 1 - j);
                    N_i[i] += discount_r;
                    N_g_i[i] += discount_g;
                }
            }
            W_n += N_i[i];
        }

        // Calculate discounted means
        std::vector<double> R_i_disc(m_K, 0.0);
        std::vector<double> G_i_disc(m_K, 0.0);
        
        for(uint32_t i = 0; i < m_K; i++)
        {
            if(N_i[i] > 0.0 && m_rewards[i].size() > 0)
            {
                double sum_rewards = 0.0;
                double sum_qualities = 0.0;
                size_t reward_idx = 0;
                
                for(size_t j = 0; j < m_channelHistory.size(); j++)
                {
                    if(m_channelHistory[j] == i && reward_idx < m_rewards[i].size())
                    {
                        double discount_r = pow(m_lambda, m_channelHistory.size() - 1 - j);
                        double discount_g = pow(m_lambdaG, m_channelHistory.size() - 1 - j);
                        sum_rewards += discount_r * m_rewards[i][reward_idx];
                        sum_qualities += discount_g * m_qualities[i][reward_idx];
                        reward_idx++;
                    }
                }
                
                R_i_disc[i] = sum_rewards / N_i[i];
                if(N_g_i[i] > 0.0)
                {
                    G_i_disc[i] = sum_qualities / N_g_i[i];
                }
            }
        }

        // Find G_max(n)
        double G_max_disc = 0.0;
        for(uint32_t i = 0; i < m_K; i++)
        {
            if(G_i_disc[i] > G_max_disc)
            {
                G_max_disc = G_i_disc[i];
            }
        }

        for(uint32_t i = 0; i < m_K; i++)
        {
            if(N_i[i] == 0.0)
            {
                return i;
            }

            // Q_i(n) = β * (G_i(n)/G_max(n) - 1) * ln(W(n))/N_i(n)
            double Q_i = 0.0;
            if(G_max_disc > 0.0)
            {
                Q_i = m_beta * (G_i_disc[i] / G_max_disc - 1.0) * log(W_n) / N_i[i];
            }

            // B_i(n) = R_i(n) + Q_i(n) + α * sqrt(ln(W(n)) / N_i(n))
            double B_i = R_i_disc[i] + Q_i + m_alpha * sqrt(log(W_n) / N_i[i]);

            if(B_i > maxScore)
            {
                maxScore = B_i;
                bestChannel = i;
            }
        }

        return bestChannel;
    }

    void UpdateEmpiricalMeans(uint32_t channel)
    {
        // R_i(n) = (1/T_i(n)) * sum(r_i(m) * 1_{A(m)=i})
        double sum_rewards = 0.0;
        for(double reward : m_rewards[channel])
        {
            sum_rewards += reward;
        }
        m_R_i[channel] = (m_T_i[channel] > 0) ? sum_rewards / m_T_i[channel] : 0.0;

        // G_i(n) = (1/T_i(n)) * sum(g_i(k))
        double sum_qualities = 0.0;
        for(double quality : m_qualities[channel])
        {
            sum_qualities += quality;
        }
        m_G_i[channel] = (m_T_i[channel] > 0) ? sum_qualities / m_T_i[channel] : 0.0;
    }

    double CalculateGmax()
    {
        double maxQuality = 0.0;
        for(uint32_t i = 0; i < m_K; i++)
        {
            if(m_G_i[i] > maxQuality)
            {
                maxQuality = m_G_i[i];
            }
        }
        return maxQuality;
    }

public:
    // Getters pour les statistiques
    uint32_t GetTimesSelected(uint32_t channel) { return m_T_i[channel]; }
    double GetMeanReward(uint32_t channel) { return m_R_i[channel]; }
    uint32_t GetPacketIndex() { return m_n; }
    uint32_t GetSuccessfulTransmissions() 
    { 
        uint32_t total = 0;
        for(uint32_t i = 0; i < m_K; i++)
        {
            for(double reward : m_rewards[i])
            {
                total += (uint32_t)reward;
            }
        }
        return total;
    }
    uint32_t GetLostPackets() { return m_n - GetSuccessfulTransmissions(); }
    std::string GetTypeName() 
    {
        switch(m_type) {
            case UNIFORM: return "Uniform";
            case UCB: return "UCB";
            case QOC_A: return "QoC-A";
            case DQOC_A: return "DQoC-A";
            default: return "Unknown";
        }
    }
};

// Classe pour simuler exactement les conditions de l'article avec support du Spreading Factor
class ChannelConditionModel
{
private:
    std::vector<double> m_channelESP;  // ESP values for each channel in dBm
    uint32_t m_K;  // Number of channels (8)
    uint8_t m_spreadingFactor;  // SF parameter
    bool m_isStationary;
    uint32_t m_currentLocation;
    double m_mobilityPercentage;  // NEW: Mobility percentage parameter
    std::mt19937 m_rng;
    std::normal_distribution<double> m_shadowingDist;

    // Channel frequencies as per Table IV: {867.1, 867.3, 867.5, 867.7, 867.9, 868.1, 868.3, 868.5} MHz
    std::vector<double> m_frequencies = {867.1, 867.3, 867.5, 867.7, 867.9, 868.1, 868.3, 868.5};

public:
    ChannelConditionModel(uint32_t K, uint8_t sf = 7, bool stationary = true, 
                         double mobilityPercentage = 0.0, uint32_t seed = 12345)
        : m_K(K), m_spreadingFactor(sf), m_isStationary(stationary), m_currentLocation(0), 
          m_mobilityPercentage(mobilityPercentage), m_rng(seed), 
          m_shadowingDist(0.0, 1.5 + mobilityPercentage * 0.05)  // Mobilité augmente la variance du shadowing
    {
        InitializeChannels();
    }

    void InitializeChannels()
    {
        m_channelESP.resize(m_K);

        if(m_isStationary)
        {
            // Scenario 1: Stationary - Based on Figure 4 of the paper
            // Deep fade at 867.3 MHz (~10 dB depth), other channels vary
            std::vector<double> stationaryESP = {
                -118.0,  // 867.1 MHz
                -124.0,  // 867.3 MHz (deep fade)
                -116.0,  // 867.5 MHz
                -115.0,  // 867.7 MHz
                -114.0,  // 867.9 MHz (best)
                -116.0,  // 868.1 MHz
                -115.0,  // 868.3 MHz
                -117.0   // 868.5 MHz
            };
            
            // Ajuster selon le Spreading Factor
            for(size_t i = 0; i < stationaryESP.size(); i++)
            {
                // SF plus élevé = meilleure sensibilité mais ToA plus long
                double sfBonus = (m_spreadingFactor - 7) * 2.5; // 2.5 dB par SF
                m_channelESP[i] = stationaryESP[i] + sfBonus;
            }
        }
        else
        {
            // Scenario 2: Non-stationary - starts with location 0
            UpdateNonStationaryChannels();
        }
    }

    void UpdateNonStationaryChannels()
    {
        // Based on Figure 6 of the paper - 3 different locations
        if(m_currentLocation == 0)  // Location 1
        {
            std::vector<double> loc1ESP = {
                -118.0,  // 867.1 MHz (fade)
                -116.0,  // 867.3 MHz
                -115.0,  // 867.5 MHz
                -114.0,  // 867.7 MHz
                -112.0,  // 867.9 MHz (best)
                -115.0,  // 868.1 MHz
                -114.0,  // 868.3 MHz
                -118.0   // 868.5 MHz (fade)
            };
            for(size_t i = 0; i < loc1ESP.size(); i++)
            {
                double sfBonus = (m_spreadingFactor - 7) * 2.5;
                m_channelESP[i] = loc1ESP[i] + sfBonus;
            }
        }
        else if(m_currentLocation == 1)  // Location 2
        {
            std::vector<double> loc2ESP = {
                -119.0,  // 867.1 MHz
                -120.0,  // 867.3 MHz
                -118.0,  // 867.5 MHz
                -117.0,  // 867.7 MHz
                -119.0,  // 867.9 MHz (degraded)
                -114.0,  // 868.1 MHz (better)
                -113.0,  // 868.3 MHz (best)
                -115.0   // 868.5 MHz (good)
            };
            for(size_t i = 0; i < loc2ESP.size(); i++)
            {
                double sfBonus = (m_spreadingFactor - 7) * 2.5;
                m_channelESP[i] = loc2ESP[i] + sfBonus;
            }
        }
        else  // Location 3
        {
            std::vector<double> loc3ESP = {
                -122.0,  // 867.1 MHz
                -123.0,  // 867.3 MHz
                -121.0,  // 867.5 MHz
                -120.0,  // 867.7 MHz
                -122.0,  // 867.9 MHz
                -117.0,  // 868.1 MHz
                -116.0,  // 868.3 MHz (best)
                -118.0   // 868.5 MHz
            };
            for(size_t i = 0; i < loc3ESP.size(); i++)
            {
                double sfBonus = (m_spreadingFactor - 7) * 2.5;
                m_channelESP[i] = loc3ESP[i] + sfBonus;
            }
        }
    }

    void ChangeLocation(uint32_t newLocation)
    {
        if(!m_isStationary && newLocation != m_currentLocation && newLocation < 3)
        {
            m_currentLocation = newLocation;
            UpdateNonStationaryChannels();
        }
    }

    // Calculate ESP as per Equation (1): ESP = RSSI + SNR - 10*log10(1 + 10^(SNR/10))
    double CalculateESP(double rssi_dBm, double snr_dB)
    {
        return rssi_dBm + snr_dB - 10.0 * log10(1.0 + pow(10.0, snr_dB / 10.0));
    }

    // Get channel quality (ESP in linear scale for the algorithm)
    double GetChannelQuality(uint32_t channel)
    {
        if(channel >= m_K) return 0.0;
        
        // Add shadowing variation
        double esp_dBm = m_channelESP[channel] + m_shadowingDist(m_rng);
        
        // Convert to linear scale (mW) as stated in the paper
        double esp_linear = pow(10.0, esp_dBm / 10.0);
        
        return esp_linear;
    }

    // Simulate transmission success based on ESP and channel conditions with SF impact AND mobility
    bool IsTransmissionSuccessful(uint32_t channel)
    {
        if(channel >= m_K) return false;
        
        // Get ESP with shadowing (now affected by mobility)
        double mobilityFading = m_mobilityPercentage * 0.1; // 0.1 dB per percent mobility
        double esp_dBm = m_channelESP[channel] + m_shadowingDist(m_rng) - mobilityFading;
        
        // Modèle de succès calibré avec impact du SF
        double threshold = -120.0 - (m_spreadingFactor - 7) * 2.5;  // SF améliore la sensibilité
        double successProb = 1.0 / (1.0 + exp(-(esp_dBm - threshold) / 2.5));
        
        // Ajustement avec impact de la mobilité
        if(m_isStationary)
        {
            // Mobilité réduit les performances même en stationnaire
            double mobilityPenalty = m_mobilityPercentage * 0.002; // 0.2% de pénalité par % de mobilité
            successProb = (0.4 + 0.6 * successProb) * (1.0 - mobilityPenalty);
        }
        else
        {
            // En non-stationnaire, l'impact de la mobilité est plus fort
            double mobilityPenalty = m_mobilityPercentage * 0.003; // 0.3% de pénalité par % de mobilité
            successProb = (0.2 + 0.8 * successProb) * (1.0 - mobilityPenalty);
        }
        
        // S'assurer que la probabilité reste dans [0, 1]
        successProb = std::max(0.0, std::min(1.0, successProb));
        
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        return uniform(m_rng) < successProb;
    }

    // Getters
    uint32_t GetCurrentLocation() { return m_currentLocation; }
    double GetChannelESP_dBm(uint32_t channel) 
    { 
        return (channel < m_K) ? m_channelESP[channel] : -999.0; 
    }
    double GetFrequency(uint32_t channel) 
    { 
        return (channel < m_K) ? m_frequencies[channel] : 0.0; 
    }
    uint8_t GetSpreadingFactor() { return m_spreadingFactor; }
    void SetSpreadingFactor(uint8_t sf) { 
        m_spreadingFactor = sf; 
        InitializeChannels(); // Recalculer les ESP avec le nouveau SF
    }
};

// Classe principale de simulation qui reproduit exactement les conditions de l'article
class LoRaWANQoCSimulation
{
private:
    uint32_t m_K;                    // Number of channels (8)
    uint32_t m_totalPackets;         // Total packets per algorithm
    uint32_t m_packetsPerLocation;   // Packets per location (non-stationary)
    bool m_isStationary;
    
    // Scenario parameters - updated for 5 scenarios
    uint32_t m_numDevices;
    uint32_t m_payloadSize;
    double m_packetInterval;
    double m_mobilityPercentage;
    uint8_t m_spreadingFactor;       // NEW: Spreading Factor parameter
    
    std::unique_ptr<BanditAlgorithm> m_uniformAlg;
    std::unique_ptr<BanditAlgorithm> m_ucbAlg;
    std::unique_ptr<BanditAlgorithm> m_qocaAlg;
    std::unique_ptr<BanditAlgorithm> m_dqocaAlg;
    
    std::unique_ptr<ChannelConditionModel> m_channelModel;
    
    // Results tracking
    struct SimulationResults {
        std::vector<double> successRates;  // Success rate at each step
        std::vector<uint32_t> cumulativeLost; // Cumulative lost packets
        uint32_t finalSuccessful;
        uint32_t finalLost;
        double finalSuccessRate;
        std::string algName;
        
        // Métriques détaillées
        double pdr;                    // Packet Delivery Ratio
        double energyEfficiency;       // Efficacité énergétique (packets/Joule)
        double averageToA;             // Time on Air moyen (ms)
        double averageSNR;             // SNR moyen (dB)
        double averageRSSI;            // RSSI moyen (dBm)
        double totalEnergyConsumption; // Consommation énergétique totale (J)
    };
    
    std::vector<SimulationResults> m_results; // Results for each algorithm
    std::vector<BanditAlgorithm*> m_activeAlgorithms; // Algorithmes actifs selon le scénario
    std::vector<std::string> m_activeAlgNames;

public:
    LoRaWANQoCSimulation(bool stationary = true, uint32_t numDevices = 100,
                        uint32_t payloadSize = 50, double packetInterval = 15.0,
                        double mobilityPercentage = 0.0, uint8_t spreadingFactor = 7,
                        uint32_t numPacketsPerDevice = 110)
        : m_K(8), m_isStationary(stationary), m_numDevices(numDevices),
          m_payloadSize(payloadSize), m_packetInterval(packetInterval),
          m_mobilityPercentage(mobilityPercentage), m_spreadingFactor(spreadingFactor)  // K=8 channels as per Table IV
    {
        // Nombre total de paquets = nombre de dispositifs × paquets par dispositif
        m_totalPackets = (uint32_t)(numDevices * numPacketsPerDevice);

        // Minimum de paquets pour garantir des résultats significatifs
        if(m_totalPackets < 100) {
            m_totalPackets = 100;
        }
        
        if(stationary)
        {
            m_packetsPerLocation = m_totalPackets;
        }
        else
        {
            m_packetsPerLocation = m_totalPackets / 3;  // 3 locations for non-stationary
        }
        
        // Initialize algorithms with exact parameters from the paper
        m_uniformAlg = std::make_unique<BanditAlgorithm>(m_K, BanditAlgorithm::UNIFORM);
        m_ucbAlg = std::make_unique<BanditAlgorithm>(m_K, BanditAlgorithm::UCB, 1.28);  // α = 1.28
        m_qocaAlg = std::make_unique<BanditAlgorithm>(m_K, BanditAlgorithm::QOC_A, 1.9, 0.9);  // α = 1.9, β = 0.9
        m_dqocaAlg = std::make_unique<BanditAlgorithm>(m_K, BanditAlgorithm::DQOC_A, 0.6, 0.2, 0.98, 0.90);  // α = 0.6, β = 0.2, λ = 0.98, λg = 0.90
        
        m_channelModel = std::make_unique<ChannelConditionModel>(m_K, m_spreadingFactor, stationary, m_mobilityPercentage, 12345); // Passer la mobilité
        
        // Sélection des algorithmes selon le scénario
        if(m_isStationary)
        {
            // Scénario stationnaire: Uniform, UCB, QoC-A seulement (conforme à l'article)
            m_activeAlgorithms = {m_uniformAlg.get(), m_ucbAlg.get(), m_qocaAlg.get()};
            m_activeAlgNames = {"Uniform", "UCB", "QoC-A"};
        }
        else
        {
            // Scénario non-stationnaire: Tous les algorithmes
            m_activeAlgorithms = {m_uniformAlg.get(), m_ucbAlg.get(), m_qocaAlg.get(), m_dqocaAlg.get()};
            m_activeAlgNames = {"Uniform", "UCB", "QoC-A", "DQoC-A"};
        }
        
        // Initialize results
        m_results.resize(m_activeAlgorithms.size());
        for(size_t i = 0; i < m_activeAlgorithms.size(); i++)
        {
            m_results[i].successRates.resize(m_totalPackets + 1, 0.0);
            m_results[i].cumulativeLost.resize(m_totalPackets + 1, 0);
            m_results[i].algName = m_activeAlgNames[i];
        }
    }

    void RunSimulation()
    {
        NS_LOG_INFO("Running simulation for " << (m_isStationary ? "Stationary" : "Non-Stationary") 
                   << " scenario with " << m_totalPackets << " packets...");
        NS_LOG_INFO("SF: " << (int)m_spreadingFactor << ", Devices: " << m_numDevices 
                   << ", Payload: " << m_payloadSize << "B, Interval: " << m_packetInterval << "min");
        NS_LOG_INFO("Testing " << m_activeAlgorithms.size() << " algorithms: ");
        for(const auto& name : m_activeAlgNames)
        {
            NS_LOG_INFO("  - " << name);
        }

        // Teste chaque algorithme indépendamment
        for(size_t algIndex = 0; algIndex < m_activeAlgorithms.size(); algIndex++)
        {
            // Reset algorithm state
            m_activeAlgorithms[algIndex]->Reset();
            
            // Reset channel model with same seed for fair comparison
            m_channelModel = std::make_unique<ChannelConditionModel>(m_K, m_spreadingFactor, m_isStationary, m_mobilityPercentage, 12345);
            
            uint32_t currentLocationIndex = 0;
            uint32_t successCount = 0;
            
            for(uint32_t packet = 0; packet < m_totalPackets; packet++)
            {
                // Change location for non-stationary scenario
                if(!m_isStationary && (packet % m_packetsPerLocation == 0) && packet != 0)
                {
                    currentLocationIndex = (currentLocationIndex + 1) % 3;
                    m_channelModel->ChangeLocation(currentLocationIndex);
                    NS_LOG_INFO("Algorithm " << m_activeAlgNames[algIndex] 
                               << " changed location to " << currentLocationIndex 
                               << " at packet " << packet);
                }

                // Select channel and simulate transmission
                uint32_t selectedChannel = m_activeAlgorithms[algIndex]->SelectChannel();
                double channelQuality = m_channelModel->GetChannelQuality(selectedChannel);
                bool success = m_channelModel->IsTransmissionSuccessful(selectedChannel);
                
                double reward = success ? 1.0 : 0.0;
                m_activeAlgorithms[algIndex]->UpdateReward(selectedChannel, reward, channelQuality);

                // Update statistics
                if(success) successCount++;
                uint32_t lostCount = packet + 1 - successCount;
                
                m_results[algIndex].successRates[packet + 1] = (double)successCount / (packet + 1);
                m_results[algIndex].cumulativeLost[packet + 1] = lostCount;
            }
            
            // Final statistics
            m_results[algIndex].finalSuccessful = successCount;
            m_results[algIndex].finalLost = m_totalPackets - successCount;
            m_results[algIndex].finalSuccessRate = (double)successCount / m_totalPackets;
            
            NS_LOG_INFO("Algorithm " << m_activeAlgNames[algIndex] << ": " 
                       << successCount << " successful, " 
                       << (m_totalPackets - successCount) << " lost, "
                       << (m_results[algIndex].finalSuccessRate * 100.0) << "% success rate");
        }

        NS_LOG_INFO("Simulation finished.");
    }

    void SaveResultsToCsv(const std::string& rewardFilename, const std::string& regretFilename)
    {
        // Créer le dossier scratch/qoc-a s'il n'existe pas
        system("mkdir -p scratch/qoc-a");
        
        std::string fullRewardPath = "scratch/qoc-a/" + rewardFilename;
        std::string fullRegretPath = "scratch/qoc-a/" + regretFilename;
        
        std::ofstream rewardFile(fullRewardPath);
        std::ofstream regretFile(fullRegretPath);

        // Extraire le numéro de scénario du nom de fichier
        uint32_t numScenario = ExtractScenarioNumber(rewardFilename);

        // Header avec le bon nombre de colonnes incluant NumScenario
        rewardFile << "NumScenario,Step";
        regretFile << "NumScenario,Step";
        for(const auto& name : m_activeAlgNames)
        {
            rewardFile << "," << name;
            regretFile << "," << name;
        }
        rewardFile << "\n";
        regretFile << "\n";

        for(uint32_t i = 0; i <= m_totalPackets; i++)
        {
            rewardFile << numScenario << "," << i;
            regretFile << numScenario << "," << i;
            
            for(size_t alg = 0; alg < m_activeAlgorithms.size(); alg++)
            {
                rewardFile << "," << m_results[alg].successRates[i];
                regretFile << "," << m_results[alg].cumulativeLost[i];
            }
            
            rewardFile << "\n";
            regretFile << "\n";
        }

        rewardFile.close();
        regretFile.close();
        NS_LOG_INFO("Results saved to " << fullRewardPath << " and " << fullRegretPath);
    }

    void SaveSummaryToCsv(const std::string& summaryFilename)
    {
        // Calculer les métriques détaillées
        CalculateDetailedMetrics();
        
        // Créer le dossier scratch/qoc-a s'il n'existe pas
        system("mkdir -p scratch/qoc-a");
        
        std::string fullSummaryPath = "scratch/qoc-a/" + summaryFilename;
        std::ofstream summaryFile(fullSummaryPath);

        summaryFile << "NumScenario,Scenario,NumDevices,Algorithm,Packet_Index,Succeed,Lost,Success_Rate,PayloadSize,PacketInterval,MobilityPercentage,SpreadingFactor,SimulationDuration,PDR,EnergyEfficiency,AverageToA,AverageSNR,AverageRSSI,TotalEnergyConsumption,VariableParameter,ParameterValue\n";
        
        double actualDurationMinutes = m_totalPackets * m_packetInterval / m_numDevices;
        
        // Extraire le numéro de scénario du nom de fichier
        uint32_t numScenario = ExtractScenarioNumber(summaryFilename);
        
        // Déterminer le paramètre qui varie selon le contexte
        std::string variableParam = "numDevices"; // Par défaut
        double paramValue = m_numDevices;
        std::string scenario = "S" + std::to_string(numScenario);
        
        // Logic pour déterminer quel paramètre varie et le scénario correspondant
        if(m_payloadSize != 50) {
            variableParam = "payloadSize";
            paramValue = m_payloadSize;
        } else if(m_mobilityPercentage != 0.0) {
            variableParam = "mobilityPercentage"; 
            paramValue = m_mobilityPercentage;
        } else if(m_packetInterval != 15.0) {
            variableParam = "packetInterval";
            paramValue = m_packetInterval;
        } else if(m_spreadingFactor != 7) {
            variableParam = "spreadingFactor";
            paramValue = m_spreadingFactor;
        } else {
            // Scénario de densité - garder numDevices comme paramètre variable
            variableParam = "numDevices";
            paramValue = m_numDevices;
        }
        
        for(size_t i = 0; i < m_activeAlgorithms.size(); i++)
        {
            auto& result = m_results[i];
            summaryFile << numScenario << ","
                       << scenario << ","
                       << m_numDevices << ","
                       << result.algName << ","
                       << m_totalPackets << ","
                       << result.finalSuccessful << ","
                       << result.finalLost << ","
                       << std::fixed << std::setprecision(4) 
                       << result.finalSuccessRate << ","
                       << m_payloadSize << ","
                       << m_packetInterval << ","
                       << m_mobilityPercentage << ","
                       << (int)m_spreadingFactor << ","
                       << std::setprecision(2) << actualDurationMinutes << ","
                       << std::setprecision(4) << result.pdr << ","
                       << std::setprecision(6) << result.energyEfficiency << ","
                       << std::setprecision(2) << result.averageToA << ","
                       << std::setprecision(2) << result.averageSNR << ","
                       << std::setprecision(2) << result.averageRSSI << ","
                       << std::setprecision(4) << result.totalEnergyConsumption << ","
                       << variableParam << ","
                       << paramValue << "\n";
        }
        
        summaryFile.close();
        NS_LOG_INFO("Summary saved to " << fullSummaryPath);
    }

    void PrintFinalResults()
    {
        double actualDurationMinutes = m_totalPackets * m_packetInterval / m_numDevices;
        double actualDurationHours = actualDurationMinutes / 60.0;
        
        std::cout << "\n========== FINAL RESULTS ==========\n";
        std::cout << "Scenario: " << (m_isStationary ? "Stationary" : "Non-Stationary") << "\n";
        std::cout << "Spreading Factor: SF" << (int)m_spreadingFactor << "\n";
        std::cout << "Simulation duration: " << std::fixed << std::setprecision(2) << actualDurationHours << " hours (" << std::setprecision(1) << actualDurationMinutes << " minutes)\n";
        std::cout << "Total packets per algorithm: " << m_totalPackets << "\n";
        std::cout << "Packet interval: " << m_packetInterval << " minutes\n";
        std::cout << "Number of devices: " << m_numDevices << "\n";
        std::cout << "Mobility percentage: " << m_mobilityPercentage << "%\n";
        std::cout << "Algorithms tested: " << m_activeAlgorithms.size() << "\n\n";
        
        std::cout << "Algorithm\tSucceed\tLost\tSuccess Rate\n";
        std::cout << "=========\t======\t====\t============\n";
        
        for(size_t i = 0; i < m_activeAlgorithms.size(); i++)
        {
            std::cout << m_results[i].algName << "\t\t" 
                     << m_results[i].finalSuccessful << "\t"
                     << m_results[i].finalLost << "\t"
                     << std::fixed << std::setprecision(1) 
                     << (m_results[i].finalSuccessRate * 100.0) << "%\n";
        }
        std::cout << "\n";
    }

    void PrintChannelStatistics()
    {
        std::cout << "\n========== CHANNEL STATISTICS ==========\n";
        std::cout << "Channel\tFreq(MHz)\tESP(dBm)\tSF\n";
        std::cout << "=======\t=========\t========\t===\n";
        for(uint32_t i = 0; i < m_K; i++)
        {
            std::cout << i << "\t" 
                     << std::fixed << std::setprecision(1) 
                     << m_channelModel->GetFrequency(i) << "\t\t"
                     << m_channelModel->GetChannelESP_dBm(i) << "\t\t"
                     << (int)m_channelModel->GetSpreadingFactor() << "\n";
        }
        std::cout << "\n";
    }

    // Méthodes pour calculer les métriques détaillées avec support SF
    double CalculateToA(uint32_t payloadSize, uint8_t sf, double bandwidth = 125000)
    {
        // Calcul du Time on Air selon la formule LoRaWAN
        double de = 0; // Data rate optimization disabled
        double ih = 0; // Implicit header disabled
        double crc = 1; // CRC enabled
        double cr = 1; // Code rate 4/5
        
        // Payload length
        double pl = payloadSize;
        
        // Calcul du nombre de symboles dans le préambule
        double nPreamble = 8; // Standard preamble length
        
        // Calcul du nombre de symboles dans le payload
        double argument = (8 * pl - 4 * sf + 28 + 16 * crc - 20 * ih) / (4 * (sf - 2 * de));
        double nPayload = 8 + std::max(0.0, ceil(argument) * (cr + 4));
        
        // Time on Air total
        double tSymbol = (1 << sf) / bandwidth; // Symbol time
        double toA = (nPreamble + 4.25 + nPayload) * tSymbol * 1000; // en ms
        
        return toA;
    }

    double CalculateEnergyConsumption(uint32_t totalPackets, double avgToA)
    {
        // Paramètres énergétiques LoRaWAN typiques
        double txCurrent = 120e-3; // 120 mA en transmission
        double rxCurrent = 13e-3;  // 13 mA en réception
        double sleepCurrent = 1e-6; // 1 µA en veille
        double voltage = 3.3; // V
        
        // Temps actif pour transmission (ToA)
        double txTime = totalPackets * avgToA / 1000.0; // en secondes
        
        // Temps d'écoute (assumption: 1% du temps)
        double rxTime = m_totalPackets * m_packetInterval * 60 * 0.01; // 1% du temps total
        
        // Temps de veille
        double sleepTime = m_totalPackets * m_packetInterval * 60 - txTime - rxTime;
        
        // Énergie totale consommée
        double totalEnergy = (txCurrent * txTime + rxCurrent * rxTime + sleepCurrent * sleepTime) * voltage;
        
        return totalEnergy; // en Joules
    }

    double GenerateRealisticSNR(uint32_t deviceIndex, uint32_t packetIndex)
    {
        // Générer un SNR réaliste basé sur la distance et les conditions
        std::mt19937 rng(deviceIndex * 1000 + packetIndex);
        
        // SF impact sur le SNR requis (SF plus élevé = meilleur SNR effectif)
        double sfBonus = (m_spreadingFactor - 7) * 1.5; // 1.5 dB par SF
        double baseSNR = 8.0 + sfBonus;
        
        std::normal_distribution<double> snrDist(baseSNR, 3.0);
        
        // Variation plus importante basée sur la mobilité
        double mobilityFactor = (m_mobilityPercentage / 100.0) * 4.0; // Impact plus fort de la mobilité
        std::normal_distribution<double> mobilityNoise(0.0, mobilityFactor);
        
        // La mobilité peut aussi causer des chutes soudaines de SNR
        double mobilityPenalty = m_mobilityPercentage * 0.05; // Pénalité moyenne due à la mobilité
        
        double snr = snrDist(rng) + mobilityNoise(rng) - mobilityPenalty;
        return std::max(-10.0, std::min(20.0, snr)); // Limiter entre -10 et 20 dB
    }

    double GenerateRealisticRSSI(uint32_t deviceIndex, uint32_t packetIndex)
    {
        // Générer un RSSI réaliste basé sur la distance
        std::mt19937 rng(deviceIndex * 2000 + packetIndex);
        std::normal_distribution<double> rssiDist(-110.0, 10.0); // RSSI moyen -110 dBm, écart-type 10 dB
        
        // Variation basée sur le nombre de dispositifs (densité)
        double densityFactor = (double)m_numDevices / 100.0; // Facteur de densité
        double densityOffset = densityFactor * 5.0; // Plus de dispositifs = plus d'interférences
        
        // Impact de la mobilité sur le RSSI (fading plus important)
        double mobilityFading = m_mobilityPercentage * 0.2; // 0.2 dB par % de mobilité
        
        double rssi = rssiDist(rng) - densityOffset - mobilityFading;
        return std::max(-130.0, std::min(-40.0, rssi)); // Limiter entre -130 et -40 dBm
    }

    void CalculateDetailedMetrics()
    {
        for(size_t i = 0; i < m_activeAlgorithms.size(); i++)
        {
            auto& result = m_results[i];
            
            // PDR (Packet Delivery Ratio)
            result.pdr = result.finalSuccessRate;
            
            // Calcul du ToA moyen avec le SF spécifique
            result.averageToA = CalculateToA(m_payloadSize, m_spreadingFactor, 125000);
            
            // Calcul de l'énergie totale consommée
            result.totalEnergyConsumption = CalculateEnergyConsumption(m_totalPackets, result.averageToA);
            
            // Efficacité énergétique (paquets réussis par Joule)
            result.energyEfficiency = (result.totalEnergyConsumption > 0) ? 
                result.finalSuccessful / result.totalEnergyConsumption : 0.0;
            
            // Calcul des métriques SNR et RSSI moyennes
            double totalSNR = 0.0, totalRSSI = 0.0;
            uint32_t samples = std::min(m_totalPackets, (uint32_t)100); // Échantillonner 100 paquets max
            
            for(uint32_t j = 0; j < samples; j++)
            {
                totalSNR += GenerateRealisticSNR(i, j);
                totalRSSI += GenerateRealisticRSSI(i, j);
            }
            
            result.averageSNR = totalSNR / samples;
            result.averageRSSI = totalRSSI / samples;
        }
    }

private:
    // Fonction utilitaire pour extraire le numéro de scénario du nom de fichier
    uint32_t ExtractScenarioNumber(const std::string& filename)
    {
        // Rechercher le pattern "scenarioX" dans le nom de fichier
        size_t pos = filename.find("scenario");
        if(pos != std::string::npos)
        {
            // Vérifier si on a "scenario" suivi d'un chiffre
            pos += 8; // longueur de "scenario"
            if(pos < filename.length() && std::isdigit(filename[pos]))
            {
                return static_cast<uint32_t>(filename[pos] - '0');
            }
        }
        
        // Si pas trouvé dans le nom de fichier, utiliser la logique existante
        if(m_payloadSize != 50) {
            return 2; // Scénario 2 - Spreading Factor (anciennement payload)
        } else if(m_mobilityPercentage != 0.0) {
            return 4; // Scénario 4 - Mobilité
        } else if(m_packetInterval != 15.0) {
            return 3; // Scénario 3 - Fréquence de transmission
        } else if(m_spreadingFactor != 7) {
            return 2; // Scénario 2 - Spreading Factor
        } else {
            // Déterminer S1 vs S5 basé sur la densité
            double density = (double)m_numDevices / 4.0; // Supposant 4 km²
            if(density <= 25) return 5; // Scénario 5 - Densité du réseau
            else return 1; // Scénario 1 - Charge du trafic
        }
    }

public:
};

int main(int argc, char *argv[])
{
    // Paramètres de simulation configurables pour 5 scénarios
    uint32_t numNodes = 100;
    uint32_t payloadSize = 50;
    double packetInterval = 15.0; // minutes
    double mobilityPercentage = 0.0; // pourcentage de nœuds mobiles
    uint8_t spreadingFactor = 7; // NEW: Spreading Factor (SF7-SF12)
    uint32_t numPacketsPerDevice = 110; // Nombre de messages par device
    bool stationary = true;
    bool nonStationary = true;
    std::string outputPrefix = "qoc_results";

    // Parse command line arguments
    CommandLine cmd;
    cmd.AddValue("numNodes", "Number of LoRaWAN devices", numNodes);
    cmd.AddValue("payloadSize", "Payload size in bytes", payloadSize);
    cmd.AddValue("packetInterval", "Packet transmission interval in minutes", packetInterval);
    cmd.AddValue("mobilityPercentage", "Percentage of mobile nodes (0-100)", mobilityPercentage);
    cmd.AddValue("spreadingFactor", "Spreading Factor (7-12)", spreadingFactor);
    cmd.AddValue("numPacketsPerDevice", "Number of packets per device", numPacketsPerDevice);
    cmd.AddValue("stationary", "Run stationary scenario", stationary);
    cmd.AddValue("nonStationary", "Run non-stationary scenario", nonStationary);
    cmd.AddValue("outputPrefix", "Output files prefix", outputPrefix);
    cmd.Parse(argc, argv);

    LogComponentEnable("LoRaWANQoCSimulation", LOG_LEVEL_INFO);

    std::cout << "LoRaWAN QoC-A Simulation - Multi-Scenario Version (5 Scenarios)\n";
    std::cout << "===============================================================\n";
    std::cout << "Configuration:\n";
    std::cout << "  Devices: " << numNodes << "\n";
    std::cout << "  Payload: " << payloadSize << " bytes\n";
    std::cout << "  Interval: " << packetInterval << " minutes\n";
    std::cout << "  Mobility: " << mobilityPercentage << "%\n";
    std::cout << "  Spreading Factor: SF" << (int)spreadingFactor << "\n";
    std::cout << "  Output: " << outputPrefix << "\n\n";

    std::cout << "  Packets/Device: " << numPacketsPerDevice << "\n\n";

    // Conditionally run scenarios based on parameters
    if(stationary)
    {
        // Scenario 1: Stationary (QoC-A)
        std::cout << "Running Stationary Scenario (QoC-A)...\n";
        LoRaWANQoCSimulation stationarySim(true, numNodes, payloadSize, packetInterval, mobilityPercentage, spreadingFactor, numPacketsPerDevice);
        stationarySim.PrintChannelStatistics();
        stationarySim.RunSimulation();
        stationarySim.SaveResultsToCsv(outputPrefix + "_stationary_rewards.csv",
                                     outputPrefix + "_stationary_regret.csv");
        stationarySim.SaveSummaryToCsv(outputPrefix + "_stationary_summary.csv");
        stationarySim.PrintFinalResults();
    }

    if(nonStationary)
    {
        // Scenario 2: Non-stationary (DQoC-A)
        std::cout << "\nRunning Non-Stationary Scenario (DQoC-A)...\n";
        LoRaWANQoCSimulation nonStationarySim(false, numNodes, payloadSize, packetInterval, mobilityPercentage, spreadingFactor, numPacketsPerDevice);
        nonStationarySim.RunSimulation();
        nonStationarySim.SaveResultsToCsv(outputPrefix + "_nonstationary_rewards.csv",
                                        outputPrefix + "_nonstationary_regret.csv");
        nonStationarySim.SaveSummaryToCsv(outputPrefix + "_nonstationary_summary.csv");
        nonStationarySim.PrintFinalResults();
    }

    return 0;
}