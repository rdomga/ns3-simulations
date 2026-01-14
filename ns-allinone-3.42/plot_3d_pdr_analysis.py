"""
Script Python pour l'analyse 3D du PDR (Packet Delivery Ratio) des 4 solutions:
- D-LoRa
- UCB1-tuned 
- QoC-A
- ToW (LoRaWAN)

Analyse en fonction de:
- Mobilité des nœuds (scenario 4)
- Densité du réseau (scenario 5) 
- Charge du trafic (scenario 1 - nombre de dispositifs)
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import os
import glob
import seaborn as sns
from scipy.interpolate import griddata
import warnings
warnings.filterwarnings('ignore')

class PDRAnalyzer3D:
    def __init__(self):
        self.data_paths = {
            'qoca': '/home/bonice/Bureau/knowledge/ns-allinone-3.42/ns-3.42/scratch/qoca_results',
            'lorawan': '/home/bonice/Bureau/knowledge/ns-allinone-3.42/ns-3.42/scratch/lorawan/results', 
            'dlora': '/home/bonice/Bureau/knowledge/ns-allinone-3.42/ns-3.42/scratch/dlora/results',
            'tow': '/home/bonice/Bureau/knowledge/ns-allinone-3.42/ns-3.42/results',
        }
        self.df_combined = pd.DataFrame()
        
    def extract_scenario_info(self, filename, algorithm, row_data=None):
        """
        info = {
            'algorithm': algorithm,
            'scenario': None,
            'mobility': None,
            'density': None,
            'traffic_load': None,
            'filename': filename
        }
        
        # Si on a les données de la ligne, essayer d'extraire directement
        if row_data is not None:
            # Extraire la mobilité depuis les données ou le nom de fichier
            if 'MobilityPercentage' in row_data:
                info['mobility'] = float(row_data['MobilityPercentage'])
            
            # Extraire le nombre de dispositifs/nœuds
            if 'NumDevices' in row_data:
                info['traffic_load'] = int(row_data['NumDevices'])
                info['density'] = int(row_data['NumDevices'])  # On utilise aussi pour la densité
        
        # Extraction depuis le nom de fichier comme fallback
        if 'S1' in filename:
            info['scenario'] = 'traffic_load'
            if info['traffic_load'] is None:
                for num in ['50', '200', '400', '700', '1000']:
                    if f'{num}devices' in filename:
                        info['traffic_load'] = int(num)
                        break
                        
        elif 'S4' in filename:
            info['scenario'] = 'mobility'
            if info['mobility'] is None:
                for pct in ['0', '25', '50', '75', '100']:
                    if f'{pct}pct' in filename:
                        info['mobility'] = int(pct)
                        break
                        
        elif 'S5' in filename:
            info['scenario'] = 'density'
            if info['density'] is None:
                for num in ['50', '100', '200', '300', '400']:
                    if f'{num}nodes' in filename or f'{num}devices' in filename or f'_{num}_' in filename or f'{num}n' in filename:
                        info['density'] = int(num)
                        break
                        break
        
        # Valeurs par défaut pour les dimensions manquantes
        if info['mobility'] is None:
            info['mobility'] = 0  # Par défaut pas de mobilité
        if info['density'] is None and info['traffic_load'] is not None:
            info['density'] = info['traffic_load']  # Utiliser la charge comme densité
        if info['traffic_load'] is None and info['density'] is not None:
            info['traffic_load'] = info['density']  # Utiliser la densité comme charge
                    
        return info
    
    def load_qoca_data(self):
        """Charger les données QoC-A"""
        qoca_files = glob.glob(os.path.join(self.data_paths['qoca'], '*summary.csv'))
        qoca_data = []
        
        for file in qoca_files:
            if 'stationary' in file:  # On prend les données stationnaires
                try:
                    df = pd.read_csv(file)
                    if 'QoC-A' in df['Algorithm'].values:
                        qoca_row = df[df['Algorithm'] == 'QoC-A'].iloc[0]
                        
                        info = self.extract_scenario_info(os.path.basename(file), 'QoC-A', qoca_row)
                        
                        record = {
                            'algorithm': 'QoC-A',
                            'pdr': qoca_row['PDR'],
                            'energy_efficiency': qoca_row['EnergyEfficiency'],
                            'mobility': info['mobility'],
                            'density': info['density'], 
                            'traffic_load': info['traffic_load'],
                            'scenario': info['scenario'],
                            'source_file': file
                        }
                        qoca_data.append(record)
                        print(f"QoC-A: {os.path.basename(file)} - PDR: {qoca_row['PDR']:.4f}, Mobilité: {info['mobility']}, Densité: {info['density']}, Charge: {info['traffic_load']}")
                except Exception as e:
                    print(f"Erreur lors du chargement de {file}: {e}")
                    
        return pd.DataFrame(qoca_data)
    
    def load_lorawan_data(self):
        """Charger les données LoRaWAN (UCB1-tuned et TWO)"""
        lorawan_files = glob.glob(os.path.join(self.data_paths['lorawan'], '*.csv'))
        lorawan_data = []
        
        for file in lorawan_files:
            filename = os.path.basename(file)
            
            # Déterminer l'algorithme
            if 'UCB1-tuned' in filename:
                algorithm = 'UCB1-tuned'
            elif 'ToW' in filename:
                algorithm = 'ToW'
            else:
                continue
                
            try:
                df = pd.read_csv(file)
                if not df.empty:
                    # Prendre la première ligne avec les métriques agrégées
                    row = df.iloc[0]
                    
                    info = self.extract_scenario_info(filename, algorithm, row)
                    
                    record = {
                        'algorithm': algorithm,
                        'pdr': row['PDR'],
                        'energy_efficiency': row['EnergyEfficiency'],
                        'mobility': info['mobility'],
                        'density': info['density'],
                        'traffic_load': info['traffic_load'], 
                        'scenario': info['scenario'],
                        'source_file': file
                    }
                    lorawan_data.append(record)
                    print(f"{algorithm}: {filename} - PDR: {row['PDR']:.4f}, Mobilité: {info['mobility']}, Densité: {info['density']}, Charge: {info['traffic_load']}")
            except Exception as e:
                print(f"Erreur lors du chargement de {file}: {e}")
                
        return pd.DataFrame(lorawan_data)
    
    def load_dlora_data(self):
        """Charger les données D-LoRa"""
        dlora_files = glob.glob(os.path.join(self.data_paths['dlora'], 'scenario*_DLoRa_*.csv'))
        dlora_data = []
        
        for file in dlora_files:
            filename = os.path.basename(file)
            
            try:
                df = pd.read_csv(file)
                if not df.empty:
                    row = df.iloc[0]
                    
                    info = self.extract_scenario_info(filename, 'D-LoRa', row)
                    
                    record = {
                        'algorithm': 'D-LoRa',
                        'pdr': row['PDR'],
                        'energy_efficiency': row['EnergyEfficiency'],
                        'mobility': info['mobility'],
                        'density': info['density'],
                        'traffic_load': info['traffic_load'],
                        'scenario': info['scenario'],
                        'source_file': file
                    }
                    dlora_data.append(record)
                    print(f"D-LoRa: {filename} - PDR: {row['PDR']:.4f}, Mobilité: {info['mobility']}, Densité: {info['density']}, Charge: {info['traffic_load']}")
            except Exception as e:
                print(f"Erreur lors du chargement de {file}: {e}")
                
        return pd.DataFrame(dlora_data)
    
    def load_tow_data(self):
        """Charger les données ToW depuis le dossier results"""
        tow_files = glob.glob(os.path.join(self.data_paths['tow'], 'results_ToW_*.csv'))
        tow_data = []
        
        for file in tow_files:
            filename = os.path.basename(file)
            
            try:
                df = pd.read_csv(file)
                if not df.empty:
                    row = df.iloc[0]
                    
                    info = self.extract_scenario_info(filename, 'ToW', row)
                    
                    record = {
                        'algorithm': 'ToW',
                        'pdr': row['PDR'],
                        'energy_efficiency': row['EnergyEfficiency'],
                        'mobility': info['mobility'],
                        'density': info['density'],
                        'traffic_load': info['traffic_load'],
                        'scenario': info['scenario'],
                        'source_file': file
                    }
                    tow_data.append(record)
                    print(f"ToW: {filename} - PDR: {record['pdr']:.4f}, Mobilité: {info['mobility']}, Densité: {info['density']}, Charge: {info['traffic_load']}")
            except Exception as e:
                print(f"Erreur lors du chargement de {file}: {e}")
                    
        return pd.DataFrame(tow_data)
    
    def load_all_data(self):
        """Charger toutes les données des 4 solutions"""
        print("Chargement des données QoC-A...")
        qoca_df = self.load_qoca_data()
        print(f"QoC-A: {len(qoca_df)} enregistrements")
        
        print("Chargement des données LoRaWAN...")
        lorawan_df = self.load_lorawan_data()
        print(f"LoRaWAN: {len(lorawan_df)} enregistrements")
        
        print("Chargement des données D-LoRa...")
        dlora_df = self.load_dlora_data()
        print(f"D-LoRa: {len(dlora_df)} enregistrements")
        
        print("Chargement des données ToW...")
        tow_df = self.load_tow_data()
        print(f"ToW: {len(tow_df)} enregistrements")
        
        # Combiner toutes les données
        self.df_combined = pd.concat([qoca_df, lorawan_df, dlora_df, tow_df], ignore_index=True)
        
        # Debug: afficher les données avant nettoyage
        print(f"\nAvant nettoyage: {len(self.df_combined)} enregistrements")
        print("Colonnes disponibles:", self.df_combined.columns.tolist())
        if not self.df_combined.empty:
            print("Premières lignes:")
            print(self.df_combined[['algorithm', 'pdr', 'mobility', 'density', 'traffic_load']].head())
        
        # Nettoyer les données avec plus de flexibilité
        self.df_combined = self.df_combined.dropna(subset=['pdr'])
        self.df_combined = self.df_combined[self.df_combined['pdr'] > 0]  # Filtrer les PDR valides
        
        # Remplir les valeurs manquantes avec des valeurs par défaut
        self.df_combined['mobility'] = self.df_combined['mobility'].fillna(0)
        self.df_combined['density'] = self.df_combined['density'].fillna(self.df_combined['traffic_load'])
        self.df_combined['traffic_load'] = self.df_combined['traffic_load'].fillna(self.df_combined['density'])
        
        # Supprimer les lignes où toutes les dimensions sont manquantes
        self.df_combined = self.df_combined.dropna(subset=['mobility', 'density', 'traffic_load'], how='all')
        
        # Valider les valeurs de densité
        self.validate_density_values()
        
        print(f"\nDonnées combinées: {len(self.df_combined)} enregistrements")
        print("Algorithmes disponibles:", self.df_combined['algorithm'].unique())
        
        return self.df_combined
    
    def validate_density_values(self):
        '''Valider et corriger les valeurs de densité pour qu'elles respectent [50, 100, 200, 300, 400]'''
        valid_densities = [50, 100, 200, 300, 400]
        
        print("Validation des valeurs de densité...")
        original_count = len(self.df_combined)
        
        # Mapper les valeurs proches aux valeurs exactes
        def map_to_valid_density(density):
            if pd.isna(density):
                return None
            
            # Si c'est déjà une valeur valide, la garder
            if density in valid_densities:
                return density
            
            # Sinon, mapper à la valeur la plus proche
            closest = min(valid_densities, key=lambda x: abs(x - density))
            if abs(closest - density) <= 25:  # Tolérance de 25
                return closest
            
            return None
        
        # Appliquer le mapping
        self.df_combined['density'] = self.df_combined['density'].apply(map_to_valid_density)
        
        # Supprimer les lignes avec des densités non valides
        self.df_combined = self.df_combined.dropna(subset=['density'])
        
        print(f"Avant validation: {original_count} enregistrements")
        print(f"Après validation: {len(self.df_combined)} enregistrements")
        print(f"Valeurs de densité utilisées: {sorted(self.df_combined['density'].unique())}")
        
        # Vérifier que toutes les valeurs sont valides
        invalid_densities = [d for d in self.df_combined['density'].unique() if d not in valid_densities]
        if invalid_densities:
            print(f"ATTENTION: Valeurs de densité non valides détectées: {invalid_densities}")
        else:
            print("✓ Toutes les valeurs de densité sont valides: [50, 100, 200, 300, 400]")
        
        # Debug: vérifier les valeurs de densité après validation
        self.debug_density_values()
        
        return self.df_combined
    
    def debug_density_values(self):
        """Afficher les valeurs de densité pour debug"""
        print("\n=== DEBUG VALEURS DE DENSITÉ ===")
        density_values = sorted(self.df_combined['density'].unique())
        print(f"Valeurs de densité trouvées: {density_values}")
        
        for alg in self.df_combined['algorithm'].unique():
            alg_data = self.df_combined[self.df_combined['algorithm'] == alg]
            alg_densities = sorted(alg_data['density'].unique())
            print(f"{alg}: densités = {alg_densities}")
        
        # Vérifier si toutes les valeurs sont dans [50, 100, 200, 300, 400]
        valid_densities = [50, 100, 200, 300, 400]
        invalid = [d for d in density_values if d not in valid_densities]
        if invalid:
            print(f"ATTENTION: Valeurs invalides détectées: {invalid}")
        else:
            print("✓ Toutes les valeurs de densité sont valides")
        print("=" * 35)

    def create_3d_scatter_plot(self):
        """Créer un graphique 3D scatter plot"""
        fig = plt.figure(figsize=(15, 10))
        ax = fig.add_subplot(111, projection='3d')
        
        algorithms = ['QoC-A', 'UCB1-tuned', 'D-LoRa', 'ToW']
        colors = ['gold', 'lightcoral', 'teal', 'purple']
        markers = ['o', '^', 's', 'D']
        
        for i, alg in enumerate(algorithms):
            data = self.df_combined[self.df_combined['algorithm'] == alg]
            
            if not data.empty:
                scatter = ax.scatter(data['traffic_load'], data['density'], data['mobility'], 
                          c=data['pdr'], cmap='viridis', 
                          s=100, alpha=0.8, label=alg, marker=markers[i])
        
        ax.set_xlabel('Charge du Trafic (Nombre de Dispositifs)', fontsize=12)
        ax.set_ylabel('Densité du Réseau (Nœuds)', fontsize=12)
        ax.set_zlabel('Mobilité (%)', fontsize=12)
        ax.set_title('Analyse 3D du PDR des 4 Solutions LoRaWAN', fontsize=14)
        
        # Forcer les valeurs d'axe pour la densité
        ax.set_yticks([50, 100, 200, 300, 400])
        ax.set_yticklabels(['50', '100', '200', '300', '400'])
        
        # Ajouter une barre de couleur pour le PDR
        if not self.df_combined.empty:
            scatter = ax.scatter([], [], [], c=[], cmap='viridis')
            cbar = plt.colorbar(scatter, ax=ax, shrink=0.5)
            cbar.set_label('PDR (Packet Delivery Ratio)', fontsize=12)
        
        ax.legend()
        plt.tight_layout()
        plt.savefig('pdr_3d_analysis_scatter.png', dpi=300, bbox_inches='tight')
        plt.show()
    
    def create_3d_surface_plots(self):
        """Créer des graphiques de surface 3D pour chaque algorithme"""
        algorithms = self.df_combined['algorithm'].unique()
        
        fig, axes = plt.subplots(2, 2, figsize=(20, 15), subplot_kw={'projection': '3d'})
        axes = axes.flatten()
        
        for i, alg in enumerate(algorithms):
            if i >= len(axes):
                break
                
            data = self.df_combined[self.df_combined['algorithm'] == alg]
            
            if len(data) >= 3:  # Au moins 3 points pour la visualisation
                x = data['traffic_load'].values
                y = data['density'].values  
                z = data['mobility'].values
                pdr = data['pdr'].values
                
                # Créer un scatter plot 3D pour chaque algorithme
                scatter = axes[i].scatter(x, y, z, c=pdr, cmap='viridis', s=100, alpha=0.8)
                
                axes[i].set_xlabel('Charge du Trafic (Dispositifs)')
                axes[i].set_ylabel('Densité du Réseau (Nœuds)')
                axes[i].set_zlabel('Mobilité (%)')
                axes[i].set_title(f'{alg} - Distribution PDR 3D')
                
                # Ajouter une barre de couleur
                fig.colorbar(scatter, ax=axes[i], shrink=0.5, label='PDR')
            else:
                axes[i].text(0.5, 0.5, 0.5, f'Données insuffisantes\npour {alg}', 
                           transform=axes[i].transAxes, ha='center', va='center')
                axes[i].set_title(f'{alg} - Pas assez de données')
        
        plt.tight_layout()
        plt.savefig('pdr_3d_surfaces_by_algorithm.png', dpi=300, bbox_inches='tight')
        plt.show()
    
    def create_comparison_plots(self):
        """Créer des graphiques de comparaison entre algorithmes"""
        
        # 1. Graphique en barres par scénario
        fig, axes = plt.subplots(1, 3, figsize=(18, 6))
        
        scenarios = ['traffic_load', 'density', 'mobility']
        scenario_names = ['Charge du Trafic', 'Densité du Réseau', 'Mobilité']
        
        for i, (scenario, name) in enumerate(zip(scenarios, scenario_names)):
            scenario_data = self.df_combined[self.df_combined['scenario'] == scenario]
            
            if not scenario_data.empty:
                pivot_data = scenario_data.groupby(['algorithm', scenario])['pdr'].mean().unstack(fill_value=0)
                
                pivot_data.plot(kind='bar', ax=axes[i], width=0.8)
                axes[i].set_title(f'PDR Moyen par {name}')
                axes[i].set_ylabel('PDR')
                axes[i].legend(title=name, bbox_to_anchor=(1.05, 1), loc='upper left')
                axes[i].tick_params(axis='x', rotation=45)
        
        plt.tight_layout()
        plt.savefig('pdr_comparison_by_scenario.png', dpi=300, bbox_inches='tight')
        plt.show()
        
        # 2. Heatmap de performance
        fig, ax = plt.subplots(figsize=(12, 8))
        
        # Créer une matrice de performance moyenne
        perf_matrix = self.df_combined.groupby(['algorithm'])['pdr'].agg(['mean', 'std']).round(4)
        
        # Ajouter l'efficacité énergétique
        energy_matrix = self.df_combined.groupby(['algorithm'])['energy_efficiency'].agg(['mean', 'std']).round(4)
        
        combined_matrix = pd.concat([perf_matrix, energy_matrix], axis=1)
        combined_matrix.columns = ['PDR_Mean', 'PDR_Std', 'Energy_Mean', 'Energy_Std']
        
        sns.heatmap(combined_matrix, annot=True, cmap='viridis', ax=ax)
        ax.set_title('Matrice de Performance des Algorithmes')
        plt.tight_layout()
        plt.savefig('performance_heatmap.png', dpi=300, bbox_inches='tight')
        plt.show()
    
    def generate_statistics_report(self):
        """Générer un rapport statistique détaillé"""
        print("\n" + "="*60)
        print("RAPPORT STATISTIQUE - ANALYSE 3D DU PDR")
        print("="*60)
        
        # Statistiques générales
        print(f"\nNombre total d'enregistrements: {len(self.df_combined)}")
        print(f"Algorithmes analysés: {', '.join(self.df_combined['algorithm'].unique())}")
        
        # Statistiques par algorithme
        print("\n--- PERFORMANCE PAR ALGORITHME ---")
        stats_by_alg = self.df_combined.groupby('algorithm').agg({
            'pdr': ['mean', 'std', 'min', 'max'],
            'energy_efficiency': ['mean', 'std'],
            'traffic_load': ['min', 'max'],
            'density': ['min', 'max'],
            'mobility': ['min', 'max']
        }).round(4)
        
        for alg in self.df_combined['algorithm'].unique():
            alg_data = self.df_combined[self.df_combined['algorithm'] == alg]
            print(f"\n{alg}:")
            print(f"  PDR Moyen: {alg_data['pdr'].mean():.4f} ± {alg_data['pdr'].std():.4f}")
            print(f"  PDR Min/Max: {alg_data['pdr'].min():.4f} / {alg_data['pdr'].max():.4f}")
            print(f"  Efficacité Énergétique: {alg_data['energy_efficiency'].mean():.4f}")
            print(f"  Nombre d'échantillons: {len(alg_data)}")
        
        # Identification du meilleur algorithme par dimension
        print("\n--- MEILLEUR ALGORITHME PAR DIMENSION ---")
        best_pdr = self.df_combined.loc[self.df_combined['pdr'].idxmax()]
        print(f"Meilleur PDR: {best_pdr['algorithm']} (PDR: {best_pdr['pdr']:.4f})")
        
        best_energy = self.df_combined.loc[self.df_combined['energy_efficiency'].idxmax()]
        print(f"Meilleure Efficacité Énergétique: {best_energy['algorithm']} (EE: {best_energy['energy_efficiency']:.4f})")
        
        # Sauvegarder les statistiques
        stats_by_alg.to_csv('pdr_3d_statistics.csv')
        print(f"\nStatistiques sauvegardées dans 'pdr_3d_statistics.csv'")
    
    def plot_2d_scenario_curves(self):
        """Tracer les courbes 2D pour chaque scénario et chaque algorithme"""
        scenarios = [
            # (nom interne, abscisse, label abscisse, label ordonnée)
            ('traffic_load', 'traffic_load', 'Nombre de nœuds / Charge du trafic', 'PDR (%)'),
            ('traffic_load', 'traffic_load', 'Nombre de nœuds / Charge du trafic', 'Efficacité énergétique'),
            ('density', 'density', 'Densité du réseau', 'PDR (%)'),
            ('density', 'density', 'Densité du réseau', 'Efficacité énergétique'),
            ('mobility', 'mobility', 'Mobilité des nœuds (%)', 'PDR (%)'),
            ('mobility', 'mobility', 'Mobilité des nœuds (%)', 'Efficacité énergétique'),
        ]
        algorithms = ['QoC-A', 'UCB1-tuned', 'D-LoRa', 'ToW']
        colors = ['gold', 'lightcoral', 'teal', 'purple']
        markers = ['o', '^', 's', 'D']

        for i in range(0, len(scenarios), 2):
            scenario, x_col, x_label, y_label_pdr = scenarios[i]
            _, _, _, y_label_ee = scenarios[i+1]
            fig, axes = plt.subplots(1, 2, figsize=(14, 6))
            # PDR
            for alg, color, marker in zip(algorithms, colors, markers):
                data = self.df_combined[(self.df_combined['algorithm'] == alg) & (self.df_combined['scenario'] == scenario)]
                if not data.empty:
                    axes[0].plot(data[x_col], data['pdr'], label=alg, color=color, marker=marker, linewidth=2)
            axes[0].set_xlabel(x_label)
            axes[0].set_ylabel(y_label_pdr)
            axes[0].set_title(f'{y_label_pdr} en fonction de {x_label}')
            axes[0].legend()
            axes[0].grid(True, alpha=0.3)
            # Efficacité énergétique
            for alg, color, marker in zip(algorithms, colors, markers):
                data = self.df_combined[(self.df_combined['algorithm'] == alg) & (self.df_combined['scenario'] == scenario)]
                if not data.empty:
                    axes[1].plot(data[x_col], data['energy_efficiency'], label=alg, color=color, marker=marker, linewidth=2)
            axes[1].set_xlabel(x_label)
            axes[1].set_ylabel(y_label_ee)
            axes[1].set_title(f'{y_label_ee} en fonction de {x_label}')
            axes[1].legend()
            axes[1].grid(True, alpha=0.3)
            plt.tight_layout()
            plt.savefig(f'courbes_{scenario}_{x_col}.png', dpi=300, bbox_inches='tight')
            plt.show()
    
    def run_complete_analysis(self):
        """Exécuter l'analyse complète"""
        print("Démarrage de l'analyse 3D du PDR...")
        # Charger les données
        self.load_all_data()
        if self.df_combined.empty:
            print("Aucune donnée trouvée. Vérifiez les chemins des fichiers.")
            return
        # Debug des valeurs de densité
        self.debug_density_values()
        # Générer les graphiques
        print("\nGénération des graphiques...")
        self.create_3d_scatter_plot()
        self.create_3d_surface_plots() 
        self.create_comparison_plots()
        self.plot_2d_scenario_curves()  # Ajout des courbes 2D demandées
        # Générer le rapport
        self.generate_statistics_report()
        print("\nAnalyse terminée! Graphiques et rapport générés.")


def main():
    """Fonction principale"""
    analyzer = PDRAnalyzer3D()
    analyzer.run_complete_analysis()


if __name__ == "__main__":
    main()
