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
