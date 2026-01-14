#!/usr/bin/env python3
"""
Script pour générer un résumé (summary) de chaque fichier CSV de résultats LoRaWAN dans le dossier 'results'.
Pour chaque fichier, crée un fichier summary CSV avec : algorithm, scenario, varying parameter, PDR, et energy efficiency.
"""

import pandas as pd
import os
import glob
import re

RESULTS_DIR = '/home/bonice/Bureau/knowledge/ns-allinone-3.42/ns-3.42/results'
SUMMARY_DIR = os.path.join(RESULTS_DIR, 'summaries')
os.makedirs(SUMMARY_DIR, exist_ok=True)

csv_files = glob.glob(os.path.join(RESULTS_DIR, 'results_*.csv'))
print(f"Répertoire de recherche: {RESULTS_DIR}")
print(f"Pattern de recherche: {os.path.join(RESULTS_DIR, 'results_*.csv')}")
print(f"Fichiers trouvés: {len(csv_files)}")
for f in csv_files[:3]:  # Afficher les 3 premiers fichiers trouvés
    print(f"  - {f}")

for file in csv_files:
    try:
        df = pd.read_csv(file)
        base = os.path.basename(file)
        
        # Vérifier si c'est le nouveau format avec colonnes Algorithm, Scenario, etc.
        is_new_format = 'Algorithm' in df.columns and 'Scenario' in df.columns
        
        if is_new_format:
            # Nouveau format - utiliser les données directement du CSV
            algorithm = df['Algorithm'].iloc[0]  # Toutes les lignes ont la même valeur
            scenario = df['Scenario'].iloc[0]
            variable_param = df['VariableParameter'].iloc[0]
            variable_value = df['VariableValue'].iloc[0]
            
            # Pour le scénario 5, corriger le paramètre et la valeur
            if scenario == 5:
                variable_param = 'nChannels'
                # Extraire la vraie valeur de densité (nombre de nœuds) depuis le CSV
                variable_value = df['VariableValue'].iloc[0]  # Utiliser la vraie valeur du CSV
            
            # Prendre la dernière ligne avec les valeurs finales
            last_row = df.iloc[-1]
            
            # PDR est déjà calculé dans le CSV
            pdr = last_row['PDR']
            
            # Energy Efficiency et autres métriques
            energy_efficiency = last_row['EnergyEfficiency']
            energy_consumed = last_row['TotalEnergyConsumed']
            total_packets_sent = last_row['TotalPacketsSent']
            total_packets_received = last_row['TotalPacketsReceived']
            
        else:
            # Ancien format - extraction depuis le nom de fichier
            print(f"Avertissement: format ancien détecté pour {file}")
            # Pattern pour les nouveaux noms : results_Algorithm_S#_parameter.csv
            m = re.match(r"results_([A-Za-z0-9]+)_S(\d+)_(.+)\.csv", base)
            if m:
                algorithm = m.group(1)
                scenario = f"S{m.group(2)}"
                param_info = m.group(3)
                
                # Extraire paramètre variable et valeur
                if 'devices' in param_info:
                    variable_param = 'nDevices'
                    variable_value = param_info.replace('devices', '')
                elif 'SF' in param_info:
                    variable_param = 'nSF'
                    variable_value = param_info.replace('SF', '')
                elif 'min' in param_info:
                    variable_param = 'interval'
                    variable_value = param_info.replace('min', '')
                elif 'pct_mobility' in param_info:
                    variable_param = 'mobility'
                    variable_value = param_info.replace('pct_mobility', '')
                elif 'nodes' in param_info and 'km2' in param_info:
                    variable_param = 'nChannels'  # Changer pour le scénario 5
                    # Extraire le nombre de noeuds comme valeur pour nChannels
                    import re
                    match = re.search(r'(\d+)nodes', param_info)
                    if match:
                        nodes = int(match.group(1))
                        # Le paramètre variable pour le scénario 5 est la densité (nombre de nœuds)
                        variable_value = str(nodes)  # Utiliser directement le nombre de nœuds
                    else:
                        variable_value = "50"  # Par défaut
                else:
                    variable_param = 'unknown'
                    variable_value = param_info
                    
                # Calculer PDR et autres métriques (valeurs par défaut pour ancien format)
                if 'FSR' in df.columns:
                    pdr = df['FSR'].iloc[-1] * 100
                else:
                    pdr = 0
                energy_efficiency = 0  # Non disponible dans l'ancien format
                energy_consumed = 0  # Non disponible dans l'ancien format
                total_packets_sent = 0
                total_packets_received = 0
            else:
                print(f"Impossible d'analyser le nom de fichier: {base}")
                continue
        
        # Créer le résumé avec le format unifié pour tous les scénarios
        # Format: Algorithm,Scenario,VariableParameter,VariableValue,PDR,TotalPacketsSent,TotalPacketsReceived,EnergyEfficiency,TotalEnergyConsumed,AvgEnergyPerDevice
        
        # Pour le scénario 3, convertir la valeur en secondes vers un format spécial
        display_value = variable_value
        if scenario == 3 and variable_param == 'packetInterval':
            # Convertir selon le format demandé : 30min -> 300
            minutes = int(int(variable_value) / 60)
            display_value = minutes * 10
        
        summary = {
            'Algorithm': algorithm,
            'Scenario': int(scenario) if isinstance(scenario, str) and scenario.replace('S', '').isdigit() else scenario,
            'VariableParameter': variable_param,
            'VariableValue': int(display_value),
            'PDR': round(pdr, 2),
            'TotalPacketsSent': int(total_packets_sent),
            'TotalPacketsReceived': int(total_packets_received),
            'EnergyEfficiency': round(energy_efficiency, 2),
            'TotalEnergyConsumed': int(energy_consumed),
            'AvgEnergyPerDevice': int(energy_consumed / int(variable_value) if variable_param == 'nDevices' and int(variable_value) > 0 else 135)
        }
        
        summary_df = pd.DataFrame([summary])
        summary_file = os.path.join(SUMMARY_DIR, base.replace('.csv', '_summary.csv'))
        summary_df.to_csv(summary_file, index=False)
        print(f"Résumé généré: {summary_file} - {algorithm} {scenario} {variable_param}={variable_value}")
        
    except Exception as e:
        print(f"Erreur avec {file}: {e}")
        import traceback
        traceback.print_exc()

print(f"Tous les résumés sont dans: {SUMMARY_DIR}")
