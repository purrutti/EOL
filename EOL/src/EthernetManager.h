#pragma once
#include <Arduino.h>

// ── Ethernet ──────────────────────────────────────────────────────────────────
// Initialise l'interface W5500 (IP fixe 192.168.2.231). Rapide, ne tente pas
// de NTP — la sync horaire se fait plus tard via ntpSessionBegin/Poll/End,
// après le premier cycle de mesures, pour laisser au modem GSM le temps
// d'établir sa connexion avant le premier essai.
void ethernetInit();

// ── RTC DS3231 ────────────────────────────────────────────────────────────────
bool rtcBegin();           // Initialise le DS3231 via I2C
bool rtcSyncToSystem();    // RTC → horloge système (fallback si pas de NTP)
bool rtcSetFromSystem();   // Horloge système → RTC (appelé après NTP)

// ── FTP ───────────────────────────────────────────────────────────────────────
// Upload les CSV du jour (/data/YYYYMMDD_EOL_*.csv) vers oceane.obs-vlfr.fr.
// Retourne le nombre de fichiers envoyés avec succès (0-3).
uint8_t ftpUploadDaily();

// ── NTP non-bloquant (pour la machine à états) ────────────────────────────────
// ntpSessionBegin() ouvre le socket UDP, ntpSessionPoll() envoie une requête
// et attend 1 s la réponse (bloquant 1 s max) — retourne true si heure synchro,
// ntpSessionEnd() ferme le socket.
void ntpSessionBegin();
bool ntpSessionPoll();
void ntpSessionEnd();
