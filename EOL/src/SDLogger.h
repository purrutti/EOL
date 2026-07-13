#pragma once
#include <Arduino.h>
#include "CTD.h"
#include "FLNTU.h"
#include "PiSAMI_decode.h"

// Broche CS de la carte SD (SPI par défaut : CLK=18 MISO=19 MOSI=23)
#ifndef SD_CS_PIN
#  define SD_CS_PIN PIN_SD_CS
#endif

// Initialise la carte SD et crée le répertoire /data si absent.
// Retourne false si la carte est introuvable.
bool sdLoggerBegin(uint8_t csPin = SD_CS_PIN);

// Écrit une ligne dans /data/YYYYMMDD_EOL_CTD.csv
// Nécessite que data.date soit renseigné (parseDatetime a réussi).
void sdLogCTD(const CTDData& data);

// Écrit une ligne dans /data/YYYYMMDD_EOL_FLNTU.csv
void sdLogFLNTU(const FLNTUData& data);

// Écrit une ligne dans /data/YYYYMMDD_EOL_SAMI.csv
// Utilise rec.unixTimestamp pour nommer le fichier et horodater la ligne.
void sdLogSAMI(const PiSAMI_Record& rec);

// Écrit une ligne dans /data/YYYYMMDD_EOL_SAMI_RAW.csv
// Contient la chaîne hexadécimale brute reçue du PiSAMI lors du poll,
// avant décodage (utile pour rejouer/déboguer même en cas d'échec de décodage).
void sdLogSAMIRaw(const char* raw);

// Écrit une ligne d'erreur dans /data/errors.log (format: DateTime,Source,Message).
// Si le fichier dépasse 50 Mo après écriture, il est supprimé (avec son marqueur
// de date de création) pour ne pas saturer la carte SD.
void sdLogError(const char* source, const char* fmt, ...);

// Date de création (YYYYMMDD) de l'errors.log courant, lue depuis son marqueur
// /data/errors_created.txt. Retourne false si le marqueur est absent.
bool sdErrorsLogCreationDate(char dateOut[9]);
