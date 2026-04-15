Université d'Ottawa  
Faculté de Génie  
École de Science Informatique et de Génie Électrique

 **CEG 4566 / SEG4545 / CSI4541 - Conception de systèmes informatiques en temps réel**

Mini-Projet (babyMon)

Jeudi, 16 Avril 2026

Groupe :  
(Noms des membres du groupe à remplir)

# Table des matières

1. Page de couverture
2. Table des matières
3. Table des figures
4. Table des tableaux
5. Introduction
6. Description détaillée du projet
7. Analyse du problème et Spécification
8. Liste des composantes matérielles et logicielles
9. Diagrammes et schémas électriques (Hardware)
10. Architecture logicielle, FMS et Algorithmes (Software)
11. Analyse du temps et justification de l'ordonnancement
12. Communication entre les différentes composantes
13. Analyse de sécurité et justification des solutions retenues
14. Tests, validation et résolution de problèmes (Maintenance)
15. Difficultés éventuelles
16. Conclusion
17. Références
18. Annexe

# Table des figures

- Figure 1 : Schéma électrique du système babyMon

# Table des tableaux

- Tableau 1 : Correspondance entre les composants matériels et leurs fonctions
- Tableau 2 : Liste des tâches FreeRTOS et leurs rôles

# 5. Introduction

Le syndrome de mort subite du nourrisson (SMSN) représente une préoccupation majeure pour les nouveaux parents, s'agissant d'un décès inexpliqué survenant généralement pendant le sommeil de bébés en bonne santé de moins d'un an. Ce rapport présente **babyMon**, un système de surveillance embarqué en temps réel visant à réduire ce risque, particulièrement durant le premier mois de vie du nourrisson. 

L'implémentation a été réalisée sur un microcontrôleur ESP32 avec FreeRTOS, en respectant l'exigence du projet d'utiliser un système d'exploitation temps réel pour ordonnancer et gérer concurremment plusieurs tâches critiques. Le système obtenu représente une solution IoT intelligente.

# 6. Description détaillée du projet

Le projet répond au besoin de surveiller la santé, la sécurité et l'environnement d'un bébé. Le système doit :
1. Mesurer automatiquement les signaux vitaux (température du corps, rythme cardiaque, saturation du sang en oxygène, mobilité du bébé).
2. Envoyer une notification par SMS sur le téléphone portable des parents en cas d'urgence.
3. Mesurer l'environnement ambiant (température, humidité, CO2) et démarrer des actionneurs comme un ventilateur, un chauffage ou un déshumidificateur.
4. Détecter la fumée d'incendie dans la pièce et envoyer une alerte réseau (Avertissement/Alarme).
5. Allumer une lumière de sommeil en fonction des mouvements du bébé.

# 7. Analyse du problème et Spécification

Le problème consiste à concevoir un système de santé embarqué capable d'acquérir des données de capteurs divers, d'analyser ces données en temps réel, de communiquer avec le réseau et d'agir sur l'environnement.

## 7.1 Exigences fonctionnelles (Tableau d'intervention)
À partir des directives, les actions suivantes sont spécifiées :
- Température corporelle : < 36°C (Alerte SMS), > 37.5°C (Ventilateur activé, Alerte SMS)
- Fréquence cardiaque : < 120 ou > 180 BPM (Alerte SMS)
- Mouvements du bébé : 40 à 60 min (Avertissement), > 60 min (Alerte SMS), Mouvement (Lumière allumée), Pas de mouvement (Lumière éteinte)
- Bébé qui pleure : Avertissement
- Température ambiante : < 20°C (Chauffage allumé), > 22.2°C (Ventilateur allumé)
- Humidité : > 55% (Déshumidificateur activé)
- Particules (Poussière/Pollen) : Changer filtre à air
- Détection d'incendie : Alarme

# 8. Liste des composantes matérielles et logicielles

**Tableau 1 : Correspondance entre les composants matériels et leurs fonctions**

| Composant | Rôle dans le système |
| :--- | :--- |
| **Microcontrôleur ESP32** | Exécute FreeRTOS, acquiert les données, gère le WiFi et l'API HTTP. |
| **MAX30205 (I2C)** | Mesure précise de la température corporelle de qualité médicale. |
| **MAX30102 (I2C)** | Mesure du pouls et de la saturation en oxygène (SpO2). |
| **BME680 (I2C)** | Surveille l'humidité, la température ambiante et la qualité de l'air. |
| **MH-Z19B (UART)** | Détecte le taux de CO2. |
| **SPH0645 (I2S)** | Microphone MEMS pour la détection audio (pleurs). |
| **Capteur PIR Zigbee** | Surveille la mobilité du bébé à distance. |

*Logiciel :* Framework ESP-IDF, FreeRTOS, bibliothèques I2C/UART/I2S.

# 9. Diagrammes et schémas électriques (Hardware)

Le schéma électrique suivant décrit les connexions entre le microcontrôleur ESP32 et les différents capteurs matériels utilisés dans le projet **babyMon**.

![Schéma électrique du système babyMon](Babymon%20schematics.png)
**Figure 1 : Schéma électrique du système babyMon**

**Connexions matérielles principales :**
- **Bus I2C (SCL = IO18, SDA = IO19) :** MAX30205MTA, MAX30102, BME680.
- **Bus UART :** MH-Z19B (TX = IO27, RX = IO14).
- **Bus I2S :** SPH0645LM4H (WS = IO25, BCLK = IO26, DATA = IO32).

# 10. Architecture logicielle, FMS et Algorithmes (Software)

L'architecture logicielle repose sur une Machine à États Finis (FSM) principale couplée à des tâches d'acquisition.

## 10.1 Machine à états finis (FSM) globale
Le système global suit cette machine à états :
1. **INIT :** Configuration de FreeRTOS, connexion au WiFi, initialisation des bus I2C/UART/I2S.
2. **MONITORING (Acquisition) :** Tâches concurrentes lisant les capteurs indépendamment.
3. **EVALUATION (Traitement) :** Chaque tâche applique ses seuils locaux (*Thresholds*) sur les données acquises.
4. **ALERTING / CONTROL :** Si un seuil est franchi, la tâche effectue une transition en déposant un message dans la `sms_queue` ou en commandant le HVAC via `telemetry_task`.

## 10.2 Algorithmes et Pseudocodes par Tâche (Tasks)
Chaque tâche de surveillance fonctionne comme une sous-machine à états autonome avec ses propres seuils (définis selon les spécifications médicales).

### 1. Tâche de température corporelle (`Body Temp Mon`)
**Responsabilité et Machine à États :** Cette tâche est responsable de l'acquisition en continu de la température corporelle via le capteur I2C MAX30205. Elle utilise une machine à états interne (`is_temperature_stable`) pour s'assurer que la lecture est stable sur une fenêtre de temps avant de prendre une décision, évitant ainsi les fausses alertes liées à une perte de contact temporaire.
**Justification des seuils :** Les seuils de 36.0°C (hypothermie) et 37.8°C (fièvre) sont basés sur les limites cliniques pédiatriques. Une température hors de cette plage signale un stress physiologique grave nécessitant une intervention immédiate.

```text
TÂCHE Surveillance_Temperature_Corporelle:
    DÉFINIR SEUIL_BAS = 36.0
    DÉFINIR SEUIL_HAUT = 37.8

    BOUCLE_INFINIE:
        temp_c <- LIRE_CAPTEUR_I2C(MAX30205)
        
        // Envoi périodique via la file de télémétrie
        ENVOYER_TELEMETRIE(BODY_TEMPERATURE, temp_c)

        // Machine à état interne de stabilité
        SI est_stable(temp_c) ALORS:
            SI temp_c < SEUIL_BAS ALORS:
                ENVOYER_SMS(WARNING, "Température basse: < 36.0 C")
            SINON SI temp_c >= SEUIL_HAUT ALORS:
                ENVOYER_SMS(WARNING, "Température haute: > 37.8 C")
            FIN SI
        FIN SI
        
        ATTENDRE(1 seconde)
    FIN BOUCLE
FIN TÂCHE
```

### 2. Tâche de fréquence cardiaque et SpO2 (`Heart Rate Mon`)
**Responsabilité et Machine à États :** Cette tâche gère l'oxymètre MAX30102. Elle accumule les échantillons dans un tampon circulaire pour appliquer un filtre logiciel (fenêtre glissante de 20 secondes) avant le calcul, car les mouvements du nourrisson introduisent beaucoup de bruit (artefacts).
**Justification des seuils :** Le rythme cardiaque normal d'un nouveau-né (0-1 mois) au repos se situe entre 120 et 180 battements par minute (BPM) [4]. Une tachycardie ou bradycardie hors de ces limites déclenche une alerte. De plus, la saturation en oxygène (SpO2) est surveillée car une hypoxie est un précurseur direct de détresse respiratoire [7, 8].

```text
TÂCHE Surveillance_Rythme_Cardiaque:
    DÉFINIR SEUIL_BPM_MIN = 120
    DÉFINIR SEUIL_BPM_MAX = 180
    DÉFINIR SEUIL_SPO2_MIN = 90

    BOUCLE_INFINIE:
        tampon_donnees <- COLLECTER_ECHANTILLONS_I2C(MAX30102, 2000 échantillons)
        bpm, spo2 <- CALCULER_HR_ET_SPO2(tampon_donnees)
        
        // Transition: Si bébé est détecté
        SI bpm > 0 ALORS: 
            // Envoi des métriques à l'API via télémétrie
            ENVOYER_TELEMETRIE(HEART_RATE, bpm)
            ENVOYER_TELEMETRIE(OXYGEN_SATURATION, spo2)

            SI bpm < SEUIL_BPM_MIN OU bpm > SEUIL_BPM_MAX OU spo2 < SEUIL_SPO2_MIN ALORS:
                ENVOYER_SMS(CRITIQUE, "Anomalie cardiaque ou hypoxie détectée !")
            FIN SI
        FIN SI
        
        ATTENDRE(0.5 seconde)
    FIN BOUCLE
FIN TÂCHE
```

### 3. Tâche de surveillance de l'environnement (`Ambient Temp`)
**Responsabilité et Machine à États :** Cette tâche orchestre les systèmes de chauffage, ventilation et climatisation (HVAC) en fonction des lectures du BME680. La machine à états transite entre `HEATING_ON`, `COOLING_ON`, `DEHUMIDIFIER_ON` ou `HVAC_OFF`.
**Justification des seuils :** La température de la pièce est maintenue entre 20.0°C et 22.2°C pour prévenir la surchauffe, un facteur de risque majeur et prouvé du SMSN [2, 3]. L'humidité est maintenue en dessous de 55% pour prévenir la prolifération de moisissures et assurer un confort respiratoire optimal [6].

```text
TÂCHE Surveillance_Environnement_Ambiant:
    DÉFINIR TEMP_MIN = 20.0
    DÉFINIR TEMP_MAX = 22.2
    DÉFINIR HUMIDITE_MAX = 55.0

    BOUCLE_INFINIE:
        donnees_ambiantes <- LIRE_CAPTEUR_BME680()
        temp <- donnees_ambiantes.temperature
        humidite <- donnees_ambiantes.humidite
        
        // Envoi des données environnementales à Home Assistant
        ENVOYER_TELEMETRIE(TEMPERATURE_MEASUREMENT, temp)
        ENVOYER_TELEMETRIE(RELATIVE_HUMIDITY, humidite)

        // Transitions et Contrôle HVAC
        SI temp < TEMP_MIN ALORS:
            ACTIVER_CHAUFFAGE()
        SINON SI temp > TEMP_MAX ALORS:
            ACTIVER_CLIMATISATION_VENTILATEUR()
        SINON:
            DESACTIVER_HVAC()
        FIN SI

        SI humidite > HUMIDITE_MAX ALORS:
            ACTIVER_DESHUMIDIFICATEUR()
        FIN SI
        
        ATTENDRE(5 secondes)
    FIN BOUCLE
FIN TÂCHE
```

### 4. Tâche de qualité de l'air (`CO2 Mon`)
**Responsabilité et Machine à États :** Cette tâche lit les données UART du capteur MH-Z19B pour mesurer la concentration de dioxyde de carbone. Si le seuil est dépassé, elle déclenche la ventilation.
**Justification des seuils :** Le niveau cible de CO2 en intérieur doit se situer sous les 800 à 1000 ppm [5]. Une accumulation de CO2 dans l'environnement de sommeil augmente le risque de "réinhalation" par le nourrisson, un des mécanismes suspectés dans le SMSN.

```text
TÂCHE Surveillance_Qualite_Air:
    DÉFINIR CO2_MAX = 1000

    BOUCLE_INFINIE:
        co2_ppm <- LIRE_CAPTEUR_UART(MHZ19)
        
        ENVOYER_TELEMETRIE(CARBON_DIOXIDE_LEVEL, co2_ppm)

        SI co2_ppm > CO2_MAX ALORS:
            ACTIVER_VENTILATION()
            ENVOYER_SMS(WARNING, "Qualité de l'air dégradée (CO2 élevé)")
        FIN SI
        
        ATTENDRE(10 secondes)
    FIN BOUCLE
FIN TÂCHE
```

### 5. Tâche de mobilité (`Mobility Mon`)
**Responsabilité et Machine à États :** Cette tâche interroge un capteur infrarouge déporté (Zigbee). Elle utilise des compteurs temporels (`elapsed_sec`). La FSM passe d'un état normal à un état d'Avertissement (Warning) puis d'Alarme Critique (Alarm) si aucune activité n'est détectée.
**Justification des seuils :** Un bébé bouge naturellement pendant son sommeil (cycles de sommeil actif). Une immobilité totale de plus de 40 minutes déclenche une pré-alerte, et au-delà de 60 minutes, une alerte critique est envoyée pour vérifier l'état de réactivité du nourrisson.

```text
TÂCHE Surveillance_Mobilite:
    DÉFINIR DELAI_AVERTISSEMENT_MIN = 40
    DÉFINIR DELAI_ALARME_MIN = 60
    
    dernier_mouvement <- OBTENIR_TEMPS_ACTUEL()
    
    BOUCLE_INFINIE:
        SI verifier_mouvement_zigbee() EST VRAI ALORS:
            dernier_mouvement <- OBTENIR_TEMPS_ACTUEL()
            ENVOYER_TELEMETRIE(MOVEMENT, 1)
            ALLUMER_LUMIERE_SOMMEIL()
        SINON:
            ENVOYER_TELEMETRIE(MOVEMENT, 0)
            ETEINDRE_LUMIERE_SOMMEIL()
        FIN SI
        
        temps_ecoule_minutes <- (OBTENIR_TEMPS_ACTUEL() - dernier_mouvement) EN MINUTES
        ENVOYER_TELEMETRIE(MOVEMENT_LAST_TIME, temps_ecoule_minutes)
        
        // Transitions d'états d'alerte
        SI temps_ecoule_minutes > DELAI_ALARME_MIN ALORS:
            ENVOYER_TELEMETRIE(TELEMETRY_ALERT, "Aucun mouvement > 60 min!")
            ENVOYER_SMS(CRITIQUE, "Aucun mouvement détecté > 60 min!")
        SINON SI temps_ecoule_minutes > DELAI_AVERTISSEMENT_MIN ALORS:
            ENVOYER_TELEMETRIE(TELEMETRY_WARNING, "Aucun mouvement depuis 40 min.")
            ENVOYER_SMS(WARNING, "Aucun mouvement depuis 40 min.")
        FIN SI
        
        ATTENDRE(10 secondes)
    FIN BOUCLE
FIN TÂCHE
```

### 6. Tâche du moniteur audio (`Sound Monitor`)
**Responsabilité :** Cette tâche acquiert le signal I2S du microphone pour détecter les pleurs d'un bébé. Pour éviter les fausses alertes (faux positifs liés à un bruit bref ou un éternuement), la machine à états exige 4 périodes consécutives d'intensité sonore élevée (pleurs) avant de déclencher la transition vers l'état d'alerte SMS.

```text
TÂCHE Surveillance_Audio:
    DÉFINIR SEUIL_PERIODES_BRUYANTES = 4
    compteur_bruit <- 0

    BOUCLE_INFINIE:
        tampon_audio, niveau_sonore_rms <- LIRE_FLUX_I2S()
        
        // Envoi continu du niveau sonore
        ENVOYER_TELEMETRIE(SOUND_LEVEL, niveau_sonore_rms)
        
        SI analyser_pleurs(tampon_audio) EST VRAI ALORS:
            compteur_bruit <- compteur_bruit + 1
            
            // Transition d'état: Alerte si le seuil consécutif est atteint
            SI compteur_bruit >= SEUIL_PERIODES_BRUYANTES ALORS:
                ENVOYER_SMS(WARNING, "Avertissement: Bébé pleure")
                compteur_bruit <- 0  // Réinitialiser après envoi
            FIN SI
        SINON:
            compteur_bruit <- 0  // Réinitialiser si l'enfant se calme
        FIN SI
        
        ATTENDRE(1 seconde)
    FIN BOUCLE
FIN TÂCHE
```

### 7. Tâche de gestion réseau (`Wifi Man`)
**Responsabilité :** Maintient la connexion WiFi. La machine à états transite entre CONNECTE et DECONNECTE, en déclenchant des tentatives de reconnexion régulières lors d'une perte de réseau, assurant ainsi la fiabilité de la couche communication.

```text
TÂCHE Gestion_WiFi:
    // Initialisation asynchrone (pas de boucle infinie)
    INITIALISER_MEMOIRE_FLASH()
    CONFIGURER_CONNEXION_WIFI(SSID, PASSWORD)
    ENREGISTRER_GESTIONNAIRE_D_EVENEMENTS(wifi_event_handler)
    DEMARRER_SERVICE_WIFI()
    
    // La tâche se termine, la reconnexion est gérée par les événements ESP-IDF en arrière-plan
    SUPPRIMER_TACHE_COURANTE()
FIN TÂCHE

FONCTION wifi_event_handler(Evenement):
    SI Evenement EST DECONNEXION ALORS:
        TENTER_RECONNEXION_WIFI()  // Stratégie de reconnexion automatique
    FIN SI
FIN FONCTION
```

### 8. Tâche d'envoi de SMS (`SMS Worker`)
**Responsabilité :** Tâche de très haute priorité qui dépile les messages critiques (produits par les autres tâches) depuis une file d'attente (Queue) FreeRTOS. Cette séparation permet d'effectuer les requêtes HTTPS vers l'API SMS (qui sont bloquantes à cause du réseau) sans jamais bloquer l'acquisition des signes vitaux en temps réel.

```text
TÂCHE Envoi_SMS:
    BOUCLE_INFINIE:
        message <- LIRE_FILE_ATTENTE(sms_queue, ATTENTE_INFINIE)
        
        SI statut_wifi() EST CONNECTE ALORS:
            REPONSE <- EXECUTER_REQUETE_HTTPS_POST(API_SMS, message)
            SI REPONSE EST ERREUR ALORS:
                REMETTRE_DANS_FILE(sms_queue, message) // Tolérance aux fautes
                ATTENDRE(2 secondes)
            FIN SI
        SINON:
            REMETTRE_DANS_FILE(sms_queue, message)
            ATTENDRE(5 secondes)
        FIN SI
    FIN BOUCLE
FIN TÂCHE
```

### 9. Tâche de télémétrie (`Telemetry Worker`)
**Responsabilité :** Tâche agissant comme un pont (Bridge) vers le système domotique (Home Assistant). Elle récupère les données de routine (température, humidité) via une `telemetry_queue` et les transmet en arrière-plan sans perturber les tâches d'acquisition prioritaires.

```text
TÂCHE Telemetrie_IoT:
    BOUCLE_INFINIE:
        donnees_capteurs <- LIRE_FILE_ATTENTE(telemetry_queue, ATTENTE_INFINIE)
        
        SI statut_wifi() EST CONNECTE ALORS:
            ENVOYER_API_REST(SERVEUR_IOT, donnees_capteurs)
        FIN SI
    FIN BOUCLE
FIN TÂCHE
```

# 11. Analyse du temps et justification de l'ordonnancement

Le système exploite FreeRTOS pour assurer l'exécution concurrente. Le choix de l'ordonnancement garantit qu'une alerte vitale n'est jamais retardée par un calcul de bas niveau.

**Tableau 2 : Liste des tâches FreeRTOS et leurs rôles**

| Tâche FreeRTOS | Rôle et Responsabilité | Priorité (0-5) |
| :--- | :--- | :--- |
| `Wifi Man` | Gestion de la connectivité sans fil. | 5 (Élevée) |
| `SMS Worker` | Envoi asynchrone d'alertes SMS critiques via WiFi. | 5 (Élevée) |
| `Mobility Mon` | Interrogation de l'API distante (Zigbee) pour vérifier l'état de mouvement. | 5 (Élevée) |
| `Telemetry Worker` | Centralisation et envoi des données capteurs vers la passerelle IoT. | 5 (Élevée) |
| `Body Temp Mon` | Lecture périodique du capteur MAX30205 (température corporelle). | 5 (Élevée) |
| `Heart Rate Mon`| Lecture et filtrage des données de l'oxymètre MAX30102. | 5 (Élevée) |
| `CO2 Mon` | Lecture du taux de CO2 via le capteur MH-Z19B. | 5 (Élevée) |
| `Ambient Temp` | Lecture de la température ambiante et de l'humidité (BME680) et contrôle HVAC. | 4 (Normale) |
| `Sound Monitor` | Traitement du flux audio I2S pour détecter les pleurs (envoi d'un avertissement numérique). | 3 (Basse) |

*Justification :* Les tâches traitant de la survie, de la communication réseau immédiate et de l'acquisition des signes vitaux (température, pouls, mobilité) partagent la priorité maximale (5). Le contrôle de la température ambiante (4) et l'écoute audio (3) sont relégués à des priorités plus faibles pour éviter qu'un calcul lourd (comme la FFT du son) ne provoque une famine (starvation) bloquant l'envoi d'un SMS urgent.

# 12. Communication entre les différentes composantes

Le système repose sur un flux de communication hybride :
- **Interne (Matériel) :** Communication via I2C, I2S et UART entre l'ESP32 et les capteurs.
- **Interne (Logiciel) :** Les tâches communiquent entre elles à l'aide de **Queues** et de **Sémaphores** FreeRTOS. L'acquisition (`Body Temp Mon` et `Heart Rate Mon`) dépose ses données dans une file, lue par la tâche `Telemetry Worker` et la logique de contrôle.
- **Externe (Réseau) :** 
  - *Home Assistant (API REST / MQTT) :* Pour l'activation des relais intelligents (ventilateur, chauffage).
  - *Passerelle SMS :* Requêtes HTTP POST pour alerter le téléphone portable des parents.

# 13. Analyse de sécurité et justification des solutions retenues

La sûreté de fonctionnement (dependability) est vitale. 
- **Tolérance aux pannes matérielles :** Si un bus I2C se bloque, les tâches d'acquisition (ex: `Body Temp Mon`) utilise des *Timeouts* pour éviter de bloquer l'ESP32 et déclenche une réinitialisation logicielle du bus.
- **Gestion des erreurs (ESP_CHECK) :** L'utilisation systématique des macros `ESP_ERROR_CHECK` ou des retours d'erreurs (ex: `ESP_ERR_TIMEOUT`) pour l'acquisition sur les bus I2C/UART garantit que les lectures invalides ne soient pas analysées.
- **Files d'attente (Queues) :** Les tâches critiques communiquent de manière asynchrone, évitant ainsi le blocage croisé du système (deadlock) lors du traitement de multiples données de capteurs.
- **Choix des capteurs :** L'utilisation du MAX30205 a été justifiée par sa précision de qualité clinique (0.1°C), essentielle pour détecter une hypothermie chez un nourrisson.

# 14. Tests, validation et résolution de problèmes (Maintenance)

La résolution de problèmes et les tests ont couvert plusieurs volets :
- **Simulation fonctionnelle :** Injection de fausses données de fréquence cardiaque (ex: 110 BPM) dans la FSM pour vérifier la mise en file d'attente du SMS.
- **Gestion des déconnexions (Maintenance) :** En cas de perte WiFi, la tâche `Wifi Man` effectue des tentatives de reconnexion régulières. Le système continue d'acquérir les données de manière robuste pour ne rien manquer lorsque le réseau sera de nouveau disponible.
- **Maintenance prédictive :** La vérification continue de la qualité de l'air génère un avertissement asynchrone pour "Changer filtre à air" avant qu'un niveau critique ne soit atteint.

# 15. Difficultés éventuelles

- **Bruit sur l'oxymètre MAX30102 :** La mesure de la fréquence cardiaque sur un nourrisson est sujette aux artefacts de mouvement. L'implémentation de filtres numériques s'est avérée complexe pour stabiliser la lecture et éviter les fausses alertes.
- **Partage du bus I2C :** Le BME680, le MAX30205 et le MAX30102 partagent les broches IO18 et IO19. Le risque de collision des paquets a nécessité l'utilisation stricte de Mutex FreeRTOS pour protéger l'accès au bus matériel.
- **Chaleur de l'ESP32 :** La puce WiFi dissipe de la chaleur, faussant les lectures de température ambiante du BME680 si ce dernier est placé trop près sur le circuit imprimé.

# 16. Conclusion

Ce projet a permis de mettre en œuvre une solution embarquée en temps réel complète répondant à un besoin critique de sécurité infantile. En appliquant les concepts de l'ordonnancement de tâches, de communication réseau et d'acquisition de capteurs, le système démontre qu'il est possible de prévenir intelligemment les risques liés à la santé du bébé, tout en assurant une haute tolérance aux fautes (dependability). 

# 17. Références

[1] Université d'Ottawa, *CEG4566/SEG4545/CSI4541 - Hiver 2026 - Mini projet : L’internet des objets au service de la sécurité infantile*, 2026.
[2] FreeRTOS, *FreeRTOS Reference Manual*, [En ligne]. Disponible : https://www.freertos.org/
[3] Espressif Systems, *ESP-IDF Programming Guide*, [En ligne]. Disponible : https://docs.espressif.com/projects/esp-idf/

[4] HealthLinkBC, *Vital Signs in Children*, [En ligne]. Disponible : https://www.healthlinkbc.ca/healthwise/vital-signs-children
[5] Atmotube, *Indoor CO2 Levels*, [En ligne]. Disponible : https://atmotube.com/blog/indoor-co2-levels
[6] Nateo Concept, *What humidity in the baby's room*, [En ligne]. Disponible : https://nateoconcept.com/en/blog/what-humidity-in-the-babys-room-n170
[7] NCBI, *Blood Oxygen Levels and Hypoxia*, PMC12520277, [En ligne]. Disponible : https://pmc.ncbi.nlm.nih.gov/articles/PMC12520277/
[8] Owlet Care, *What happens when baby's oxygen levels are low*, [En ligne]. Disponible : https://owletcare.ca/blogs/blog/what-happens-baby-oxygen-levels-low

# 18. Annexe
*(Insérez le code source final ici)*
