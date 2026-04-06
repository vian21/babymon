# Requirements

| \#  | Requirements                                                                                    | Type (F, N) |
| :-: | ----------------------------------------------------------------------------------------------- | :---------: |
|  1  | Le système doit **mesurer la température corporelle du bébé**.                                  |      F      |
|  2  | Le système doit **mesurer la fréquence cardiaque du bébé**.                                     |      F      |
|  3  | Le système doit **mesurer la saturation en oxygène du sang**.                                   |      F      |
|  4  | Le système doit **mesurer le niveau du CO2 dans la chambre**                                    |      F      |
|  4  | Le système doit **détecter les mouvements du bébé**.                                            |      F      |
|  5  | Le système doit **détecter les pleurs du bébé**                                                 |      F      |
|  6  | Le système doit **mesurer la température ambiante de la pièce**.                                |      F      |
|  8  | Le système doit **détecter la présence de fumée ou d’incendie**.                                |      F      |
| 10  | Le système doit **activer le chauffage si la température ambiante est trop basse**.             |      F      |
| 11  | Le système doit **activer une alarme en cas de fumée ou d'incendie**.                           |      F      |
| 12  | Le système doit **activer un ventilateur si la température est trop élevée**.                   |      F      |
| 13  | Le système doit **envoyer des notifications ou SMS aux utilisateurs en cas d’anomalie**.        |      F      |
| 14  | Le système doit **réagir en moins d’une seconde aux anomalies détectées**.                      |      N      |
| 15  | Le système doit **supporter l’exécution concurrente de plusieurs tâches**.                      |      N      |
| 16  | Le système doit **détecter en moins de 10ms les situations dangereuses**.                       |      N      |
| 17  | Les alertes envoyées aux utilisateurs doivent être **fiables et immédiates**.                   |      N      |
| 18  | Les capteurs doivent fournir **des mesures précises des signes vitaux et de l’environnement**.  |      N      |
| 19  | Le système doit **fonctionner en continu (24h/24)** pour surveiller le bébé pendant le sommeil. |      N      |
| 20  | Le système embarqué doit **consommer peu d’énergie**                                            |      N      |
| 21  | le système utilisera FreeRTOS                                                                   |      F      |
| 22  | le système utilisera un ESP32 comme processeur                                                  |      F      |
| 23  | Le système doit **envoyer un SMS si la température corporelle est en dessous de 36 °C**.        |      F      |
| 24  | Le système doit **activer le ventilateur et alerter par SMS si la temp. corporelle > 37,5 °C**. |      F      |
| 25  | Le système doit **émettre un avertissement si aucun mouvement n'est détecté de 40 à 60 min**.   |      F      |
| 26  | Le système doit **envoyer un SMS si aucun mouvement n'est détecté après 60 min**.               |      F      |
| 27  | Le système doit **envoyer un SMS si la fréquence cardiaque est sous 120 ou au-dessus de 180**.  |      F      |
| 28  | Le système doit **émettre un avertissement pour la saturation en oxygène**.                     |      F      |
| 29  | Le système doit **émettre un avertissement en cas de pleurs (sons)**.                           |      F      |
| 30  | Le système doit **allumer le chauffage si la température ambiante est en dessous de 20 °C**.    |      F      |
| 31  | Le système doit **allumer le ventilateur si la température ambiante est au-dessus de 22,2 °C**. |      F      |
| 32  | Le système doit **activer l'alarme en cas de détection de fumée d'incendie**.                   |      F      |
| 33  | Le système doit **allumer la lumière du sommeil si le bébé bouge**.                             |      F      |
| 34  | Le système doit **éteindre la lumière du sommeil si le bébé ne bouge pas**.                     |      F      |
| 35  | Le système doit **activer le déshumidificateur si l'humidité est à plus de 55 %**.              |      F      |
| 36  | Le système doit **signaler de changer le filtre à air en cas de particules**.                   |      F      |
