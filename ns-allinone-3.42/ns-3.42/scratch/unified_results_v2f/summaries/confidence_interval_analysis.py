#!/usr/bin/env python3
"""
Script de calcul des intervalles de confiance pour les simulations LoRa
Analyse les résultats par scénario et algorithme sur 5 répétitions
"""

import pandas as pd
import numpy as np
from scipy import stats
import os
import warnings
warnings.filterwarnings('ignore')

# Configuration
CONFIDENCE_LEVEL = 0.95
REPETITIONS = 5
SUMMARIES_DIR = os.path.dirname(os.path.abspath(__file__))

# Métriques à analyser
METRICS = ['PDR', 'Success_Rate', 'EnergyEfficiency', 'AverageToA', 'AverageSNR', 'AverageRSSI', 'TotalEnergyConsumption']


def load_all_repetitions():
    """Charge toutes les répétitions dans un seul DataFrame"""
    dfs = []
    for i in range(1, REPETITIONS + 1):
        filepath = os.path.join(SUMMARIES_DIR, f'summary_rep_{i}.csv')
        if os.path.exists(filepath):
            df = pd.read_csv(filepath)
            df['Repetition'] = i
            dfs.append(df)
            print(f"✓ Chargé: summary_rep_{i}.csv ({len(df)} lignes)")
        else:
            print(f"✗ Fichier manquant: summary_rep_{i}.csv")

    if not dfs:
        raise FileNotFoundError("Aucun fichier de répétition trouvé!")

    return pd.concat(dfs, ignore_index=True)


def calculate_confidence_interval(values, confidence=CONFIDENCE_LEVEL):
    """
    Calcule l'intervalle de confiance pour un ensemble de valeurs

    Returns:
        dict: mean, std, sem, ci_low, ci_high, margin, n
    """
    values = np.array(values)
    n = len(values)
    mean = np.mean(values)
    std = np.std(values, ddof=1) if n > 1 else 0
    sem = std / np.sqrt(n) if n > 0 else 0  # Standard Error of Mean

    if n > 1 and std > 0:
        # Distribution t de Student pour petits échantillons
        t_critical = stats.t.ppf((1 + confidence) / 2, n - 1)
        margin = t_critical * sem
        ci_low = mean - margin
        ci_high = mean + margin
    else:
        margin = 0
        ci_low = mean
        ci_high = mean

    return {
        'mean': mean,
        'std': std,
        'sem': sem,
        'ci_low': ci_low,
        'ci_high': ci_high,
        'margin': margin,
        'n': n
    }


def analyze_by_scenario_algorithm(data, metric='PDR'):
    """
    Analyse une métrique par scénario et algorithme

    Returns:
        DataFrame avec les statistiques pour chaque combinaison
    """
    results = []

    algorithms = sorted(data['Algorithm'].unique())
    scenarios = sorted(data['Scenario'].unique())

    for scenario in scenarios:
        for algo in algorithms:
            # Filtrer les données pour cette combinaison
            subset = data[(data['Algorithm'] == algo) & (data['Scenario'] == scenario)]

            if len(subset) == 0:
                continue

            # Grouper par répétition et calculer la moyenne par répétition
            rep_means = subset.groupby('Repetition')[metric].mean().values

            # Calculer l'IC sur les moyennes des répétitions
            ci_stats = calculate_confidence_interval(rep_means)

            results.append({
                'Scenario': scenario,
                'Algorithm': algo,
                'Metric': metric,
                'N_Repetitions': len(rep_means),
                'Mean': ci_stats['mean'],
                'Std': ci_stats['std'],
                'SEM': ci_stats['sem'],
                'CI_95_Low': ci_stats['ci_low'],
                'CI_95_High': ci_stats['ci_high'],
                'Margin': ci_stats['margin'],
                'CI_String': f"{ci_stats['mean']:.4f} ± {ci_stats['margin']:.4f}"
            })

    return pd.DataFrame(results)


def analyze_detailed(data, metric='PDR'):
    """
    Analyse détaillée par scénario, algorithme et paramètre variable
    """
    results = []

    for scenario in data['Scenario'].unique():
        scenario_data = data[data['Scenario'] == scenario]
        var_param = scenario_data['VariableParameter'].iloc[0]

        for algo in scenario_data['Algorithm'].unique():
            algo_data = scenario_data[scenario_data['Algorithm'] == algo]

            for param_val in algo_data['ParameterValue'].unique():
                subset = algo_data[algo_data['ParameterValue'] == param_val]

                # Moyenne par répétition
                rep_means = subset.groupby('Repetition')[metric].mean().values
                ci_stats = calculate_confidence_interval(rep_means)

                results.append({
                    'Scenario': scenario,
                    'Algorithm': algo,
                    'Variable': var_param,
                    'Value': param_val,
                    'Mean': ci_stats['mean'],
                    'Std': ci_stats['std'],
                    'CI_Low': ci_stats['ci_low'],
                    'CI_High': ci_stats['ci_high'],
                    'Margin': ci_stats['margin']
                })

    return pd.DataFrame(results)


def print_summary_table(df, title):
    """Affiche un tableau formaté"""
    print("\n" + "=" * 100)
    print(f" {title}")
    print("=" * 100)
    print(df.to_string(index=False))
    print()


def generate_latex_table(df, metric='PDR'):
    """Génère une table LaTeX pour publication"""
    latex = "\\begin{table}[htbp]\n"
    latex += "\\centering\n"
    latex += f"\\caption{{Intervalles de confiance à 95\\% pour {metric}}}\n"
    latex += "\\begin{tabular}{llccc}\n"
    latex += "\\hline\n"
    latex += "Scénario & Algorithme & Moyenne & IC 95\\% & Marge \\\\\n"
    latex += "\\hline\n"

    for _, row in df.iterrows():
        latex += f"{row['Scenario']} & {row['Algorithm']} & "
        latex += f"{row['Mean']:.4f} & [{row['CI_95_Low']:.4f}, {row['CI_95_High']:.4f}] & "
        latex += f"$\\pm${row['Margin']:.4f} \\\\\n"

    latex += "\\hline\n"
    latex += "\\end{tabular}\n"
    latex += "\\end{table}\n"

    return latex


def main():
    print("\n" + "=" * 100)
    print(" ANALYSE DES INTERVALLES DE CONFIANCE - SIMULATIONS LoRa")
    print(" Niveau de confiance: 95%")
    print("=" * 100)

    # Charger les données
    print("\n[1] Chargement des données...")
    data = load_all_repetitions()
    print(f"\n   Total: {len(data)} lignes chargées")
    print(f"   Algorithmes: {', '.join(sorted(data['Algorithm'].unique()))}")
    print(f"   Scénarios: {', '.join(sorted(data['Scenario'].unique()))}")

    # Analyse par scénario et algorithme pour PDR
    print("\n[2] Calcul des intervalles de confiance...")

    # Créer un fichier de résultats
    output_results = []

    for metric in ['PDR', 'EnergyEfficiency']:
        print(f"\n   Analyse de: {metric}")

        # Analyse globale
        results_df = analyze_by_scenario_algorithm(data, metric)
        output_results.append(results_df)

        print_summary_table(
            results_df[['Scenario', 'Algorithm', 'Mean', 'Std', 'CI_95_Low', 'CI_95_High', 'Margin']],
            f"INTERVALLES DE CONFIANCE 95% - {metric}"
        )

    # Analyse détaillée
    print("\n[3] Analyse détaillée par paramètre variable...")
    detailed_pdr = analyze_detailed(data, 'PDR')

    # Sauvegarder les résultats
    print("\n[4] Sauvegarde des résultats...")

    # Résultats globaux
    global_results = pd.concat(output_results, ignore_index=True)
    global_output = os.path.join(SUMMARIES_DIR, 'confidence_intervals_global.csv')
    global_results.to_csv(global_output, index=False)
    print(f"   ✓ {global_output}")

    # Résultats détaillés
    detailed_output = os.path.join(SUMMARIES_DIR, 'confidence_intervals_detailed.csv')
    detailed_pdr.to_csv(detailed_output, index=False)
    print(f"   ✓ {detailed_output}")

    # Générer table LaTeX
    latex_output = os.path.join(SUMMARIES_DIR, 'confidence_intervals_latex.tex')
    pdr_results = global_results[global_results['Metric'] == 'PDR']
    with open(latex_output, 'w') as f:
        f.write(generate_latex_table(pdr_results, 'PDR'))
    print(f"   ✓ {latex_output}")

    # Rapport de qualité des données
    print("\n" + "=" * 100)
    print(" RAPPORT DE QUALITÉ DES DONNÉES")
    print("=" * 100)

    zero_variance = global_results[global_results['Std'] == 0]
    if len(zero_variance) > 0:
        print(f"\n⚠️  ATTENTION: {len(zero_variance)} combinaisons ont une variance nulle!")
        print("   Cela signifie que les répétitions ont produit des résultats identiques.")
        print("   Les intervalles de confiance ne sont pas significatifs dans ce cas.")
        print("\n   Combinaisons affectées:")
        for _, row in zero_variance.iterrows():
            print(f"     - {row['Algorithm']} / {row['Scenario']} / {row['Metric']}")
    else:
        print("\n✓ Toutes les combinaisons ont une variance non nulle.")
        print("  Les intervalles de confiance sont statistiquement significatifs.")

    # Tableau récapitulatif par algorithme
    print("\n" + "=" * 100)
    print(" RÉSUMÉ PAR ALGORITHME (PDR)")
    print("=" * 100)

    pdr_summary = global_results[global_results['Metric'] == 'PDR'].groupby('Algorithm').agg({
        'Mean': 'mean',
        'Std': 'mean',
        'Margin': 'mean'
    }).round(4)

    print("\n" + pdr_summary.to_string())

    print("\n" + "=" * 100)
    print(" ANALYSE TERMINÉE")
    print("=" * 100)
    print(f"\nFichiers générés:")
    print(f"  - {global_output}")
    print(f"  - {detailed_output}")
    print(f"  - {latex_output}")


if __name__ == "__main__":
    main()
