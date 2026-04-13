# Rapport de Projet : Système de Surveillance de Bébé (babyMon)

## Table des Matières
- [Rapport de Projet : Système de Surveillance de Bébé (babyMon)](#rapport-de-projet--système-de-surveillance-de-bébé-babymon)
  - [Table des Matières](#table-des-matières)
  - [1. Introduction](#1-introduction)
  - [2. Description détaillée du projet](#2-description-détaillée-du-projet)
  - [3. Liste des composantes matérielles et logicielles](#3-liste-des-composantes-matérielles-et-logicielles)
    - [Composantes Matérielles (Hardware)](#composantes-matérielles-hardware)
    - [Composantes Logicielles (Software)](#composantes-logicielles-software)

## 1. Introduction
Le syndrome de mort subite du nourrisson (SMSN) représente une préoccupation majeure pour les nouveaux parents, s'agissant d'un décès inexpliqué survenant généralement pendant le sommeil de bébés en bonne santé de moins d'un an. Ce projet, intitulé **babyMon**, vise à concevoir et mettre en œuvre un système embarqué en temps réel basé sur un microcontrôleur **ESP32** pour surveiller la santé et la sécurité des nourrissons, particulièrement durant leur premier mois de vie. L'objectif principal est de fournir une solution intelligente capable de prévenir les risques avant qu'ils ne surviennent en surveillant les signes vitaux et l'environnement du bébé via des protocoles **WiFi** et **IoT**, tout en alertant les parents instantanément via **SMS** en cas d'anomalie critique. Pour garantir une fiabilité maximale et une réactivité immédiate, le système exploite les capacités multitâches de **FreeRTOS**.

## 2. Description détaillée du projet
Le système **babyMon** est une solution de surveillance basée sur l'Internet des Objets (IoT) utilisant un microcontrôleur ESP32 et le système d'exploitation temps réel FreeRTOS. Le projet répond aux exigences suivantes :

*   **Surveillance des signes vitaux :**
    *   Mesure de la température corporelle (seuil critique < 36°C ou > 37.5°C).
    *   Mesure de la fréquence cardiaque (normale entre 120-180 BPM).
    *   Mesure de la saturation en oxygène (SpO2).
*   **Analyse de l'environnement :**
    *   Mesure de la température ambiante (cible ~21°C), de l'humidité et de la qualité de l'air (CO2).
*   **Activité et confort du bébé :**
    *   Détection des mouvements via un capteur infrarouge Zigbee distant, interrogé par l'ESP32 via une API REST (Zigbee2MQTT).
    *   Alerte si aucune mobilité > 40 min, SMS d'alarme après 60 min.
    *   Détection des pleurs via un microphone I2S (SPH0645).
*   **Actions automatisées et Contrôle :**
    *   Régulation thermique et gestion de la qualité de l'air via l'intégration avec une instance **Home Assistant**.
    *   Pilotage à distance du chauffage, de la climatisation, du ventilateur et du déshumidificateur via des appels API REST (`esp_http_client`).
    *   Gestion intelligente de l'éclairage de sommeil synchronisée avec les mouvements détectés via Home Assistant.
*   **Sécurité et Connectivité :**
    *   Envoi de notifications et SMS via WiFi en cas d'anomalie.
    *   Réactivité temps réel (détection de danger en moins de 10ms, réaction globale en moins d'une seconde).

## 3. Liste des composantes matérielles et logicielles

### Composantes Matérielles (Hardware)
*   **Microcontrôleur :** ESP32 (DevKit v1 ou similaire), choisi pour ses capacités WiFi et son support FreeRTOS.
*   **Capteurs :**
    *   **MAX30205 :** Capteur de température corporelle de qualité médicale (I2C).
    *   **MAX30102 :** Oxymètre de pouls et capteur de fréquence cardiaque (I2C).
    *   **BME680 :** Capteur environnemental 4-en-1 (Température, Humidité, Pression, Gaz) (I2C).
    *   **MH-Z19B :** Capteur de CO2 infrarouge (UART).
    *   **SPH0645 :** Microphone MEMS pour la détection des pleurs (I2S).
    *   **Capteur de mouvement Zigbee :** Détecteur infrarouge passif (PIR) déporté, intégré via Zigbee2MQTT.
*   **Interfaces et Actionneurs :**
    *   **Passerelle IoT :** Serveur Home Assistant centralisant le contrôle des équipements (HVAC, lumières).
    *   **Interface I2C partagée :** Utilisation des broches GPIO 21 (SDA) et GPIO 22 (SCL).

### Composantes Logicielles (Software)
*   **Framework :** ESP-IDF (Espressif IoT Development Framework).
*   **OS Temps Réel :** FreeRTOS, permettant l'exécution concurrente de tâches avec priorités.
*   **Architecture des tâches (Tasks) :**
    *   `Wifi Man` : Gestion de la connectivité sans fil.
    *   `SMS Worker` : File d'attente (`sms_queue`) pour l'envoi d'alertes SMS.
    *   `Mobility Mon` : Interrogation de l'API Zigbee2MQTT pour l'état de mouvement.
    *   `Telemetry Worker` : Centralisation et envoi des données via une file d'attente.
    *   `Body Temp Mon` : Lecture périodique du capteur MAX30205 via I2C.
    *   `Heart Rate Mon` : Lecture et traitement des données de l'oxymètre MAX30102 via I2C.
    *   `CO2 Mon` : Lecture du taux de CO2 via le capteur MH-Z19B (UART).
    *   `Ambient Temp` : Orchestration du contrôle HVAC via l'API REST de Home Assistant.
    *   `Sound Monitor` : Traitement audio I2S pour la détection des pleurs.
*   **Communication Inter-tâches :** Utilisation de Queues FreeRTOS pour le découpage détection/action.
