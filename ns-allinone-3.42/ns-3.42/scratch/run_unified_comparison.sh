#!/bin/bash

################################################################################
# Script d'exécution unifiée - Comparaison des 5 algorithmes RL pour LoRaWAN
#
# ============================================================================
# ALGORITHMES EXÉCUTÉS (5 algorithmes uniquement) :
# ============================================================================
# 1. UCB1-Tuned  → lora-ucb1-simulation
# 2. QoC-A       → lorawan_qoca_simulation (stationary=true)
# 3. DQoC-A      → lorawan_qoca_simulation (nonStationary=true)
# 4. D-LoRa      → d-lora-simulation
# 5. ToW         → tow-lorawan-simulation
#
# ============================================================================
# SCÉNARIOS EXÉCUTÉS (4 scénarios par algorithme) :
# ============================================================================
# Scénario 1: Densité varie (100-1000)
#             avec Vitesse(0,30), Mobile%(0,50), Trafic(5,40)
#
# Scénario 2: Trafic varie (1-55 min)
#             avec Densité(200,600), Vitesse(0,30), Mobile%(0,50)
#
# Scénario 3: Vitesse varie (0-60 km/h)
#             avec Densité(200,600), Mobile%(0,50), Trafic(5,40)
#
# Scénario 4: Mobile% varie (0-90%)
#             avec Densité(200,600), Vitesse(0,30), Trafic(5,40)
#
# ============================================================================
# TOTAL: 5 algorithmes × 4 scénarios = 20 combinaisons par répétition
# ============================================================================
################################################################################

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Répertoire de base
BASE_DIR="/media/bonice/0CC8175A0CC8175A/Article final/ns-allinone-3.42/ns-3.42"
OUTPUT_DIR="$BASE_DIR/scratch/unified_results"
SUMMARY_DIR="$OUTPUT_DIR/summaries"

# Nombre de messages par device (fixé à 110)
NUM_TRANSMISSIONS=5

# Nombre de répétitions
NUM_REPETITIONS=5

# Variable pour le numéro de répétition courant (sera mise à jour dans la boucle)
CURRENT_REP=1

# Créer les répertoires de sortie
mkdir -p "$OUTPUT_DIR"
mkdir -p "$SUMMARY_DIR"

# Fonction d'affichage avec timestamp
log_info() {
    echo -e "${GREEN}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

log_error() {
    echo -e "${RED}[$(date '+%Y-%m-%d %H:%M:%S')] ERROR:${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[$(date '+%Y-%m-%d %H:%M:%S')] WARNING:${NC} $1"
}

log_scenario() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} =========================================="
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} =========================================="
}

log_algo() {
    echo -e "${CYAN}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} Algorithme: $1"
}

# Fonction pour compiler le projet
compile_project() {
    log_info "Compilation du projet NS-3..."
    cd "$BASE_DIR"

    # Nettoyer le cache CMake corrompu si nécessaire
    if [ -d "cmake-cache" ]; then
        log_info "Nettoyage du cache CMake..."
        rm -rf cmake-cache
    fi

    ./ns3 configure 2>&1 | tee "$OUTPUT_DIR/configure.log"
    if [ $? -ne 0 ]; then
        log_error "Échec de la configuration"
        exit 1
    fi

    ./ns3 build 2>&1 | tee "$OUTPUT_DIR/build.log"
    if [ $? -ne 0 ]; then
        log_error "Échec de la compilation"
        exit 1
    fi

    log_info "✓ Compilation réussie"
}

# Créer l'en-tête du fichier CSV pour une répétition (tous les algorithmes)
create_csv_header() {
    local rep=$1
    CURRENT_CSV="$OUTPUT_DIR/results_rep_${rep}.csv"
    echo "Scenario,NumDevices,Algorithm,Packet_Index,Succeed,Lost,Success_Rate,PayloadSize,PacketInterval,MobilityPercentage,SpreadingFactor,SimulationDuration,PDR,EnergyEfficiency,AverageToA,AverageSNR,AverageRSSI,TotalEnergyConsumption,VariableParameter,ParameterValue,MobilitySpeed" > "$CURRENT_CSV"
    log_info "Fichier CSV créé pour répétition $rep (tous les algorithmes): $CURRENT_CSV"
}

# Créer le fichier summary pour une répétition (tous les algorithmes)
create_summary_rep() {
    local rep=$1
    local input_csv="$OUTPUT_DIR/results_rep_${rep}.csv"
    local summary_csv="$SUMMARY_DIR/summary_rep_${rep}.csv"

    log_info "Création du summary pour répétition $rep (tous les algorithmes)..."

    python3 << EOF
import pandas as pd

# Lire le fichier CSV de la répétition
df = pd.read_csv('$input_csv', on_bad_lines='skip')

# Créer un résumé agrégé
summary = df.groupby(['Algorithm', 'Scenario', 'NumDevices', 'VariableParameter', 'ParameterValue',
                      'PayloadSize', 'PacketInterval', 'MobilityPercentage', 'SpreadingFactor',
                      'SimulationDuration', 'MobilitySpeed']).agg({
    'Succeed': 'sum',
    'Lost': 'sum',
    'PDR': 'first',
    'EnergyEfficiency': 'first',
    'AverageToA': 'first',
    'AverageSNR': 'first',
    'AverageRSSI': 'first',
    'TotalEnergyConsumption': 'first'
}).reset_index()

# Calculer Success_Rate
summary['TotalPackets'] = summary['Succeed'] + summary['Lost']
summary['Success_Rate'] = (summary['Succeed'] / summary['TotalPackets'] * 100).round(4)

# Ajouter le numéro de répétition
summary['Repetition'] = $rep

# Réorganiser les colonnes
summary = summary[['Repetition', 'Algorithm', 'Scenario', 'NumDevices', 'VariableParameter', 'ParameterValue',
                   'PayloadSize', 'PacketInterval', 'MobilityPercentage', 'SpreadingFactor',
                   'SimulationDuration', 'MobilitySpeed', 'TotalPackets', 'Succeed', 'Lost',
                   'Success_Rate', 'PDR', 'EnergyEfficiency', 'AverageToA', 'AverageSNR',
                   'AverageRSSI', 'TotalEnergyConsumption']]

# Trier par Algorithme, Scénario, puis paramètre
summary = summary.sort_values(['Algorithm', 'Scenario', 'VariableParameter', 'ParameterValue'])
summary.to_csv('$summary_csv', index=False)

# Afficher les statistiques
num_algos = summary['Algorithm'].nunique()
num_scenarios = summary['Scenario'].nunique()
print(f"Summary répétition $rep créé:")
print(f"  - Algorithmes: {num_algos} ({', '.join(summary['Algorithm'].unique())})")
print(f"  - Scénarios: {num_scenarios} ({', '.join(summary['Scenario'].unique())})")
print(f"  - Total lignes: {len(summary)}")
EOF

    if [ -f "$summary_csv" ]; then
        log_info "✓ Summary créé : $summary_csv"
    fi
}

# Fonction pour exécuter UCB1-Tuned
run_ucb1() {
    local scenario=$1
    local numDevices=$2
    local packetInterval=$3
    local mobilitySpeed=$4
    local mobilityPercentage=$5
    local variableParam=$6
    local paramValue=$7

    log_algo "UCB1-Tuned"

    cd "$BASE_DIR"

    # Exécuter la simulation UCB1-Tuned avec 110 messages par device
    ./ns3 run "lora-ucb1-simulation --numDevices=$numDevices --txInterval=$packetInterval --mobilityPercentage=$mobilityPercentage --scenario=$scenario --algorithm=UCB1-tuned --numTransmissions=$NUM_TRANSMISSIONS" 2>&1 | tee -a "$OUTPUT_DIR/ucb1.log"

    # Récupérer les résultats du fichier CSV généré (dans scratch/lorawan/results/)
    local result_file=$(ls -t scratch/lorawan/results/UCB1*.csv 2>/dev/null | head -1)
    if [ -f "$result_file" ]; then
        # Ajouter les données (sans l'en-tête) au fichier unifié avec vitesse mobilité
        tail -n +2 "$result_file" | while read line; do
            echo "$line,$mobilitySpeed" >> "$CURRENT_CSV"
        done
        return 0
    else
        log_warning "Pas de fichier résultat pour UCB1-Tuned"
        return 1
    fi
}

# Fonction pour exécuter QoC-A
run_qoca() {
    local scenario=$1
    local numDevices=$2
    local packetInterval=$3
    local mobilitySpeed=$4
    local mobilityPercentage=$5
    local variableParam=$6
    local paramValue=$7

    log_algo "QoC-A"

    cd "$BASE_DIR"

    # Exécuter la simulation QoC-A (stationary = QoC-A) avec 110 messages par device
    ./ns3 run "lorawan_qoca_simulation --numNodes=$numDevices --packetInterval=$packetInterval --mobilityPercentage=$mobilityPercentage --numPacketsPerDevice=$NUM_TRANSMISSIONS --stationary=true --nonStationary=false --outputPrefix=qoca_run" 2>&1 | tee -a "$OUTPUT_DIR/qoca.log"

    # Récupérer les résultats (fichier summary dans scratch/qoc-a/)
    local result_file="scratch/qoc-a/qoca_run_stationary_summary.csv"
    if [ -f "$result_file" ]; then
        # Le format summary est déjà au format unifié, ajouter avec vitesse mobilité
        tail -n +2 "$result_file" | while read line; do
            echo "$line,$mobilitySpeed" >> "$CURRENT_CSV"
        done
        rm -f scratch/qoc-a/qoca_run*.csv
        return 0
    else
        log_warning "Pas de fichier résultat pour QoC-A"
        return 1
    fi
}

# Fonction pour exécuter DQoC-A
run_dqoca() {
    local scenario=$1
    local numDevices=$2
    local packetInterval=$3
    local mobilitySpeed=$4
    local mobilityPercentage=$5
    local variableParam=$6
    local paramValue=$7

    log_algo "DQoC-A"

    cd "$BASE_DIR"

    # Exécuter la simulation DQoC-A (nonStationary = DQoC-A) avec 110 messages par device
    ./ns3 run "lorawan_qoca_simulation --numNodes=$numDevices --packetInterval=$packetInterval --mobilityPercentage=$mobilityPercentage --numPacketsPerDevice=$NUM_TRANSMISSIONS --stationary=false --nonStationary=true --outputPrefix=dqoca_run" 2>&1 | tee -a "$OUTPUT_DIR/dqoca.log"

    # Récupérer les résultats (fichier summary dans scratch/qoc-a/)
    local result_file="scratch/qoc-a/dqoca_run_nonstationary_summary.csv"
    if [ -f "$result_file" ]; then
        # Le format summary est déjà au format unifié, ajouter avec vitesse mobilité
        tail -n +2 "$result_file" | while read line; do
            echo "$line,$mobilitySpeed" >> "$CURRENT_CSV"
        done
        rm -f scratch/qoc-a/dqoca_run*.csv
        return 0
    else
        log_warning "Pas de fichier résultat pour DQoC-A"
        return 1
    fi
}

# Fonction pour exécuter D-LoRa
run_dlora() {
    local scenario=$1
    local numDevices=$2
    local packetInterval=$3
    local mobilitySpeed=$4
    local mobilityPercentage=$5
    local variableParam=$6
    local paramValue=$7

    log_algo "D-LoRa"

    cd "$BASE_DIR"

    # Convertir l'intervalle en secondes pour D-LoRa
    local intervalSeconds=$((packetInterval * 60))

    # Calculer le temps de simulation pour 110 messages par device
    # simulationTime = NUM_TRANSMISSIONS * intervalSeconds
    local simTime=$((NUM_TRANSMISSIONS * intervalSeconds))

    # Exécuter la simulation D-LoRa avec 110 messages par device
    ./ns3 run "d-lora-simulation --numNodes=$numDevices --packetInterval=$intervalSeconds --mobilityPercentage=$mobilityPercentage --algorithm=DLoRa --simulationTime=$simTime" 2>&1 | tee -a "$OUTPUT_DIR/dlora.log"

    # Récupérer les résultats (dans le répertoire courant)
    local result_file=$(ls -t simulation_results_DLoRa*.csv 2>/dev/null | head -1)
    if [ -f "$result_file" ]; then
        tail -n +2 "$result_file" | while read line; do
            echo "$line,$mobilitySpeed" >> "$CURRENT_CSV"
        done
        rm -f simulation_results_DLoRa*.csv
        return 0
    else
        log_warning "Pas de fichier résultat pour D-LoRa"
        return 1
    fi
}

# Fonction pour exécuter ToW
run_tow() {
    local scenario=$1
    local numDevices=$2
    local packetInterval=$3
    local mobilitySpeed=$4
    local mobilityPercentage=$5
    local variableParam=$6
    local paramValue=$7

    log_algo "ToW"

    cd "$BASE_DIR"

    # Convertir l'intervalle en secondes pour ToW
    local intervalSeconds=$((packetInterval * 60))

    # Calculer le temps de simulation pour 110 messages par device
    local simTime=$((NUM_TRANSMISSIONS * intervalSeconds))

    # Exécuter la simulation ToW avec 110 messages par device
    ./ns3 run "tow-lorawan-simulation --nDevices=$numDevices --packetInterval=$intervalSeconds --mobilityPercentage=$mobilityPercentage --algorithm=ToW --scenario=$scenario --variableParameter=$variableParam --simulationTime=$simTime" 2>&1 | tee -a "$OUTPUT_DIR/tow.log"

    # Chercher le fichier de résultats ToW (format: results_ToW_<scenario>_<timestamp>.csv)
    local result_file=$(ls -t results_ToW_*.csv 2>/dev/null | head -1)
    if [ -f "$result_file" ]; then
        tail -n +2 "$result_file" | while read line; do
            echo "$line,$mobilitySpeed" >> "$CURRENT_CSV"
        done
        rm -f results_ToW_*.csv
        return 0
    else
        log_warning "Pas de fichier résultat pour ToW"
        return 1
    fi
}

# Fonction pour exécuter les 4 scénarios pour un algorithme donné
run_all_scenarios_for_algo() {
    local algo=$1
    local run_func=$2
    local rep=$3

    log_algo "$algo - Exécution des 4 scénarios"

    ############################################################################
    # SCÉNARIO 1 : DENSITÉ DES DISPOSITIFS
    # Variable: Densité (100-1000)
    # Fixés: Vitesse(0,30), Mobile%(0,50), Trafic(5,40)
    ############################################################################
    log_scenario "[$algo] SCÉNARIO 1/4 : DENSITÉ DES DISPOSITIFS (100-1000)"
    log_info "Paramètres fixes: Vitesse∈{0,30}, Mobile%∈{0,50}, Trafic∈{5,40}"

    for density in "${DENSITY_VALUES[@]}"; do
        for speed in 0 30; do
            for percentage in 0 50; do
                for traffic in 5 40; do
                    log_info "[$algo] S1: Densité=$density, Intervalle=$traffic min, Vitesse=$speed km/h, Mobile=$percentage%"
                    $run_func "S1_Density" "$density" "$traffic" "$speed" "$percentage" "NumDevices" "$density"
                    TOTAL_SIMULATIONS=$((TOTAL_SIMULATIONS + 1))
                    sleep 1
                done
            done
        done
    done

    ############################################################################
    # SCÉNARIO 2 : CHARGE DU TRAFIC
    # Variable: Trafic (1-55 min)
    # Fixés: Densité(200,600), Vitesse(0,30), Mobile%(0,50)
    ############################################################################
    log_scenario "[$algo] SCÉNARIO 2/4 : CHARGE DU TRAFIC (1-55 min)"
    log_info "Paramètres fixes: Densité∈{200,600}, Vitesse∈{0,30}, Mobile%∈{0,50}"

    for traffic in "${TRAFFIC_VALUES[@]}"; do
        for density in 200 600; do
            for speed in 0 30; do
                for percentage in 0 50; do
                    log_info "[$algo] S2: Densité=$density, Intervalle=$traffic min, Vitesse=$speed km/h, Mobile=$percentage%"
                    $run_func "S2_TrafficLoad" "$density" "$traffic" "$speed" "$percentage" "PacketInterval" "$traffic"
                    TOTAL_SIMULATIONS=$((TOTAL_SIMULATIONS + 1))
                    sleep 1
                done
            done
        done
    done

    ############################################################################
    # SCÉNARIO 3 : MOBILITÉ - VITESSE
    # Variable: Vitesse (0-60 km/h)
    # Fixés: Densité(200,600), Mobile%(0,50), Trafic(5,40)
    ############################################################################
    log_scenario "[$algo] SCÉNARIO 3/4 : MOBILITÉ - VITESSE (0-60 km/h)"
    log_info "Paramètres fixes: Densité∈{200,600}, Mobile%∈{0,50}, Trafic∈{5,40}"

    for speed in "${SPEED_VALUES[@]}"; do
        for density in 200 600; do
            for percentage in 0 50; do
                for traffic in 5 40; do
                    log_info "[$algo] S3: Densité=$density, Intervalle=$traffic min, Vitesse=$speed km/h, Mobile=$percentage%"
                    $run_func "S3_MobilitySpeed" "$density" "$traffic" "$speed" "$percentage" "MobilitySpeed" "$speed"
                    TOTAL_SIMULATIONS=$((TOTAL_SIMULATIONS + 1))
                    sleep 1
                done
            done
        done
    done

    ############################################################################
    # SCÉNARIO 4 : MOBILITÉ - POURCENTAGE
    # Variable: Mobile% (0-90%)
    # Fixés: Densité(200,600), Vitesse(0,30), Trafic(5,40)
    ############################################################################
    log_scenario "[$algo] SCÉNARIO 4/4 : MOBILITÉ - POURCENTAGE (0-90%)"
    log_info "Paramètres fixes: Densité∈{200,600}, Vitesse∈{0,30}, Trafic∈{5,40}"

    for percentage in "${PERCENTAGE_VALUES[@]}"; do
        for density in 200 600; do
            for speed in 0 30; do
                for traffic in 5 40; do
                    log_info "[$algo] S4: Densité=$density, Intervalle=$traffic min, Vitesse=$speed km/h, Mobile=$percentage%"
                    $run_func "S4_MobilityPercentage" "$density" "$traffic" "$speed" "$percentage" "MobilityPercentage" "$percentage"
                    TOTAL_SIMULATIONS=$((TOTAL_SIMULATIONS + 1))
                    sleep 1
                done
            done
        done
    done

    log_info "✓ $algo terminé - 4 scénarios exécutés"
}

################################################################################
# DÉBUT DU SCRIPT PRINCIPAL
################################################################################

clear
echo "================================================================================"
echo "    COMPARAISON UNIFIÉE DES 5 ALGORITHMES RL POUR LORAWAN"
echo "================================================================================"
echo ""
echo -e "${CYAN}ALGORITHMES À EXÉCUTER (5):${NC}"
echo "  1. UCB1-Tuned  → lora-ucb1-simulation"
echo "  2. QoC-A       → lorawan_qoca_simulation (stationary)"
echo "  3. DQoC-A      → lorawan_qoca_simulation (non-stationary)"
echo "  4. D-LoRa      → d-lora-simulation"
echo "  5. ToW         → tow-lorawan-simulation"
echo ""
echo -e "${CYAN}SCÉNARIOS PAR ALGORITHME (4):${NC}"
echo "  S1. Densité varie (100-1000) avec Vitesse(0,30), Mobile%(0,50), Trafic(5,40)"
echo "  S2. Trafic varie (1-55 min) avec Densité(200,600), Vitesse(0,30), Mobile%(0,50)"
echo "  S3. Vitesse varie (0-60 km/h) avec Densité(200,600), Mobile%(0,50), Trafic(5,40)"
echo "  S4. Mobile% varie (0-90%) avec Densité(200,600), Vitesse(0,30), Trafic(5,40)"
echo ""
echo "Messages par device: $NUM_TRANSMISSIONS"
echo "Répétitions: $NUM_REPETITIONS"
echo ""
echo "Résultats dans: $OUTPUT_DIR"
echo "Summaries dans: $SUMMARY_DIR"
echo ""

read -p "Voulez-vous continuer ? (o/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Oo]$ ]]; then
    log_warning "Exécution annulée par l'utilisateur"
    exit 0
fi

# Initialisation
START_TIME=$(date +%s)
TOTAL_SIMULATIONS=0
SUCCESSFUL_SIMULATIONS=0

# Compilation du projet
compile_project

# Définir les valeurs des paramètres
DENSITY_VALUES=(100 200 300 400 500 600 700 800 900 1000)
TRAFFIC_VALUES=(1 7 13 19 25 31 37 43 49 55)
SPEED_VALUES=(0 7 13 20 27 33 40 47 53 60)
PERCENTAGE_VALUES=(0 10 20 30 40 50 60 70 80 90)

################################################################################
# BOUCLE PRINCIPALE : RÉPÉTITIONS
################################################################################

# Liste des 5 algorithmes avec leurs fonctions d'exécution
# IMPORTANT: Seuls ces 5 algorithmes seront exécutés
ALGORITHMS=("UCB1-Tuned" "QoC-A" "DQoC-A" "D-LoRa" "ToW")
ALGO_FUNCTIONS=("run_ucb1" "run_qoca" "run_dqoca" "run_dlora" "run_tow")

log_info "=============================================="
log_info "DÉBUT DE L'EXÉCUTION DES 5 ALGORITHMES"
log_info "Chaque algorithme exécutera ses 4 scénarios"
log_info "=============================================="

for rep in $(seq 1 $NUM_REPETITIONS); do
    log_scenario "RÉPÉTITION $rep / $NUM_REPETITIONS"

    # Créer le fichier CSV pour cette répétition (contiendra TOUS les algorithmes)
    create_csv_header "$rep"

    ############################################################################
    # EXÉCUTER CHAQUE ALGORITHME AVEC SES 4 SCÉNARIOS
    ############################################################################
    for i in "${!ALGORITHMS[@]}"; do
        algo="${ALGORITHMS[$i]}"
        func="${ALGO_FUNCTIONS[$i]}"

        echo ""
        echo -e "${MAGENTA}================================================================================${NC}"
        echo -e "${MAGENTA}    ALGORITHME $((i+1))/5: $algo - Répétition $rep / $NUM_REPETITIONS${NC}"
        echo -e "${MAGENTA}    Simulation: ${ALGO_FUNCTIONS[$i]}${NC}"
        echo -e "${MAGENTA}================================================================================${NC}"
        echo ""

        run_all_scenarios_for_algo "$algo" "$func" "$rep"

        log_info "✓ $algo (Répétition $rep) terminé - 4 scénarios exécutés"
        echo ""
    done

    # Créer le summary pour cette répétition (contient TOUS les algorithmes et scénarios)
    create_summary_rep "$rep"

    log_info "✓ Répétition $rep terminée - Les 5 algorithmes ont exécuté leurs 4 scénarios"
    log_info "  Fichier CSV: results_rep_${rep}.csv"
    log_info "  Fichier Summary: summary_rep_${rep}.csv"
done

################################################################################
# FIN DE L'EXÉCUTION
################################################################################

END_TIME=$(date +%s)
EXECUTION_TIME=$((END_TIME - START_TIME))
HOURS=$((EXECUTION_TIME / 3600))
MINUTES=$(((EXECUTION_TIME % 3600) / 60))
SECONDS=$((EXECUTION_TIME % 60))

echo ""
echo "================================================================================"
echo "                          RÉSUMÉ DE L'EXÉCUTION"
echo "================================================================================"
echo ""
echo -e "${CYAN}ALGORITHMES EXÉCUTÉS:${NC}"
echo "  1. UCB1-Tuned  (lora-ucb1-simulation)"
echo "  2. QoC-A       (lorawan_qoca_simulation - stationary)"
echo "  3. DQoC-A      (lorawan_qoca_simulation - non-stationary)"
echo "  4. D-LoRa      (d-lora-simulation)"
echo "  5. ToW         (tow-lorawan-simulation)"
echo ""
echo -e "${CYAN}SCÉNARIOS PAR ALGORITHME:${NC}"
echo "  S1. Densité varie (100-1000)"
echo "  S2. Trafic varie (1-55 min)"
echo "  S3. Vitesse varie (0-60 km/h)"
echo "  S4. Mobile% varie (0-90%)"
echo ""
echo "Simulations exécutées : $TOTAL_SIMULATIONS (5 algos × 4 scénarios × $NUM_REPETITIONS répétitions)"
echo "Temps d'exécution     : ${HOURS}h ${MINUTES}min ${SECONDS}s"
echo ""

# Compter les fichiers générés (1 par répétition, contenant tous les algos)
NUM_RESULTS=$(ls -1 "$OUTPUT_DIR"/results_rep_*.csv 2>/dev/null | wc -l)
NUM_SUMMARIES=$(ls -1 "$SUMMARY_DIR"/summary_rep_*.csv 2>/dev/null | wc -l)

echo "Fichiers résultats générés : $NUM_RESULTS (1 par répétition, contenant les 5 algorithmes)"
echo "Fichiers summary générés   : $NUM_SUMMARIES (1 par répétition, contenant les 5 algorithmes)"
echo ""

# Afficher le contenu des summaries
echo "Contenu des fichiers summary :"
for rep in $(seq 1 $NUM_REPETITIONS); do
    summary_file="$SUMMARY_DIR/summary_rep_${rep}.csv"
    if [ -f "$summary_file" ]; then
        num_lines=$(wc -l < "$summary_file")
        echo "  - summary_rep_${rep}.csv : $((num_lines - 1)) lignes de données"
    fi
done
echo ""

# Créer un fichier global combinant tous les summaries
log_info "Création du fichier summary global..."

# Créer un fichier global combinant tous les summaries
log_info "Création du fichier summary global..."

# Summary global combinant toutes les répétitions
GLOBAL_SUMMARY="$OUTPUT_DIR/all_algorithms_all_repetitions.csv"

python3 << EOF
import pandas as pd
import glob

# Lire tous les fichiers summary (1 par répétition)
summary_files = sorted(glob.glob('$SUMMARY_DIR/summary_rep_*.csv'))

if summary_files:
    # Combiner tous les summaries
    all_summaries = pd.concat([pd.read_csv(f) for f in summary_files], ignore_index=True)
    all_summaries = all_summaries.sort_values(['Repetition', 'Algorithm', 'Scenario', 'VariableParameter', 'ParameterValue'])
    all_summaries.to_csv('$GLOBAL_SUMMARY', index=False)

    # Afficher les statistiques
    print(f"Summary global créé: $GLOBAL_SUMMARY")
    print(f"  - Répétitions: {all_summaries['Repetition'].nunique()}")
    print(f"  - Algorithmes: {all_summaries['Algorithm'].nunique()} ({', '.join(all_summaries['Algorithm'].unique())})")
    print(f"  - Scénarios: {all_summaries['Scenario'].nunique()} ({', '.join(all_summaries['Scenario'].unique())})")
    print(f"  - Total lignes: {len(all_summaries)}")
else:
    print("Aucun fichier summary trouvé")
EOF

if [ -f "$GLOBAL_SUMMARY" ]; then
    log_info "✓ Summary global créé : $GLOBAL_SUMMARY"
fi

echo ""
echo "================================================================================"
echo "                    EXÉCUTION TERMINÉE"
echo "================================================================================"
echo ""
echo "Fichiers générés :"
echo ""
echo "  Résultats bruts par répétition (contient les 5 algos × 4 scénarios) :"
echo "    $OUTPUT_DIR/results_rep_<N>.csv"
echo ""
echo "  Summary par répétition (contient les 5 algos × 4 scénarios) :"
echo "    $SUMMARY_DIR/summary_rep_<N>.csv"
echo ""
echo "  Summary global (toutes répétitions, tous algorithmes, tous scénarios) :"
echo "    $GLOBAL_SUMMARY"
echo ""

exit 0
