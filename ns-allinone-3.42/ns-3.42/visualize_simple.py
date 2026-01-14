import matplotlib.pyplot as plt

# Créer une figure simple
plt.figure()

# Positions des nœuds
gateway_pos = (1000, 0)
end_device_pos = (0, 0)

# Dessiner les nœuds
plt.plot(gateway_pos[0], gateway_pos[1], 'ro', label='Gateway')
plt.plot(end_device_pos[0], end_device_pos[1], 'bo', label='End Device')

# Ajouter des étiquettes
plt.text(gateway_pos[0] + 50, gateway_pos[1], 'Gateway')
plt.text(end_device_pos[0] + 50, end_device_pos[1], 'End Device')

# Configurer l'axe
plt.xlim(-200, 1200)
plt.ylim(-200, 200)
plt.xlabel('Position X (m)')
plt.ylabel('Position Y (m)')
plt.title('Réseau LoRaWAN')
plt.legend()
plt.grid(True)

# Sauvegarder la figure au format PNG
plt.savefig('lorawan_network.png')
