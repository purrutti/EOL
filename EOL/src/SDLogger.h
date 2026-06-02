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

// Écrit une ligne d'erreur dans /data/errors.log (format: DateTime,Source,Message)
void sdLogError(const char* source, const char* fmt, ...);
