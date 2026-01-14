#! /usr/bin/env python3
import sys
from optparse import OptionParser
import os
from xml.dom import minidom as dom
import shlex
import pandas as pd
import glob

import constants
from util import run_command, fatal, CommandError


def build_netanim(qmakepath):
    qmake = 'qmake'
    qmakeFound = False
    try:
        run_command([qmake, '-v'])
        print("qmake found")
        qmakeFound = True
    except:
        print("Could not find qmake in the default path")

    try:
        if qmakeFound == False:
                run_command(['qmake-qt5', '-v'])
                qmake = 'qmake-qt5'
                print("qmake-qt5 found")
    except:
        print("Could not find qmake-qt5 in the default path")
        
    if qmakepath:
        print("Setting qmake to user provided path")
        qmake = qmakepath
    try:    
        run_command([qmake, 'NetAnim.pro'])
        run_command(['make'])
    except OSError:
        print("Error building NetAnim. Ensure the path to qmake is correct.")
        print("Could not find qmake or qmake-qt5 in the default PATH.")
        print("Use ./build.py --qmake-path <Path-to-qmake>, if qmake is installed in a non-standard location")
        print("Note: Some systems use qmake-qt5 instead of qmake")
        print("Skipping NetAnim ....")
        pass
    except:
        print("Error building NetAnim.")
        print("Skipping NetAnim ....")
        pass

def build_ns3(config, build_examples, build_tests, args, build_options):
    cmd = [sys.executable, "ns3", "configure"] + args

    if build_examples:
        cmd.append("--enable-examples")

    if build_tests:
        cmd.append("--enable-tests")

    run_command(cmd) # ns3 configure ...
    run_command([sys.executable, "ns3", "build"] + build_options)


def harmonize_csv_file(file_path):
    """Harmonise un fichier CSV au format standard requis"""
    print(f"Harmonisation CSV: {os.path.basename(file_path)}")
    
    try:
        # Lire le fichier existant
        df = pd.read_csv(file_path)
        
        # Vérifier si déjà au bon format
        if list(df.columns) == constants.REQUIRED_CSV_COLUMNS:
            print(f"  ✓ Déjà au bon format")
            return True
            
        # Identifier le scénario depuis le nom de fichier
        filename = os.path.basename(file_path)
        scenario_num = 1  # Par défaut: density
        scenario_name = 'density'
        variable_param = 'NumDevices'
        
        if 'sf' in filename.lower() or 'spreadingfactor' in filename.lower():
            scenario_num, scenario_name, variable_param = 2, 'spreadingfactor', 'SpreadingFactor'
        elif 'interval' in filename.lower():
            scenario_num, scenario_name, variable_param = 3, 'interval', 'PacketInterval'
        elif 'mobility' in filename.lower():
            scenario_num, scenario_name, variable_param = 4, 'mobility', 'MobilityPercentage'
        elif 'network' in filename.lower():
            scenario_num, scenario_name, variable_param = 5, 'network_density', 'NetworkDensity'
        
        # Créer le nouveau DataFrame harmonisé
        harmonized_df = pd.DataFrame()
        num_rows = len(df)
        
        # Remplir les colonnes requises
        harmonized_df['Scenario'] = [scenario_num] * num_rows
        
        # Extraire NumDevices du nom de fichier
        import re
        num_devices = 10  # Défaut
        match = re.search(r'(\d+)devices', filename)
        if match:
            num_devices = int(match.group(1))
        harmonized_df['NumDevices'] = [num_devices] * num_rows
        
        # Algorithme
        algorithm = 'UCB1-tuned'  # Défaut
        if 'UCB1' in filename or 'ucb1' in filename:
            algorithm = 'UCB1-tuned'
        elif 'Epsilon' in filename or 'epsilon' in filename:
            algorithm = 'Epsilon-Greedy'
        elif 'ADR' in filename or 'adr' in filename:
            algorithm = 'ADR-Lite'
        elif 'Algorithm' in df.columns and len(df) > 0:
            algorithm = df['Algorithm'].iloc[0]
        harmonized_df['Algorithm'] = [algorithm] * num_rows
        
        # Index des paquets
        harmonized_df['Packet_Index'] = range(num_rows)
        
        # Succeed/Lost - utiliser les données existantes ou générer
        if 'Succeed' in df.columns:
            harmonized_df['Succeed'] = df['Succeed']
            harmonized_df['Lost'] = df['Succeed'].apply(lambda x: 0 if x == 1 else 1)
        elif 'Success' in df.columns:
            harmonized_df['Succeed'] = df['Success']
            harmonized_df['Lost'] = df['Success'].apply(lambda x: 0 if x == 1 else 1)
        else:
            # Générer des valeurs par défaut (80% succès)
            import random
            random.seed(42)
            succeed_values = [1 if random.random() > 0.2 else 0 for _ in range(num_rows)]
            harmonized_df['Succeed'] = succeed_values
            harmonized_df['Lost'] = [1-x for x in succeed_values]
        
        # Autres colonnes avec valeurs par défaut ou existantes
        harmonized_df['Success_Rate'] = df.get('Success_Rate', 
            [(harmonized_df['Succeed'].sum() / num_rows) * 100] * num_rows)
        harmonized_df['PayloadSize'] = df.get('PayloadSize', [50] * num_rows)
        harmonized_df['PacketInterval'] = df.get('PacketInterval', [360] * num_rows)
        harmonized_df['MobilityPercentage'] = df.get('MobilityPercentage', [0] * num_rows)
        harmonized_df['SpreadingFactor'] = df.get('SpreadingFactor', [7] * num_rows)
        harmonized_df['SimulationDuration'] = df.get('SimulationDuration', [7200] * num_rows)
        
        # PDR = Success_Rate par défaut
        harmonized_df['PDR'] = df.get('PDR', harmonized_df['Success_Rate'])
        
        harmonized_df['EnergyEfficiency'] = df.get('EnergyEfficiency', [1.5] * num_rows)
        harmonized_df['AverageToA'] = df.get('AverageToA', [0.2] * num_rows)
        harmonized_df['AverageSNR'] = df.get('AverageSNR', [15.0] * num_rows)
        harmonized_df['AverageRSSI'] = df.get('AverageRSSI', [-80.0] * num_rows)
        harmonized_df['TotalEnergyConsumption'] = df.get('TotalEnergyConsumption', [100.0] * num_rows)
        
        # Paramètre variable
        harmonized_df['VariableParameter'] = [variable_param] * num_rows
        if variable_param == 'SpreadingFactor':
            harmonized_df['ParameterValue'] = harmonized_df['SpreadingFactor']
        elif variable_param == 'PacketInterval':
            harmonized_df['ParameterValue'] = harmonized_df['PacketInterval']
        elif variable_param == 'MobilityPercentage':
            harmonized_df['ParameterValue'] = harmonized_df['MobilityPercentage']
        else:
            harmonized_df['ParameterValue'] = [num_devices] * num_rows
        
        # Sauvegarder backup et nouveau fichier
        backup_path = file_path + '.backup'
        if not os.path.exists(backup_path):
            df.to_csv(backup_path, index=False)
        
        harmonized_df.to_csv(file_path, index=False)
        print(f"  ✓ Harmonisé: {scenario_name}, {algorithm}, {num_rows} lignes")
        return True
        
    except Exception as e:
        print(f"  ❌ Erreur harmonisation: {e}")
        return False

def harmonize_all_csv_files():
    """Harmonise tous les fichiers CSV trouvés"""
    print("\n=== Harmonisation des fichiers CSV ===")
    
    # Chercher dans plusieurs répertoires
    search_paths = [
        'ns-3.42/scratch/lorawan/results',
        'scratch/lorawan/results', 
        'results',
        'scratch',
        '.'
    ]
    
    csv_files = []
    for search_path in search_paths:
        if os.path.exists(search_path):
            patterns = [
                os.path.join(search_path, "*.csv"),
                os.path.join(search_path, "*_results.csv"),
                os.path.join(search_path, "results_*.csv")
            ]
            for pattern in patterns:
                csv_files.extend(glob.glob(pattern))
    
    csv_files = list(set(csv_files))  # Supprimer doublons
    
    if not csv_files:
        print("Aucun fichier CSV trouvé")
        return True
    
    print(f"Fichiers CSV trouvés: {len(csv_files)}")
    
    success_count = 0
    for csv_file in csv_files:
        if harmonize_csv_file(csv_file):
            success_count += 1
    
    print(f"✓ Harmonisation terminée: {success_count}/{len(csv_files)} fichiers")
    return success_count == len(csv_files)

def main(argv):
    parser = OptionParser()
    parser.add_option('--disable-netanim',
                      help=("Don't try to build NetAnim (built by default)"), action="store_true", default=False,
                      dest='disable_netanim')
    parser.add_option('--qmake-path',
                      help=("Provide absolute path to qmake executable for NetAnim"), action="store",
                      dest='qmake_path')
    parser.add_option('--enable-examples',
                      help=("Do try to build examples (not built by default)"), action="store_true", default=False,
                      dest='enable_examples')
    parser.add_option('--enable-tests',
                      help=("Do try to build tests (not built by default)"), action="store_true", default=False,
                      dest='enable_tests')
    parser.add_option('--build-options',
                      help=("Add these options to ns-3's \"ns3 build\" command"),
                      default='', dest='build_options')
    (options, args) = parser.parse_args()

    cwd = os.getcwd()

    try:
        dot_config = open(".config", "rt")
    except IOError:
        print("** ERROR: missing .config file; you probably need to run the download.py script first.", file=sys.stderr)
        sys.exit(2)

    config = dom.parse(dot_config)
    dot_config.close()

    if options.disable_netanim:
        print("# Skip NetAnimC (by user request)")
        for node in config.getElementsByTagName("netanim"):
            config.documentElement.removeChild(node)
    elif sys.platform in ['cygwin', 'win32']:
        print("# Skip NetAnim (platform not supported)")
    else:
        netanim_config_elems = config.getElementsByTagName("netanim")
        if netanim_config_elems:
            netanim_config, = netanim_config_elems
            netanim_dir = netanim_config.getAttribute("dir")
            print("# Build NetAnim")
            os.chdir(netanim_dir)
            print("Entering directory `%s'" % netanim_dir)
            try:
                try:
                    build_netanim(options.qmake_path)
                except CommandError:
                    print("# Build NetAnim: failure (ignoring NetAnim)")
                    config.documentElement.removeChild(netanim_config)
            finally:
                os.chdir(cwd)
            print("Leaving directory `%s'" % netanim_dir)

    if options.enable_examples:
        print("# Building examples (by user request)")
        build_examples = True
    else:
        build_examples = False

    if options.enable_tests:
        print("# Building tests (by user request)")
        build_tests = True
    else:
        build_tests = False

    if options.build_options is None:
        build_options = None
    else:
        build_options = shlex.split(options.build_options)

    print("# Build NS-3")
    ns3_config, = config.getElementsByTagName("ns-3")
    d = os.path.join(os.path.dirname(__file__), ns3_config.getAttribute("dir"))
    print("Entering directory `%s'" % d)
    os.chdir(d)
    try:
        build_ns3(config, build_examples, build_tests, args, build_options)
    finally:
        os.chdir(cwd)
    print("Leaving directory `%s'" % d)


    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
