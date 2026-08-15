# M5GFX Home Assistant e-ink Dashboard for M5Paper

Interface tactile e-paper basée sur **M5GFX / ESP32** permettant de piloter une installation **Home Assistant**.

Code et README générés par IA.

## ✨ Fonctionnalités

### Page 1 — Maison

* 💡 **Éclairage**

  * Canapé
  * Dîner
  * Extinction salon/cuisine
  * Entrée
  * Kino
  * Veilleuse
* 🏠 **Modes**

  * Annonce audio
  * Absence prolongée
  * Fermeture automatique des volets
* 🎵 **Spotify**

  * Titre et artiste
  * Précédent / lecture-pause / suivant
* 🌡️ **Climatisation**

  * Chauffage / Clim / Off
  * Réglage de la température
  * Température actuelle
  * Humidité

### Page 2 — Volets & Wi-Fi

* 🪟 Sélection du volet
* ⬆️ Monter / ⏹ Stop / ⬇️ Descendre
* 📶 Génération d'un voucher Wi-Fi via Home Assistant
* Affichage du réseau, mot de passe et dernier voucher

La navigation entre les pages se fait avec deux boutons physiques.

---

## 🏗️ Architecture

```text
┌───────────────────────────┐
│       Interface M5        │
│   Écran + tactile + GPIO  │
└─────────────┬─────────────┘
              │ Wi-Fi / HTTP
              ▼
┌───────────────────────────┐
│      Home Assistant       │
├───────────────────────────┤
│ Lumières · Volets         │
│ Climatisation · Spotify   │
│ Wi-Fi / Vouchers          │
└───────────────────────────┘
```

Le M5 utilise l'API REST de Home Assistant pour envoyer des commandes et récupérer les états des entités.

---

## 📚 Bibliothèques

Principales dépendances :

```cpp
#include <M5GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
```

Ainsi que les bibliothèques ESP32 pour le deep sleep.

---

## 🔐 Configuration

Les informations sensibles sont placées dans un fichier séparé :

```text
secrets.h
```

Exemple :

```cpp
#define WIFI_SSID "MonWifi"
#define WIFI_PASSWORD "MotDePasse"

#define HA_HOST "http://192.168.1.10:8123"
#define HA_TOKEN "MON_LONG_LIVED_ACCESS_TOKEN"
```

Voir `secrets-example.h`.


---

## 🏠 Entités Home Assistant

Les entités utilisées sont regroupées au début du programme :

```cpp
const char* ENTITY_CLIMATE  = "climate.clim";
const char* ENTITY_TEMP     = "sensor.salon_temperature";
const char* ENTITY_HUMIDITY = "sensor.salon_humidite";

const char* ENTITY_WIFI_BUTTON = "input_button.creer_voucher";
const char* ENTITY_WIFI_SENSOR = "sensor.liste_hotspot_vouchers";

const char* ENTITY_SPOTIFY =
  "media_player.mySpotify";
```

Les scènes, modes et volets sont également définis dans les structures `ActionCard` et `CoverCard`.

👉 **Pour adapter le projet à une autre installation, c'est principalement cette partie qu'il faut modifier.**

---

## 🔌 Communication avec Home Assistant

Les commandes passent par :

```cpp
haCall(domain, service, entity, extra);
```

Exemple :

```cpp
haCall(
  "light",
  "turn_on",
  "light.salon"
);
```

Ce qui correspond à l'appel :

```text
POST /api/services/light/turn_on
```

La lecture d'un état se fait avec :

```cpp
haState(entity, doc);
```

qui utilise :

```text
GET /api/states/<entity>
```

L'authentification utilise le token Home Assistant :

```http
Authorization: Bearer <TOKEN>
```

---

## 🖥️ Interface graphique

L'interface est dessinée directement avec **M5GFX**, sans framework graphique supplémentaire.

Les principaux éléments sont organisés en cartes :

```cpp
drawLighting()
drawCovers()
drawClimate()
drawSpotify()
drawWifi()
```

La mise en page est centralisée dans :

```cpp
setupLayout()
```

Les coordonnées sont calculées à partir de la largeur réelle de l'écran afin de faciliter l'adaptation à une autre résolution.

---

## 👆 Interaction tactile

Chaque fonctionnalité possède son gestionnaire tactile :

```cpp
touchAction()
touchCovers()
touchClimate()
touchSpotify()
touchWifi()
```

Le fonctionnement général est :

```text
Touch
  ↓
Identification de la carte
  ↓
Identification du bouton
  ↓
Commande Home Assistant
  ↓
Lecture de l'état
  ↓
Mise à jour de l'affichage
```

Les boutons sont brièvement affichés en noir pendant l'exécution de la commande afin de fournir un retour visuel.

---

## 🪟 Volets

Les volets sont définis ici :

```cpp
CoverCard covers[] = {
  {"CUISINE", "cover.xxx"},
  {"SALON",   "cover.xxx"}
};
```

Le même jeu de commandes est utilisé pour le volet sélectionné :

```text
       ↑
     STOP
       ↓
```

Les services Home Assistant utilisés sont :

```text
cover.open_cover
cover.stop_cover
cover.close_cover
```

Pour ajouter un volet, il suffit d'ajouter une entrée au tableau et d'adapter `COVER_COUNT`.

---

## 🌡️ Climatisation

Les trois modes correspondent à :

```cpp
"heat"
"cool"
"off"
```

La température est modifiable avec :

```cpp
constexpr float TEMP_STEP = 1.0f;
constexpr float TEMP_MIN  = 10.0f;
constexpr float TEMP_MAX  = 30.0f;
```

Par exemple, pour utiliser des incréments de 0,5 °C :

```cpp
constexpr float TEMP_STEP = 0.5f;
```

---

## 🎵 Spotify

L'entité `media_player` est utilisée pour récupérer :

```text
media_title
media_artist
state
```

et envoyer :

```text
media_previous_track
media_play_pause
media_next_track
```

Pour utiliser un autre compte Spotify, remplacer simplement :

```cpp
ENTITY_SPOTIFY
```

par l'entité correspondante dans Home Assistant.

---

## 📶 Vouchers Wi-Fi

Le bouton :

```text
Activer hotspot
```

appelle :

```text
input_button.press
```

sur :

```cpp
ENTITY_WIFI_BUTTON
```

Le programme attend ensuite la création du voucher et lit :

```cpp
ENTITY_WIFI_SENSOR
```

Il s'attend à trouver un attribut JSON `data` contenant notamment :

```text
createdAt
code
```

Cette partie est donc **spécifique à l'installation actuelle** et devra probablement être adaptée si la structure du capteur change.

---

## 🔋 Deep Sleep

Pour économiser l'énergie, l'appareil passe en deep sleep après :

```cpp
constexpr uint32_t DEEPSLEEP_TIMEOUT_MS = 60000;
```

soit **60 secondes d'inactivité**.

Le système peut être désactivé avec :

```cpp
constexpr bool DEEPSLEEP_ENABLED = false;
```

Le réveil se fait via le tactile.

⚠️ Les GPIO utilisés pour les boutons, le tactile et l'alimentation dépendent du modèle M5 :

```cpp
G37
G39
TOUCH_INT
MAIN_PWR
```

Ils doivent être vérifiés avant de porter le programme sur un autre appareil.

---

## 🔧 Adapter le projet à une autre installation

Les modifications principales sont :

```text
1. Créer secrets.h
2. Configurer le Wi-Fi
3. Configurer HA_HOST et HA_TOKEN
4. Remplacer les entités Home Assistant
5. Adapter les scènes / modes / volets
6. Adapter Spotify
7. Adapter le système de vouchers si nécessaire
8. Vérifier les GPIO si le matériel change
```

Pour ajouter une nouvelle fonctionnalité, le modèle recommandé est :

```text
Données
  ↓
drawXXX()
  ↓
touchXXX()
  ↓
haCall() / haState()
  ↓
Ajouter dans setupLayout() et loop()
```

---

## 📁 Organisation logique du code

```text
Configuration
├── secrets.h
├── GPIO
└── Entités Home Assistant

Modèles de données
├── ActionCard
├── CoverCard
├── ClimateCard
├── WifiCard
└── SpotifyCard

Affichage
├── drawLighting()
├── drawCovers()
├── drawClimate()
├── drawSpotify()
└── drawWifi()

Home Assistant
├── haCall()
├── haState()
├── refreshAction()
├── refreshCover()
├── refreshClimate()
├── refreshSpotify()
└── refreshVoucher()

Interaction
├── pageButtons()
├── touchAction()
├── touchCovers()
├── touchClimate()
├── touchSpotify()
└── touchWifi()

Énergie
├── resetActivityTimer()
└── enterDeepSleep()
```

---

## ⚠️ Points à connaître

* Le fonctionnement dépend de la disponibilité du Wi-Fi et de Home Assistant.
* Les entités Home Assistant sont actuellement codées en dur.
* Spotify n'est pas rafraîchi en permanence.
* Le format du capteur de vouchers est spécifique à l'installation d'origine.
* Le deep sleep et les GPIO sont dépendants du matériel M5 utilisé.
* Les écrans e-paper nécessitent une attention particulière au ghosting ; le changement de page effectue donc un effacement complet tandis que les petites modifications redessinent uniquement la carte concernée.

---

## 🚀 En résumé

Ce projet est une **télécommande domotique e-paper basée sur ESP32/M5GFX**, avec Home Assistant comme backend.

Il permet de centraliser sur un petit écran :

**éclairage · modes · volets · climatisation · Spotify · accès Wi-Fi**

La structure du code est volontairement modulaire afin de pouvoir remplacer les entités Home Assistant et ajouter facilement de nouvelles cartes ou commandes.
