# ZenTimer

Programme Arduino pour un minuteur multi-modes (méditation, reiki, sport, focus) sur ESP32 avec écran e-paper 1.54". L'interface est pilotée par un bouton unique avec retour haptique via moteur de vibration.

## Fonctionnalités

- **4 modes** : Méditation, Reiki, Sport (interval training), Focus (type Pomodoro). 
- **Configuration des durées** directement depuis l'écran (édition du temps sur l'appareil). 
- **Retour haptique** pour les actions, changements de phase et fin de session. 
- **Mise en veille profonde** automatique après inactivité (économie d'énergie). 
- **Affichage batterie** sur l'écran e-paper. 

## Modes et réglages par défaut

Les valeurs ci-dessous sont les valeurs initiales stockées en RTC (elles survivent au deep sleep) :

- **Reiki** : 1 position × 3 min par position. 
- **Méditation** : 10 min, avec alerte à mi-parcours activée. 
- **Sport (intervals)** : 45 s effort / 15 s repos × 10 rounds. 
- **Focus (Pomodoro)** : 25 min travail / 5 min pause × 4 cycles. 

## Contrôles

- **Clic** : navigation dans les menus / incrément dans l'édition du temps. 
- **Appui long** : valider/entrer dans un menu, démarrer une session, passer au champ suivant en édition du temps, arrêter une session en cours. 

## Matériel ciblé

- **ESP32**
- **Écran e-paper 1.54"** (GxDEPG0150BN 200×200) piloté avec GxEPD/GxIO.
- **Bouton unique** (AceButton).
- **Moteur de vibration** (PWM).
- **Mesure batterie** via ADC.

### Brochage utilisé

```text
PIN_MOTOR  = 4
PIN_KEY    = 35
PWR_EN     = 5
BACKLIGHT  = 33
BAT_ADC    = 34

SPI_SCK    = 14
SPI_DIN    = 13
EPD_CS     = 15
EPD_DC     = 2
EPD_RESET  = 17
EPD_BUSY   = 16
```

## Dépendances Arduino

- **GxEPD** (et le driver `GxDEPG0150BN`)
- **GxIO**
- **AceButton**
- **Arduino core ESP32**

Les bitmaps d'icônes sont définis dans `images.h`.

## Compilation / Upload

1. Ouvrir `ZenTimerV2.ino` dans l'IDE Arduino.
2. Sélectionner une carte **ESP32** compatible.
3. Installer les bibliothèques listées ci-dessus.
4. Compiler puis téléverser.

## Notes

- Le CPU est fixé à 80 MHz pour réduire la consommation.
- L'appareil passe en deep sleep après ~60 secondes d'inactivité.
