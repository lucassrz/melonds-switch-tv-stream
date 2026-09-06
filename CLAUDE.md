# Projet : écran du haut de melonDS Switch streamé en wifi vers une TV Android

## Objectif
Jouer à la DS sur melonDS Switch avec l'écran du haut sur la TV et l'écran du bas
(tactile) sur la Switch, **sans câble HDMI ni dock**. La Switch reste en mode portable
et envoie elle-même l'écran du haut sur le réseau ; une app Android TV l'affiche.

## Décisions prises (ne pas rouvrir)
- **SysDVR et le mode docké sont abandonnés.** Aucun fork de melonDS Switch ne sépare
  TV et écran console : il n'y a qu'un seul framebuffer, et l'OS éteint l'écran de la
  Switch en mode docké. SysDVR ne capturerait de toute façon pas un homebrew en applet.
- **Base de code : fork Gheovgos/melonDS, branche `switch-new`** (deko3d, release 7.2,
  archivé en février 2026). Lignée : Hydr8gon (2019) → RSDuck → Gheovgos.
- **Le stream part de l'émulateur lui-même** : relecture GPU de l'écran du haut,
  encodage JPEG sur un thread dédié, envoi UDP. Pas de h264, pas de RTSP.
- Licence de l'ensemble : GPL-3.0 (imposée par melonDS).

## Structure du dépôt
```
switch/        melonDS Switch patché, intégré en git subtree (historique amont conservé)
android-tv/    client Kotlin Android TV (Gradle, AGP 9.4, compileSdk 37, sans dépendance)
tools/         stream_receiver.py : récepteur de test sur le Mac (venv dans tools/.venv)
```
`melonDS-stock.nro` à la racine est le dernier build, ignoré par git.

## État au 6 septembre 2026
- Chaîne de compilation devkitPro installée sur le Mac, le fork compile en `.nro`.
- Le NRO tourne dans **Ryujinx/Ryubing 1.3.3** sur le Mac (prod.keys + firmware
  22.5.0 installés). Carte SD virtuelle : `~/Library/Application Support/Ryujinx/sdcard/`,
  melonDS lit `switch/melonds/` (minuscules) : bios7.bin, bios9.bin, firmware.bin,
  `melonDS.ini` (DS en majuscules).
- **Le stream fonctionne à 60 fps** de Ryujinx vers `tools/stream_receiver.py`.
- Pas encore testé sur la vraie console (temps d'encodage, wifi, marge CPU).
- **La 3D est noire dans Ryujinx** (le rasteriseur compute deko3d du fork n'est pas émulé par
  Ryujinx/MoltenVK). Limitation de l'émulateur, pas du stream : tester la 3D sur console.
- Client Android TV testé sur la TV du salon (Android 14, installation par adb en wifi).
  Flux Ryujinx → TV validé, audio inclus. SDK dans `~/Library/Android/sdk`, JDK 21 dans
  `~/Library/Java/JavaVirtualMachines/openjdk-21.0.1`. Build :
  `cd android-tv && JAVA_HOME=<jdk21> ./gradlew assembleDebug`. Kotlin intégré à AGP 9,
  pas de plugin Kotlin à appliquer.

## Patches appliqués dans `switch/` (à conserver lors d'un rebase)
- `CMakeLists.txt` : le bloc LTO ne remplace plus `CMAKE_AR` par un `gcc-ar` nu en
  cross-compilation. Après ce patch il faut repartir d'un `build/` vierge.
- `src/ARMJIT_Memory.cpp` et `src/ARMJIT_A64/ARMJIT_Compiler.cpp` : repli quand le handle
  de processus est absent (cas Ryujinx). Sans effet sur vraie console.
- `src/GPU2D_Deko.cpp` : les pushConstants de plus de 2048 octets sont découpés
  (limitation Ryujinx) ; relecture GPU de l'écran du haut (`StreamCaptureMemory`,
  index 0 = écran du haut).
- `src/frontend/switch/Stream.cpp/.h` : module de streaming. `PlatformConfig`,
  `SettingsDialog`, `main.cpp` : réglages, section de menu, hook par frame, overlay.
- `src/frontend/switch/stb_image/stb_image_write.h` : encodeur JPEG (domaine public).

## Format réseau (à respecter dans le client TV)
UDP, un datagramme = en-tête de 20 octets little endian puis jusqu'à 1400 octets d'image :
`u32 magic` (`0x3153444D` "MDS1" = JPEG, `0x3253444D` "MDS2" = QOI sans perte), `u32 frameId`,
`u32 totalSize`, `u32 partOffset`, `u16 partIndex`, `u16 partCount`. Image 256x192. Le
récepteur garde la frame complète la plus récente et jette les incomplètes.
Audio : `u32 magic 0x4153444D ("MDSA")`, `u32 sequence`, `u32 sampleRate` (32823), `u16 channels`
(2), `u16 frames` (320 max), puis PCM s16 entrelacé. Envoyé depuis le thread audio du frontend
(`AudioOutput` dans `main.cpp`), qui met alors la Switch en silence.
Référence : `tools/stream_receiver.py` (décode aussi le QOI, compte les paquets audio).

## Découverte des TV (UDP 9798)
Requête broadcast depuis la Switch : `u32 magic 0x5153444D ("MDSQ")`, `u32 version`.
Réponse : `u32 magic 0x5253444D ("MDSR")`, `u16 streamPort`, `u16 nameLen`, nom UTF-8.
Le menu Display > "Top screen streaming" liste les TV trouvées ("TVs found") et permet la
saisie manuelle au clavier logiciel. L'app TV et `stream_receiver.py` répondent tous deux.

## Réglages (`melonDS.ini`)
`StreamEnable`, `StreamHost` (IP de la TV), `StreamPort` (9797), `StreamQuality` (85),
`StreamFrameSkip` (1 = 60 fps, 2 = 30, 3 = 20), `StreamHideTop` (1 = la Switch passe en
"Bottom only" tant que le stream est actif, via `UpdateScreenLayout`), `StreamCodec` (0 = JPEG,
1 = QOI sans perte, `qoi.h` MIT vendu dans le frontend), `StreamAudio` (0 = haut-parleurs
Switch, 1 = TV). Le JPEG stb passe en 4:4:4 à partir de la qualité 90, d'où le défaut 92.
App TV (v0.2) : écran d'attente (nom, IP, consigne), menu OK/Menu dessiné en Canvas
(`MenuView.kt`) : affichage Fill sharp / Pixel perfect / Fill smooth, audio on/off, latence
audio 60/120/250 ms, overlay de stats. Réglages persistés (`Prefs.kt`, SharedPreferences).
`AudioTrack` refuse 32823 Hz sur la TV testée : `AudioPlayer.kt` retombe sur 48 kHz avec un
rééchantillonnage linéaire. Aucune dépendance AndroidX.
`ShowPerformanceMetrics=1` affiche la ligne "stream:" (frames, paquets, erreurs, taille
JPEG, temps d'encodage).

## Compiler
```bash
export DEVKITPRO=/opt/devkitpro PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitA64/bin:$PATH
cd switch/build && cmake .. -G Ninja -DENABLE_OGLRENDERER=OFF -DBUILD_QT_SDL=OFF \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-cross-Switch.cmake -DCMAKE_BUILD_TYPE=Release && ninja
cp melonDS.nro ../../melonDS-stock.nro
```
`dkp-pacman` est dans `/usr/local/bin` et demande sudo. Le NRO est lié au deko3d fourni par
devkitPro ; la variante RSDuck du README d'origine n'a pas été nécessaire.

## Publication
Dépôt GitHub prévu : `melonds-switch-tv-stream` (un seul dépôt, `switch/` en subtree, pas de
fork GitHub ni de sous-module). LICENSE GPL-3.0 à la racine, crédits dans THIRD_PARTY.md,
en-têtes "Copyright 2026 Lucas Sarazin" sur les fichiers nouveaux, CI GitHub Actions
(`.github/workflows/build.yml`, image `devkitpro/devkita64` + Gradle) qui attache NRO et APK
aux releases `v*`. Jamais de BIOS/firmware/ROM/clés dans le dépôt (voir `.gitignore`).
Pas d'infos perso (IP de la TV, etc.) dans les fichiers du dépôt.

## Prochaines étapes
1. Créer le dépôt GitHub vide `melonds-switch-tv-stream`, ajouter le remote, pousser `main`,
   vérifier que la CI passe (non testée en local), tagger `v0.1.0`.
2. Flux Ryujinx → TV validé le 6 septembre (APK installé par adb, Android 14). Reste à
   valider dans Ryujinx : la liste "TVs found" (le broadcast du guest doit sortir de
   Ryujinx) et le passage automatique en "Bottom only".
3. Test sur la vraie Switch : encodage, wifi, marge CPU. Envisager libjpeg-turbo si stb
   est trop lent.
4. Confort restant : port configurable côté TV, écran de réglages dans l'app TV.

## Contexte utilisateur
Développeur PHP/Python, pas de background Android/Kotlin/C++ Switch : accompagner pas à
pas, sans sur-ingénierie, dire honnêtement quand un gain ne vaut pas la complexité.
Objectif de publication open source si ça fonctionne.
