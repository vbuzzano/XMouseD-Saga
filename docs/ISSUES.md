# XMouse - Issues et Problèmes Potentiels

**Version:** 1.0  
**Date:** December 16, 2025  
**Auteur:** Vincent Buzzano (ReddoC)

---

## Table des Matières

1. [Issues Majeures](#issues-majeures)
2. [Issues Mineures](#issues-mineures)
3. [Risques Potentiels](#risques-potentiels)
4. [Code Smell & Debt Technique](#code-smell--debt-technique)

---
## Issues Majeures
---

### M4. `PrintF()` appelé en mode RELEASE sans protection

**Localisation:** `parseArguments()` ligne 424-432

**Problème:**
En mode RELEASE, `PrintF()` est appelé sans vérifier si console disponible (peut crasher si lancé depuis Workbench).

**Code problématique:**
```c
### M4. `PrintF()` appelé en mode RELEASE sans protection

**Localisation:** `parseArguments()` ligne ~545

**Problème:**
En mode RELEASE, `PrintF()` est appelé sans vérifier si console disponible (peut crasher si lancé depuis Workbench).

**Code actuel:**
```c
#ifndef RELEASE
    PrintF("config: 0x%02lx", (ULONG)configByte);
    PrintF("wheel: %s", (configByte & CONFIG_WHEEL_ENABLED) ? "ON" : "OFF");
    PrintF("extra buttons: %s", (configByte & CONFIG_BUTTONS_ENABLED) ? "ON" : "OFF");
#endif
if (configByte & CONFIG_DEBUG_MODE)  // ← CONFIG_DEBUG_MODE actif en RELEASE!
{
    PrintF("mode: %s", getModeName(configByte));  // ← Pas protégé!
}
```

**Explication du problème:**

1. **CONFIG_DEBUG_MODE (bit 7)** est un flag utilisateur (dans le byte de config 0xNN)
2. **RELEASE** est un flag de compilation (`make MODE=release`)
3. Ce sont deux choses **complètement différentes**!

**Scénario crash:**
- User compile en `MODE=release` → pas de console debug prévue
- User lance depuis **Workbench** (double-clic icône) → `Output()` = NULL
- User passe config `0x93` (bit7=1, debug mode activé)
- Code appelle `PrintF("mode: %s", ...)` → **CRASH** car pas de console

**Impact:**
- 🔴 **Crash potentiel** si lancé depuis Workbench avec CONFIG_DEBUG_MODE
- 🟡 **Inconsistency** - 3 premiers PrintF protégés par `#ifndef RELEASE`, le 4ème non
- 🟡 **Confusion** - CONFIG_DEBUG_MODE devrait être inutile en RELEASE

**Solution 1 (conservative):** Protéger le PrintF restant
```c
#ifndef RELEASE
if (configByte & CONFIG_DEBUG_MODE)
{
    PrintF("mode: %s", getModeName(configByte));
}
#endif
```

**Solution 2 (robuste):** Vérifier `Output()` avant tout PrintF en RELEASE
```c
if (configByte & CONFIG_DEBUG_MODE)
{
    BPTR out = Output();
    if (out)  // ← Console existe?
    {
        PrintF("mode: %s", getModeName(configByte));
    }
}
```

**Recommandation:** Solution 1 (plus simple, cohérent avec les 3 autres PrintF)

**Priorité:** 🟠 MAJEURE - Peut crasher en production
**Problème:**
Les pointeurs statiques (`s_PublicPort`, `s_InputPort`, etc.) ne sont pas mis à `NULL` après cleanup.

**Impact:**
- **Risque double-free** si `daemon_Cleanup()` appelé deux fois
- **Dangling pointers** si daemon relancé dans même processus (théoriquement impossible)

**Solution:**
```c
if (s_PublicPort)
{
    RemPort(s_PublicPort);
    DeleteMsgPort(s_PublicPort);
    s_PublicPort = NULL;  // ← Ajouter
}
```

**Priorité:** 🟡 MINEURE - Edge case improbable

---

### m2. Logs debug commentés dans `daemon_processWheel()` et `daemon_processButtons()`

**Localisation:** Lignes 728-731, 773-774

**Problème:**
Code debug commenté pollue le source. Devrait être supprimé ou activable via flag.

**Exemple:**
```c
#ifndef RELEASE
    // Log wheel event
    //DebugLogF("Wheel: delta=%ld dir=%s count=%ld", ...);  // ← Commenté
#endif
```

**Impact:**
- **Code smell** - confusion entre code actif et mort
- **Maintenance** - oubli de nettoyer

**Solution:**
Soit supprimer, soit créer flag `CONFIG_VERBOSE_DEBUG`:
```c
#ifndef RELEASE
    if (s_configByte & CONFIG_VERBOSE_DEBUG) {
        DebugLogF("Wheel: delta=%ld dir=%s count=%ld", ...);
    }
#endif
```

**Priorité:** 🟡 MINEURE - Qualité code

---

### m3. Duplication code gestion debug console

**Localisation:** `daemon()` lignes 548-565 et 631-646

**Problème:**
Logique open/close debug console dupliquée dans deux endroits (init et XMSG_CMD_SET_CONFIG).

**Solution:**
Extraire en fonctions:
```c
static inline void openDebugConsole(void)
{
#ifndef RELEASE
    if (!s_debugCon) {
        s_debugCon = Open("CON:0/0/640/200/XMouseD Debug/AUTO/CLOSE/WAIT", MODE_NEWFILE);
        DebugLog("Debug mode enabled");
    }
#endif
}

static inline void closeDebugConsole(void)
{
#ifndef RELEASE
    if (s_debugCon) {
        Close(s_debugCon);
        s_debugCon = 0;
    }
#endif
}
```

**Priorité:** 🟡 MINEURE - DRY principle

---

### m4. `InputBase` déclaré deux fois

**Localisation:** Ligne 105 (commenté) et ligne 106

**Problème:**
```c
//void *InputBase;                       // Input library base (for PeekQualifier inline pragma)
struct Device * InputBase;
```

Ancienne déclaration commentée mais toujours présente.

**Solution:**
Supprimer ligne commentée.

**Priorité:** 🟡 MINEURE - Cleanup

---

## Risques Potentiels

### R1. Pas de debouncing pour boutons 4/5

**Localisation:** `daemon_processButtons()` ligne 747-792

**Risque:**
Si hardware glitche ou génère bruit électrique, multiples événements press/release peuvent être injectés.

**Mitigation:**
Ajouter simple debouncing:
```c
#define BUTTON_DEBOUNCE_TICKS 2

static UBYTE s_buttonStableCount[2] = {0, 0};  // Button 4 et 5

// Dans processButtons():
if (changed & SAGA_BUTTON4_MASK) {
    s_buttonStableCount[0]++;
    if (s_buttonStableCount[0] >= BUTTON_DEBOUNCE_TICKS) {
        // Inject event
        s_buttonStableCount[0] = 0;
    }
} else {
    s_buttonStableCount[0] = 0;  // Reset si pas de changement
}
```

**Priorité:** 🟢 INFO - Pas observé en pratique

---

### R2. `CONFIG_STOP` logique inversée peut confondre

**Localisation:** Ligne 87

**Problème:**
```c
#define CONFIG_STOP (CONFIG_WHEEL_ENABLED | CONFIG_BUTTONS_ENABLED)
```

Nom suggère "config pour stop", mais en réalité c'est "bits à tester pour détecter stop".

**Confusion:**
```c
if ((configByte & CONFIG_STOP) == 0)  // Stop si wheel ET buttons désactivés
```

**Solution:**
Renommer en `CONFIG_ANY_FEATURE_MASK` ou commenter clairement.

**Priorité:** 🟢 INFO - Naming

---

### R3. Système adaptatif peut stagner en ACTIVE

**Localisation:** `getAdaptiveInterval()` état ACTIVE

**Risque:**
Si activité sporadique avec intervalle > activeThreshold mais < idleThreshold, système reste en ACTIVE indéfiniment (ne descend pas vers BURST, ne remonte pas vers IDLE).

**Exemple:**
- User scroll toutes les 600ms
- activeThreshold = 500ms
- Chaque scroll reset inactive counter
- Système oscille IDLE ↔ ACTIVE sans jamais atteindre BURST

**Impact:**
Réactivité sous-optimale (reste à 30ms au lieu de descendre à 10ms).

**Solution:**
Ajuster thresholds ou ajouter counter "ticks en ACTIVE sans descendre":
```c
static UBYTE s_activeTicksCount = 0;

case POLL_STATE_ACTIVE:
    if (hadActivity) {
        s_activeTicksCount++;
        // Force transition to BURST après 10 ticks même si pas descendu
        if (s_activeTicksCount > 10) {
            s_adaptiveState = POLL_STATE_BURST;
            s_adaptiveInterval = mode->burstUs;
        }
    }
```

**Priorité:** 🟢 INFO - Edge case théorique

---

## Code Smell & Debt Technique

### CS1. Complexité excessive système adaptatif

**Problème:**
Machine à états adaptive avec 4 états x 4 profils x 2 modes = complexité élevée pour un cas d'usage simple (wheel scroll).

**Métriques:**
- **Lines of code:** ~150 lignes pour `getAdaptiveInterval()` + table modes
- **Cyclomatic complexity:** 8+
- **Maintenance cost:** Élevé (tuning des 7 paramètres par profil)

**Justification actuelle:**
Économie CPU et batterie (important pour portable Vampire V4).

**Considération:**
Si profiling montre que fixed mode suffit pour 90% users, simplifier en enlevant dynamic.

**Priorité:** 🔵 REFACTORING - Design decision

---

### CS2. Logs debug dupliqués dans `getAdaptiveInterval()`

**Localisation:** Lignes 847-852 et 881-898

**Problème:**
Même log apparaît dans le switch (transitions) ET après le switch (toutes les changes).

**Exemple:**
Transition IDLE→ACTIVE loggée deux fois:
1. Dans `case POLL_STATE_IDLE` (ligne 847)
2. Dans `if (oldState != s_adaptiveState)` après switch (ligne 885)

**Solution:**
Choisir un seul endroit (après switch recommandé pour vue unifiée).

**Priorité:** 🔵 REFACTORING - Qualité logs

---

### CS3. Macro `TIMER_START` pourrait être fonction inline

**Localisation:** Ligne 229-233

**Problème:**
Macro multi-ligne sans do-while protection.

**Risque:**
```c
if (condition)
    TIMER_START(10000);  // ← Expand à 3 statements!
// else branch ignoré!
```

**Solution:**
```c
static inline void timerStart(ULONG micros)
{
    s_TimerReq->tr_node.io_Command = TR_ADDREQUEST;
    s_TimerReq->tr_time.tv_secs = micros / 1000000;
    s_TimerReq->tr_time.tv_micro = micros % 1000000;
    SendIO((struct IORequest *)s_TimerReq);
}
```

**Priorité:** 🔵 REFACTORING - Best practice

---

### CS4. TODO commenté pas tracked

**Localisation:** Ligne 28

**Problème:**
```c
// TODO: Transform each log string to constants vvvv HERRE vvvv
```

TODO dans code source mais pas dans issue tracker ou ROADMAP.

**Solution:**
Soit faire le travail, soit créer issue GitHub, soit supprimer si non-prioritaire.

**Priorité:** 🔵 REFACTORING - Projet management

---

## Todo List - Plan de Correction

### 🟠 Majeures (Avant Release)
- [ ] **M4** Protéger `PrintF()` RELEASE → crash prevention

### 🟡 Mineures (Avant 1.0 Final)
- [ ] **m1** Nullifier pointeurs dans `daemon_Cleanup()`
- [ ] **m2** Nettoyer logs debug commentés
- [ ] **m3** Extraire `openDebugConsole()/closeDebugConsole()`
- [ ] **m4** Supprimer `InputBase` dupliquée

### 🔵 Post-Release (Optimisations)
- [ ] **POST** Optimisations VBCC/Apollo flags (+apollo, -O4, -fomit-frame-pointer)
- [ ] **POST** Réduire taille code debug/dead code (~1-2KB gain)

---

**Document maintenu par:** ReddoC  
**Dernière revue:** December 17, 2025
