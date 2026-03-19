/* ═══════════════════════════════════════════════════════════════════
 *  T-Display-S3-Long  —  Universal OBD2 Reader  v1.0
 *
 *  Libraries needed (Arduino Library Manager):
 *    • arduino-mcp2515    by autowp
 *    • TFT_eSPI           (already installed)
 *    • QRCodeGenerator    by felixerdy (github.com/felixerdy/QRCodeGenerator)
 *
 *  Board settings:
 *    Board  : ESP32S3 Dev Module  (or LilyGo T-Display-S3)
 *    PSRAM  : OPI PSRAM           ← REQUIRED
 *    CPU    : 240 MHz
 *
 *  MCP2515 wiring → T-Display-S3-Long GPIO header:
 *    MCP2515 CS   →  GPIO 2
 *    MCP2515 INT  →  GPIO 3
 *    MCP2515 SCK  →  GPIO 6
 *    MCP2515 MOSI →  GPIO 7
 *    MCP2515 MISO →  GPIO 5
 *    MCP2515 VCC  →  3.3V
 *    MCP2515 GND  →  GND
 *
 *  CAN bus speed : 500 kbps (most 2008+ cars)
 *  MCP2515 XTAL  : 8 MHz  (change MCP_8MHZ → MCP_16MHZ if needed)
 * ═══════════════════════════════════════════════════════════════════ */

#include <FS.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include "QRCodeGenerator.h"
#include <TFT_eSPI.h>
#include <mcp2515.h>
#include <SPI.h>
#include <Wire.h>
#include "AXS15231B.h"
#include "pins_config.h"

/* ── MCP2515 pins ─────────────────────────────────────────────── */
#define MCP_CS    2
#define MCP_INT   3
#define MCP_SCK   6
#define MCP_MOSI  7
#define MCP_MISO  5

SPIClass  CAN_SPI(HSPI);
MCP2515   mcp(MCP_CS, 10000000, &CAN_SPI);
bool      canOK       = false;
bool      carConn     = false;
bool      demoMode    = false;
bool      demoUserSet = false;
uint16_t  canKbps     = 500;    // detected CAN bus speed (500 or 250)

/* ── WiFi / Web config ────────────────────────────────────────── */
#define WIFI_AP_SSID  "OBD2-READER"
#define WIFI_AP_PASS  "12345678"
WebServer  webServer(80);
DNSServer  dnsServer;
Preferences prefs;
bool       wifiOn = false;

/* ── Theme — minimal dark dashboard ──────────────────────────── */
#define C_BG      0x0000u   // pure black
#define C_LINE    0x18C3u   // #1A1A1A grid separator
#define C_LINE2   0x2945u   // #282830 lighter line
#define C_LABEL   0x8410u   // #808080 gray labels
#define C_VALUE   0xFFFFu   // #FFFFFF white values
#define C_DIM     0x4208u   // #404040 inactive
#define C_ACCENT  0x04FFu   // #0090FF blue accent
#define C_OK      0x05E0u   // #00B800 green
#define C_WARN    0xFD20u   // #FFA000 amber
#define C_DANGER  0xF800u   // #FF0000 red
/* Aliases for legacy pages (boot, DTC, system, picker) */
#define C_PANEL   0x0841u
#define C_CARD    0x10A2u
#define C_BORDER  0x2965u
#define C_SEP     0x2126u
#define C_TEXT    0xFFFFu
#define C_VAL     0x07FFu
#define C_NA      0x4228u
#define C_GREEN   0x07E0u
#define C_LIME    0x47E0u
#define C_YELLOW  0xFFE0u
#define C_AMBER   0xFD00u
#define C_RED     0xF800u
#define C_ORANGE  0xFD20u
#define C_TEAL    0x0678u
#define C_PURPLE  0x901Fu

/* ── User-configurable settings ──────────────────────────────── */
struct Settings {
    uint16_t rpmMax;    // rpm gauge full-scale
    uint16_t speedMax;  // speed gauge full-scale km/h
    uint16_t mapMax;    // MAP/boost gauge full-scale kPa
    uint8_t  coolWarn;  // coolant warning threshold °C
    uint8_t  tempUnit;  // 0=°C  1=°F
    uint8_t  pressUnit; // 0=kPa  1=bar  2=psi
};
Settings cfg = { 8000, 260, 250, 95, 0, 1 };

/* Convert a raw OBD2 value to display units */
float convertVal(uint8_t pid, float v) {
    if (pid==0x05||pid==0x0F||pid==0x46||pid==0x5C)
        return (cfg.tempUnit==1) ? v*1.8f+32.0f : v;
    if (pid==0x0B||pid==0x33||pid==0x0A||pid==0x6F) {
        if (cfg.pressUnit==1) return v*0.01f;      // kPa → bar
        if (cfg.pressUnit==2) return v*0.14504f;   // kPa → psi
    }
    return v;
}
/* Return display unit string for a PID */
const char* convertUnit(uint8_t pid, const char* orig) {
    if (pid==0x05||pid==0x0F||pid==0x46||pid==0x5C)
        return (cfg.tempUnit==1) ? "F" : "C";
    if (pid==0x0B||pid==0x33||pid==0x0A||pid==0x6F) {
        if (cfg.pressUnit==1) return "bar";
        if (cfg.pressUnit==2) return "psi";
        return "kPa";
    }
    return orig;
}

void loadSettings();   // forward declaration

void saveSettings() {
    Preferences sp;
    sp.begin("obd2set", false);
    sp.putUShort("rpmMax",   cfg.rpmMax);
    sp.putUShort("speedMax", cfg.speedMax);
    sp.putUShort("mapMax",   cfg.mapMax);
    sp.putUChar("coolWarn",  cfg.coolWarn);
    sp.putUChar("tempUnit",  cfg.tempUnit);
    sp.putUChar("pressUnit", cfg.pressUnit);
    sp.end();
}

void settingCycle(int row) {
    static const uint16_t rpmOpts[] = {4000,5000,6000,7000,8000,9000};
    static const uint16_t spdOpts[] = {120,160,200,260};
    static const uint16_t mapOpts[] = {120,150,200,250,300};
    static const uint8_t  cwOpts[]  = {90,95,100,105,110};
    switch (row) {
        case 0: { int n=sizeof(rpmOpts)/2,c=0; for(int i=0;i<n;i++) if(rpmOpts[i]==cfg.rpmMax){c=i;break;} cfg.rpmMax=rpmOpts[(c+1)%n]; break; }
        case 1: { int n=sizeof(spdOpts)/2,c=0; for(int i=0;i<n;i++) if(spdOpts[i]==cfg.speedMax){c=i;break;} cfg.speedMax=spdOpts[(c+1)%n]; break; }
        case 2: { int n=sizeof(mapOpts)/2,c=0; for(int i=0;i<n;i++) if(mapOpts[i]==cfg.mapMax){c=i;break;} cfg.mapMax=mapOpts[(c+1)%n]; break; }
        case 3: { int n=sizeof(cwOpts),c=0; for(int i=0;i<n;i++) if(cwOpts[i]==cfg.coolWarn){c=i;break;} cfg.coolWarn=cwOpts[(c+1)%n]; break; }
        case 4: cfg.tempUnit  = (cfg.tempUnit  + 1) % 2; break;
        case 5: cfg.pressUnit = (cfg.pressUnit + 1) % 3; break;
    }
}

/* ── OBD2 PID definitions ─────────────────────────────────────── */
struct PidEntry {
    uint8_t  id;
    char     label[11];
    char     unit[6];
    float    val;
    bool     supported;
    bool     active;
    uint32_t ts;
};

#define N_PIDS 26
PidEntry pids[N_PIDS] = {
    /* ── Core engine ──────────────────────────────────────── */
    {0x0C, "RPM",       "rpm",  0,false,false,0},
    {0x0D, "Speed",     "km/h", 0,false,false,0},
    {0x04, "Eng.Load",  "%",    0,false,false,0},
    {0x05, "Coolant",   "C",    0,false,false,0},
    {0x0E, "Timing",    "deg",  0,false,false,0},
    {0x5C, "Oil Temp",  "C",    0,false,false,0},
    {0x42, "ECU Volt",  "V",    0,false,false,0},
    /* ── Throttle / air ───────────────────────────────────── */
    {0x11, "Throttle",  "%",    0,false,false,0},
    {0x0F, "Intake T",  "C",    0,false,false,0},
    {0x0B, "MAP",       "kPa",  0,false,false,0},
    {0x10, "MAF",       "g/s",  0,false,false,0},
    {0x49, "Pedal",     "%",    0,false,false,0},  // accel pedal pos D
    /* ── Turbo / boost ────────────────────────────────────── */
    {0x6F, "Turbo In",  "kPa",  0,false,false,0},  // compressor inlet pressure
    {0x62, "Torque",    "%",    0,false,false,0},   // actual engine torque %
    /* ── Fuel ─────────────────────────────────────────────── */
    {0x2F, "Fuel Lvl",  "%",    0,false,false,0},
    {0x5E, "Fuel Rate", "L/h",  0,false,false,0},
    {0x0A, "Fuel Pres", "kPa",  0,false,false,0},
    {0x06, "Fuel Tr-S", "%",    0,false,false,0},
    {0x07, "Fuel Tr-L", "%",    0,false,false,0},
    {0x52, "Ethanol",   "%",    0,false,false,0},   // ethanol content
    /* ── Environment / misc ───────────────────────────────── */
    {0x46, "Ambient T", "C",    0,false,false,0},
    {0x33, "Baro",      "kPa",  0,false,false,0},
    {0x1F, "Run Time",  "s",    0,false,false,0},
    {0x4D, "MIL Dist",  "km",   0,false,false,0},
    /* ── Diagnostics ──────────────────────────────────────── */
    {0x4C, "Thr.Cmd",   "%",    0,false,false,0},   // commanded throttle actuator
    {0x5B, "HV Batt",   "%",    0,false,false,0},   // hybrid battery remaining life
};

int pidIdx(uint8_t id) {
    for (int i = 0; i < N_PIDS; i++) if (pids[i].id == id) return i;
    return -1;
}

float decodePid(uint8_t pid, uint8_t a, uint8_t b) {
    switch (pid) {
        case 0x04: return a * 100.0f / 255.0f;
        case 0x05: case 0x0F: case 0x46: case 0x5C: return (float)a - 40.0f;
        case 0x06: case 0x07: return ((float)a - 128.0f) * 100.0f / 128.0f;
        case 0x0A: return (float)a * 3.0f;
        case 0x0B: case 0x33: return (float)a;
        case 0x0C: return ((float)(a * 256 + b)) / 4.0f;
        case 0x0D: return (float)a;
        case 0x0E: return (float)a / 2.0f - 64.0f;
        case 0x10: return (float)(a * 256 + b) / 100.0f;
        case 0x11: case 0x2F: return (float)a * 100.0f / 255.0f;
        case 0x1F: case 0x4D: return (float)(a * 256 + b);
        case 0x42: return (float)(a * 256 + b) / 1000.0f;
        case 0x49: case 0x4C: case 0x52: case 0x5B: return (float)a * 100.0f / 255.0f;
        case 0x5E: return (float)(a * 256 + b) / 20.0f;
        case 0x62: return (float)a - 125.0f;   // signed engine torque %, -125..+130
        case 0x6F: return (float)a;             // turbo inlet pressure kPa (0-255)
        default:   return (float)a;
    }
}

/* ── OBD2 state machine ──────────────────────────────────────── */
enum OBD2State { OBD_PROBE0, OBD_PROBE20, OBD_PROBE40, OBD_PROBE60, OBD_RUN };
OBD2State obdState  = OBD_PROBE0;
uint32_t  obdLastTx = 0;
uint8_t   obdRunIdx = 0;
uint32_t  suppMask[4] = {0,0,0,0};

bool isPidSupported(uint8_t pid) {
    if (pid == 0) return true;
    uint8_t g = (pid - 1) / 32;
    uint8_t b = 31 - ((pid - 1) % 32);
    return g < 4 && ((suppMask[g] >> b) & 1);
}
void markSupported() {
    for (int i = 0; i < N_PIDS; i++)
        pids[i].supported = isPidSupported(pids[i].id);
}

void obdSend(uint8_t mode, uint8_t pid) {
    struct can_frame f;
    f.can_id  = 0x7DF;
    f.can_dlc = 8;
    f.data[0] = 0x02; f.data[1] = mode; f.data[2] = pid;
    f.data[3] = f.data[4] = f.data[5] = f.data[6] = f.data[7] = 0x00;
    mcp.sendMessage(&f);
    obdLastTx = millis();
}

/* ── DTC storage ──────────────────────────────────────────────── */
#define MAX_DTCS 20
char     dtcCodes[MAX_DTCS][6];
uint8_t  dtcCount   = 0;
bool     dtcRead    = false;
uint32_t dtcLastReq = 0;

static uint8_t isotpBuf[256];
static int     isotpExpected = 0;
static int     isotpReceived = 0;
static uint8_t isotpSN       = 1;
static bool    isotpActive   = false;

void formatDTC(uint8_t b1, uint8_t b2, char* out) {
    const char cats[] = "PCBU";
    out[0] = cats[(b1 >> 6) & 0x03];
    out[1] = '0' + ((b1 >> 4) & 0x03);
    auto hex = [](uint8_t n) -> char { return n < 10 ? '0'+n : 'A'+n-10; };
    out[2] = hex(b1 & 0x0F);
    out[3] = hex((b2 >> 4) & 0x0F);
    out[4] = hex(b2 & 0x0F);
    out[5] = '\0';
}

void parseDTCs(const uint8_t* data, int len) {
    if (len < 1 || data[0] != 0x43) return;
    dtcCount = 0;
    for (int i = 1; i + 1 < len && dtcCount < MAX_DTCS; i += 2) {
        if (data[i] == 0 && data[i+1] == 0) continue;
        formatDTC(data[i], data[i+1], dtcCodes[dtcCount++]);
    }
    dtcRead    = true;
    isotpActive = false;
    Serial.printf("DTCs: %d found\n", dtcCount);
}

void sendFlowControl() {
    struct can_frame fc;
    fc.can_id  = 0x7E0;
    fc.can_dlc = 8;
    fc.data[0] = 0x30; fc.data[1] = 0x00; fc.data[2] = 0x00;
    fc.data[3] = fc.data[4] = fc.data[5] = fc.data[6] = fc.data[7] = 0xCC;
    mcp.sendMessage(&fc);
}

void obdReceive() {
    struct can_frame f;
    while (mcp.readMessage(&f) == MCP2515::ERROR_OK) {
        if (f.can_id < 0x7E8 || f.can_id > 0x7EF) continue;
        if (f.can_dlc < 2) continue;
        carConn = true;
        uint8_t pci = f.data[0];

        if ((pci & 0xF0) == 0x20 && isotpActive) {
            uint8_t sn = pci & 0x0F;
            if (sn != (isotpSN & 0x0F)) { isotpActive = false; continue; }
            isotpSN++;
            int copy = min(7, isotpExpected - isotpReceived);
            memcpy(isotpBuf + isotpReceived, &f.data[1], copy);
            isotpReceived += copy;
            if (isotpReceived >= isotpExpected) parseDTCs(isotpBuf, isotpExpected);
            continue;
        }
        if ((pci & 0xF0) == 0x00) {
            uint8_t len = pci & 0x0F;
            if (len < 1) continue;
            if (f.data[1] == 0x43) { parseDTCs(&f.data[1], len); continue; }
            if (f.data[1] != 0x41 || len < 3) continue;
            uint8_t pid = f.data[2];
            if (pid == 0x00 || pid == 0x20 || pid == 0x40 || pid == 0x60) {
                uint8_t g = pid / 0x20;
                suppMask[g] = ((uint32_t)f.data[3]<<24)|((uint32_t)f.data[4]<<16)
                            | ((uint32_t)f.data[5]<< 8)| (uint32_t)f.data[6];
                markSupported();
                if      (obdState==OBD_PROBE0)  obdState=(suppMask[0]&1)?OBD_PROBE20:OBD_RUN;
                else if (obdState==OBD_PROBE20) obdState=(suppMask[1]&1)?OBD_PROBE40:OBD_RUN;
                else if (obdState==OBD_PROBE40) obdState=(suppMask[2]&1)?OBD_PROBE60:OBD_RUN;
                else                            obdState=OBD_RUN;
                continue;
            }
            int i = pidIdx(pid);
            if (i >= 0) {
                pids[i].val    = decodePid(pid, f.data[3], f.data[4]);
                pids[i].active = true;
                pids[i].ts     = millis();
            }
            continue;
        }
        if ((pci & 0xF0) == 0x10) {
            uint16_t total = (uint16_t)(pci & 0x0F) << 8 | f.data[1];
            if (total > (uint16_t)sizeof(isotpBuf)) total = sizeof(isotpBuf);
            isotpExpected = total;
            isotpReceived = 0;
            isotpSN       = 1;
            isotpActive   = true;
            int copy = min(6, (int)total);
            memcpy(isotpBuf, &f.data[2], copy);
            isotpReceived = copy;
            sendFlowControl();
        }
    }
}

void obdSendDTC() {
    struct can_frame f;
    f.can_id  = 0x7DF; f.can_dlc = 8;
    f.data[0] = 0x01; f.data[1] = 0x03;
    f.data[2]=f.data[3]=f.data[4]=f.data[5]=f.data[6]=f.data[7]=0x00;
    mcp.sendMessage(&f);
    dtcLastReq = millis();
}

void obdClearDTC() {
    struct can_frame f;
    f.can_id  = 0x7DF; f.can_dlc = 8;
    f.data[0] = 0x01; f.data[1] = 0x04;
    f.data[2]=f.data[3]=f.data[4]=f.data[5]=f.data[6]=f.data[7]=0x00;
    mcp.sendMessage(&f);
    dtcCount = 0; dtcRead = false;
    Serial.println("Sent DTC clear");
}

/* Switch MCP2515 to a different CAN speed and restart probing */
void tryCanSpeed(uint16_t kbps) {
    mcp.reset();
    MCP2515::ERROR err = (kbps == 250) ? mcp.setBitrate(CAN_250KBPS, MCP_8MHZ)
                                       : mcp.setBitrate(CAN_500KBPS, MCP_8MHZ);
    if (err == MCP2515::ERROR_OK) {
        mcp.setNormalMode();
        canOK    = true;
        canKbps  = kbps;
        obdState = OBD_PROBE0;
        memset(suppMask, 0, sizeof(suppMask));
        for (int i = 0; i < N_PIDS; i++) { pids[i].supported = false; pids[i].active = false; }
        Serial.printf("[CAN] Switched to %d kbps\n", kbps);
    }
}

void obdTick() {
    if (!canOK) return;
    obdReceive();
    uint32_t now = millis();
    for (int i = 0; i < N_PIDS; i++)
        if (pids[i].active && now - pids[i].ts > 5000) {
            pids[i].active = false; carConn = false;
        }

    /* ── Auto CAN speed detection ─────────────────────────────────
     * If no car responds at 500 kbps after 6 s, try 250 kbps.
     * Covers older VAG, Renault, Opel that run 250 kbps.          */
    static uint32_t probe0Start  = 0;
    static bool     triedSlow    = false;
    if (obdState == OBD_PROBE0 && !carConn) {
        if (probe0Start == 0) probe0Start = now;
        if (!triedSlow && canKbps == 500 && now - probe0Start > 6000) {
            triedSlow = true;
            tryCanSpeed(250);
            probe0Start = now;
        } else if (triedSlow && canKbps == 250 && now - probe0Start > 6000) {
            /* Both speeds failed — reset and keep trying 500 kbps */
            tryCanSpeed(500);
            triedSlow   = false;
            probe0Start = 0;
        }
    }
    if (carConn) { probe0Start = 0; triedSlow = false; }

    if (now - obdLastTx < 60) return;
    switch (obdState) {
        case OBD_PROBE0:  obdSend(0x01, 0x00); break;
        case OBD_PROBE20: obdSend(0x01, 0x20); break;
        case OBD_PROBE40: obdSend(0x01, 0x40); break;
        case OBD_PROBE60: obdSend(0x01, 0x60); break;
        case OBD_RUN:
            if (now - dtcLastReq > 15000) { obdSendDTC(); break; }
            for (int t = 0; t < N_PIDS; t++) {
                obdRunIdx = (obdRunIdx + 1) % N_PIDS;
                if (pids[obdRunIdx].supported) { obdSend(0x01, pids[obdRunIdx].id); break; }
            }
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Demo mode — simulated drive cycle
 * ═══════════════════════════════════════════════════════════════════ */
enum DemoPhase { D_COLD_IDLE, D_WARMUP, D_ACCEL, D_CRUISE, D_DECEL, D_IDLE };
DemoPhase demoPhase   = D_COLD_IDLE;
uint32_t  demoPhaseTs = 0;

static inline float lp(float cur, float tgt, float alpha) {
    return cur + (tgt - cur) * alpha;
}

static float demoCoolant  = 20.0f;
static float demoOilTemp  = 18.0f;
static float demoFuelLvl  = 75.0f;
static float sTurboIn     = 101.3f; // turbo inlet pressure kPa
static float sRpm=800, sSpd=0, sLoad=12, sThrottle=5, sMAF=2.0f, sMap=35.0f;

void demoTick() {
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    if (now - lastMs < 20) return;
    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;

    uint32_t elapsed = now - demoPhaseTs;
    switch (demoPhase) {
        case D_COLD_IDLE: if (elapsed > 6000)  { demoPhase=D_WARMUP; demoPhaseTs=now; } break;
        case D_WARMUP:    if (elapsed > 8000)  { demoPhase=D_ACCEL;  demoPhaseTs=now; } break;
        case D_ACCEL:     if (elapsed > 7000)  { demoPhase=D_CRUISE; demoPhaseTs=now; } break;
        case D_CRUISE:    if (elapsed > 12000) { demoPhase=D_DECEL;  demoPhaseTs=now; } break;
        case D_DECEL:     if (elapsed > 6000)  { demoPhase=D_IDLE;   demoPhaseTs=now; } break;
        case D_IDLE:      if (elapsed > 5000)  { demoPhase=D_ACCEL;  demoPhaseTs=now; } break;
    }

    float tRpm, tSpd, tLoad, tThrottle;
    float t = elapsed / 1000.0f;
    float jitter = sinf(now/400.0f)*30.0f + sinf(now/170.0f)*15.0f;

    switch (demoPhase) {
        case D_COLD_IDLE: tRpm=1100+jitter*0.5f; tSpd=0; tLoad=18; tThrottle=7; break;
        case D_WARMUP:    tRpm=900+jitter*0.3f;  tSpd=0; tLoad=14; tThrottle=5; break;
        case D_ACCEL:     tRpm=1200+t*380+jitter; tSpd=t*14.0f; tLoad=55+t*4; tThrottle=45+t*3; break;
        case D_CRUISE:    tRpm=2400+sinf(t*0.8f)*200+jitter; tSpd=108+sinf(t*0.5f)*12; tLoad=38+sinf(t*1.2f)*8; tThrottle=28+sinf(t)*6; break;
        case D_DECEL:     tRpm=2400-t*220+jitter*0.5f; tSpd=108-t*16; tLoad=20-t*2; tThrottle=8; break;
        case D_IDLE:
        default:          tRpm=820+jitter*0.4f; tSpd=0; tLoad=12; tThrottle=4; break;
    }
    tRpm=constrain(tRpm,650,7200); tSpd=constrain(tSpd,0,200);
    tLoad=constrain(tLoad,5,98);   tThrottle=constrain(tThrottle,3,95);

    float fast=0.15f, med=0.06f, slow=0.008f, vslow=0.002f;
    sRpm=lp(sRpm,tRpm,fast); sSpd=lp(sSpd,tSpd,med);
    sLoad=lp(sLoad,tLoad,med); sThrottle=lp(sThrottle,tThrottle,fast);
    sMAF=lp(sMAF,sRpm*sLoad*0.00012f,med);
    sMap=lp(sMap,100.0f-sThrottle*0.6f,fast);
    demoCoolant=lp(demoCoolant,(demoPhase==D_COLD_IDLE)?40.0f:91.0f,slow);
    demoOilTemp=lp(demoOilTemp,demoCoolant-5.0f,vslow);
    if (sSpd>5) demoFuelLvl-=dt*0.0003f;
    demoFuelLvl=constrain(demoFuelLvl,0,100);

    auto set=[&](uint8_t id,float v){
        int i=pidIdx(id); if(i<0)return;
        pids[i].val=v; pids[i].active=true; pids[i].supported=true; pids[i].ts=now;
    };

    float fuelTrim=(sLoad>60)?-2.5f:1.5f+sinf(now/3000.0f)*1.5f;
    float timing=8.0f+sRpm*0.003f-sLoad*0.05f;
    float voltage=13.8f+sinf(now/8000.0f)*0.3f;
    /* turbo inlet: atmospheric at idle, boosted under load */
    float turboTarget = (sLoad > 45.0f) ? (101.3f + (sLoad - 45.0f) * 2.6f) : 101.3f;
    sTurboIn = lp(sTurboIn, turboTarget, 0.08f);
    sTurboIn = constrain(sTurboIn, 95.0f, 240.0f);

    float torquePct = constrain((sLoad - 8.0f) * 1.15f + sinf(now/900.0f)*4.0f, -10.0f, 125.0f);
    float pedalPct  = sThrottle * 0.97f + sinf(now/1500.0f)*1.5f;

    set(0x0C,sRpm); set(0x0D,sSpd); set(0x04,sLoad); set(0x05,demoCoolant);
    set(0x11,sThrottle); set(0x0F,22.0f+sinf(now/20000.0f)*3.0f);
    set(0x0B,sMap); set(0x10,sMAF); set(0x0E,timing); set(0x5C,demoOilTemp);
    set(0x2F,demoFuelLvl); set(0x5E,sMAF*0.38f); set(0x42,voltage);
    set(0x46,21.0f); set(0x33,101.3f); set(0x06,fuelTrim); set(0x07,fuelTrim*0.4f);
    set(0x0A,350.0f+sRpm*0.01f); set(0x1F,now/1000); set(0x4D,0.0f);
    set(0x6F, sTurboIn);
    set(0x62, torquePct);
    set(0x49, constrain(pedalPct, 0.0f, 100.0f));
    set(0x52, 9.8f);      // E10 fuel — 9.8% ethanol (fixed)
    set(0x4C, sThrottle); // commanded throttle ≈ actual throttle in demo
    set(0x5B, 0.0f);      // no hybrid battery in demo (will show N/A if not supported)

    if (!dtcRead) {
        dtcCount=3;
        strncpy(dtcCodes[0],"P0128",6); strncpy(dtcCodes[1],"P0171",6);
        strncpy(dtcCodes[2],"P0300",6); dtcRead=true;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Display
 * ═══════════════════════════════════════════════════════════════════ */
TFT_eSPI    tft;
TFT_eSprite spr(&tft);

#define N_PAGES    9   // 0=Dash 1=Engine 2=Fuel 3=AllPIDs 4=DTC 5=WiFi 6=Settings 7=Trip 8=Race
int      currentPage = 0;
int      currentFps  = 0;
uint32_t fpsTimer    = 0;
uint32_t fpsCount    = 0;

/* ── Configurable PID slots (long-press a tile to reassign) ──── */
uint8_t dashIds[4]   = {0x0C, 0x6F, 0x05, 0x42};  // RPM, Turbo In, Coolant, ECU Volt
uint8_t engineIds[8] = {0x11, 0x0F, 0x0B, 0x10, 0x0E, 0x5C, 0x06, 0x42};
uint8_t fuelIds[8]   = {0x2F, 0x5E, 0x0A, 0x33, 0x46, 0x1F, 0x07, 0x4D};

/* ── 0-100 km/h launch timer ─────────────────────────────────── */
enum LaunchState { L_IDLE, L_READY, L_RUNNING, L_DONE };
LaunchState launchState  = L_IDLE;
uint32_t    launchStart  = 0;
float       launchResult = 0.0f;
uint32_t    launchShowTs = 0;

void launchTick() {
    int spdI = pidIdx(0x0D);
    if (spdI < 0 || !pids[spdI].active) return;
    float spd = pids[spdI].val;
    switch (launchState) {
        case L_IDLE:
        case L_DONE:
            if (spd < 5.0f) launchState = L_READY;
            break;
        case L_READY:
            if (spd >= 8.0f) { launchState = L_RUNNING; launchStart = millis(); }
            break;
        case L_RUNNING:
            if (spd < 5.0f) { launchState = L_READY; break; }  // aborted
            if (spd >= 100.0f) {
                launchResult = (millis() - launchStart) / 1000.0f;
                launchState  = L_DONE;
                launchShowTs = millis();
                Serial.printf("[Launch] 0-100: %.2f s\n", (double)launchResult);
            }
            break;
    }
}

/* ── Trip computer (Page 8) ──────────────────────────────────── */
struct TripData {
    float    distKm;
    float    fuelL;
    float    speedSum;
    uint32_t speedN;
    float    maxSpeedKmh;
    float    maxRpmRpm;
    float    maxBoostKPa;
    uint32_t startMs;
};
TripData trip    = {};
uint32_t tripLastTs = 0;

/* ── PID picker overlay state ────────────────────────────────── */
bool pickerOpen      = false;
int  pickerPage      = 0;   // page that opened the picker (0–2)
int  pickerSlot      = 0;   // slot index being reassigned
int  pickerSelIdx    = 0;   // PID index (0–N_PIDS-1) centred in roller
int  pickerScrollAcc = 0;   // sub-item drag accumulator (pixels)
int  pickerPrevY     = 0;   // last Y for incremental drag

/* ═══════════════════════════════════════════════════════════════════
 *  Boot Screen
 *
 *  stepsDone: 0=none, 1=Display, 2=Memory, 3=Touch, 4=CAN, 5=Ready
 *  statusMsg: current step description
 * ═══════════════════════════════════════════════════════════════════ */
void bootFrame(int stepsDone, const char* statusMsg) {
    const int N_STEPS = 5;
    spr.fillSprite(0x0009u);   // very dark blue-black

    /* ── 3px cyan accent line at very top ──────────────────────── */
    spr.fillRect(0, 0, SCREEN_W, 3, C_ACCENT);

    /* ── Header strip (3..43) ────────────────────────────────── */
    spr.fillRect(0, 3, SCREEN_W, 41, C_PANEL);
    spr.drawFastHLine(0, 43, SCREEN_W, C_BORDER);

    spr.setTextSize(3);
    spr.setTextColor(C_ACCENT, C_PANEL);
    spr.setCursor(12, 8);
    spr.print("OBD2");
    spr.setTextColor(C_TEXT, C_PANEL);
    spr.print(" READER");

    spr.setTextSize(1);
    spr.setTextColor(C_NA, C_PANEL);
    spr.setCursor(12, 32);
    spr.print("T-Display S3-Long  \xb7  ESP32-S3 240 MHz  \xb7  CAN OBD2 Scanner");

    spr.setTextColor(C_DIM, C_PANEL);
    spr.setCursor(SCREEN_W - 36, 32);
    spr.print("v2.1");

    /* ── Step pills — horizontal, 5 across (45..78) ──────────── */
    const char* stepNames[5] = {"Display", "Memory", "Touch", "CAN Bus", "Ready!"};
    const int PILL_W = SCREEN_W / N_STEPS;    // 128 px each
    const int PILL_Y = 46;
    const int PILL_H = 28;

    for (int i = 0; i < N_STEPS; i++) {
        int px = i * PILL_W;

        uint16_t bgCol, acCol;
        const char* badge;

        if (i < stepsDone) {
            bgCol = 0x0324u; acCol = C_GREEN;  badge = "OK";
        } else if (i == stepsDone && stepsDone < N_STEPS) {
            bgCol = 0x1082u; acCol = C_YELLOW; badge = "...";
        } else {
            bgCol = C_PANEL; acCol = C_DIM;    badge = "--";
        }

        /* Pill body */
        spr.fillRect(px + 1, PILL_Y, PILL_W - 2, PILL_H, bgCol);
        /* Left color accent bar (5px) */
        spr.fillRect(px + 1, PILL_Y, 5, PILL_H, acCol);
        /* Bottom border */
        spr.drawFastHLine(px + 1, PILL_Y + PILL_H - 1, PILL_W - 2, acCol);

        /* Step label */
        spr.setTextSize(1);
        spr.setTextColor(acCol, bgCol);
        spr.setCursor(px + 9, PILL_Y + 6);
        spr.print(stepNames[i]);

        /* Status badge */
        spr.setTextColor(i < stepsDone ? C_GREEN : (i == stepsDone ? C_YELLOW : C_DIM), bgCol);
        spr.setCursor(px + 9, PILL_Y + 17);
        spr.print(badge);
    }

    /* ── Progress bar (76..81) ───────────────────────────────── */
    const int PB_Y = PILL_Y + PILL_H + 2;     // 76
    spr.fillRect(0, PB_Y, SCREEN_W, 6, C_PANEL);
    int fillW = (int)((long)SCREEN_W * stepsDone / N_STEPS);
    if (fillW > 0) {
        spr.fillRect(0, PB_Y, fillW, 6, C_ACCENT);
        if (fillW >= 4) spr.fillRect(fillW - 4, PB_Y, 4, 6, 0x9FFFu);  // bright tip
    }

    /* ── Status message (84) ─────────────────────────────────── */
    spr.setTextSize(1);
    spr.setTextColor(stepsDone == N_STEPS ? C_GREEN : C_YELLOW, 0x0009u);
    spr.setCursor(4, PB_Y + 9);
    spr.print(statusMsg);

    /* ── Bottom info bar ──────────────────────────────────────── */
    spr.fillRect(0, SCREEN_H - 16, SCREEN_W, 16, C_PANEL);
    spr.drawFastHLine(0, SCREEN_H - 16, SCREEN_W, C_BORDER);
    spr.setTextSize(1);
    spr.setTextColor(C_NA, C_PANEL);
    spr.setCursor(4, SCREEN_H - 10);
    spr.printf("Heap: %d B   PSRAM: %d B   Flash: %d MB",
        ESP.getFreeHeap(), ESP.getFreePsram(),
        ESP.getFlashChipSize() / (1024 * 1024));

    lcd_PushFrame((uint16_t*)spr.getPointer());
}

/* ── Value colour — traffic-light based on PID range ─────────── */
uint16_t valColor(uint8_t pid, float v) {
    switch(pid) {
        case 0x0C: return v>6500?C_DANGER:v>5000?C_WARN:C_VALUE;
        case 0x05: case 0x5C: return v>(float)(cfg.coolWarn+15)?C_DANGER:v>(float)cfg.coolWarn?C_WARN:C_VALUE;
        case 0x2F: return v<10?C_DANGER:v<20?C_WARN:C_VALUE;
        case 0x42: return v<11.5f?C_DANGER:v<12.5f?C_WARN:C_VALUE;
        case 0x04: case 0x11: return v>90?C_DANGER:v>75?C_WARN:C_VALUE;
        case 0x6F: return v>200?C_DANGER:v>160?C_WARN:C_VALUE;  // turbo inlet pressure
        case 0x62: return v>90?C_DANGER:v>70?C_WARN:C_VALUE;    // engine torque %
        case 0x5B: return v<15?C_DANGER:v<30?C_WARN:C_VALUE;    // HV battery low
        default:   return C_VALUE;
    }
}
/* keep old barColor for AllOBD compat */
uint16_t barColor(int pi) { return valColor(pids[pi].id, pids[pi].val); }

/* ── Status bar (14 px) ──────────────────────────────────────── */
void drawStatusBar() {
    spr.fillRect(0, 0, SCREEN_W, 14, C_BG);
    spr.drawFastHLine(0, 13, SCREEN_W, C_ACCENT);  // accent separator
    spr.setTextSize(1);

    /* Left status */
    uint16_t sc; const char* st;
    if (demoMode) {
        const char* ph[]={"COLD","WARM","ACCEL","CRUISE","DECEL","IDLE"};
        sc=C_WARN; char tmp[20]; snprintf(tmp,sizeof(tmp),"DEMO %s",ph[(int)demoPhase]);
        spr.fillCircle(5,7,3,sc); spr.setTextColor(sc,C_BG); spr.setCursor(11,3); spr.print(tmp);
    } else if (!canOK) {
        spr.fillCircle(5,7,3,C_DANGER); spr.setTextColor(C_DANGER,C_BG); spr.setCursor(11,3); spr.print("NO CAN");
    } else if (!carConn) {
        spr.fillCircle(5,7,3,C_WARN); spr.setTextColor(C_WARN,C_BG); spr.setCursor(11,3);
        spr.printf("SEARCHING %dK", canKbps);
    } else if (obdState!=OBD_RUN) {
        spr.fillCircle(5,7,3,C_WARN); spr.setTextColor(C_WARN,C_BG); spr.setCursor(11,3); spr.print("PROBING");
    } else {
        int n=0; for(int i=0;i<N_PIDS;i++) if(pids[i].active) n++;
        spr.fillCircle(5,7,3,C_OK); spr.setTextColor(C_OK,C_BG); spr.setCursor(11,3);
        spr.printf("OBD2  %dPID", n);
    }

    /* Centre: current page name */
    static const char* pn[]={"DASH","ENGINE","FUEL","ALL PIDs","DTC","WIFI","SETTINGS","TRIP","RACE"};
    int nw=strlen(pn[currentPage])*6;
    spr.setTextColor(C_LABEL,C_BG); spr.setCursor(SCREEN_W/2-nw/2, 3); spr.print(pn[currentPage]);
    /* Active page underline */
    spr.drawFastHLine(SCREEN_W/2-nw/2, 12, nw, C_ACCENT);

    /* Right: WiFi | FPS */
    if (wifiOn) {
        spr.setTextColor(C_ACCENT, C_BG); spr.setCursor(SCREEN_W-62, 3);
        spr.print("WiFi");
    }
    spr.setTextColor(C_LINE2, C_BG); spr.setCursor(SCREEN_W-30, 3);
    spr.printf("%dFPS", currentFps);
}

/* ── Max scale values per PID ────────────────────────────────── */
float defaultMax(uint8_t pid) {
    switch(pid) {
        case 0x0C: return (float)cfg.rpmMax;
        case 0x0D: return (float)cfg.speedMax;
        case 0x05: case 0x0F: case 0x46: case 0x5C: return 150.0f;
        case 0x04: case 0x11: case 0x2F: return 100.0f;
        case 0x0B: case 0x6F: return (float)cfg.mapMax;
        case 0x33: return 120.0f;
        case 0x10: return 50.0f;   case 0x0A: return 500.0f;
        case 0x42: return 16.0f;   case 0x5E: return 30.0f;
        case 0x62: return 130.0f;
        case 0x49: case 0x4C: case 0x52: case 0x5B: return 100.0f;
        default:   return 100.0f;
    }
}

/* ── Flat minimal cell — no bg fill, caller fills page bg ────── *
 *  Label: small gray top-left                                    *
 *  Value: large white (or traffic-light colour) centre-left      *
 *  Unit:  small gray bottom-right                                *
 *  Bar:   2px accent line at very bottom                         *
 * ─────────────────────────────────────────────────────────────── */
void drawCell(int x,int y,int w,int h,const char* lbl,float val,const char* unit,
              bool active, uint8_t pid=0, float maxV=100.0f) {
    /* Label — size 1 */
    spr.setTextSize(1);
    spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(x+8, y+5); spr.print(lbl);

    if (!active) {
        spr.setTextColor(C_DIM, C_BG);
        spr.setCursor(x+8, y+h/2-4); spr.print("--");
        return;
    }

    /* Convert value + unit for display */
    float dVal = convertVal(pid, val);
    const char* dUnit = convertUnit(pid, unit);

    /* Value — size 3 */
    char vbuf[12];
    if      (fabsf(dVal)>=10000||dVal<=-1000) snprintf(vbuf,sizeof(vbuf),"%.0f",(double)dVal);
    else if (fabsf(dVal)>=100)                snprintf(vbuf,sizeof(vbuf),"%.1f",(double)dVal);
    else                                      snprintf(vbuf,sizeof(vbuf),"%.2f",(double)dVal);

    spr.setTextSize(3);
    spr.setTextColor(valColor(pid,val), C_BG);
    spr.setCursor(x+8, y+h/2-12); spr.print(vbuf);

    /* Unit — size 2 for easy reading */
    spr.setTextSize(2); spr.setTextColor(C_LABEL, C_BG);
    int uw = strlen(dUnit)*12;
    spr.setCursor(x+w-uw-6, y+h-20); spr.print(dUnit);

    /* 2px progress bar at bottom — uses raw val/maxV for correct ratio */
    if (maxV>0) {
        float pct=constrain(val/maxV,0.0f,1.0f);
        uint16_t bc=(pct>0.85f)?C_DANGER:(pct>0.65f)?C_WARN:C_ACCENT;
        spr.drawFastHLine(x, y+h-2, w, C_LINE);
        if(pct>0) spr.drawFastHLine(x, y+h-2, (int)(w*pct), bc);
        spr.drawFastHLine(x, y+h-1, w, C_LINE);
        if(pct>0) spr.drawFastHLine(x, y+h-1, (int)(w*pct), bc);
    }
}
void drawPidCell(int x,int y,int w,int h,int pi) {
    drawCell(x,y,w,h,pids[pi].label,pids[pi].val,pids[pi].unit,
             pids[pi].active,pids[pi].id,defaultMax(pids[pi].id));
}

/* ── Page 0: Dashboard — 4 big flat gauges ───────────────────── */
void drawBigGauge(int x,int y,int w,int h,int pi,float maxVal) {
    bool  act  = pids[pi].active;
    float raw  = pids[pi].val;
    float pct  = act ? constrain(raw/maxVal,0.0f,1.0f) : 0.0f;
    float dVal = convertVal(pids[pi].id, raw);
    const char* dUnit = convertUnit(pids[pi].id, pids[pi].unit);
    uint16_t vc = act ? valColor(pids[pi].id, raw) : C_DIM;

    /* Label */
    spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(x+10, y+6); spr.print(pids[pi].label);

    /* Percentage — top right */
    if (act) {
        spr.setTextColor(C_LINE2, C_BG);
        spr.setCursor(x+w-30, y+6);
        spr.printf("%3.0f%%",(double)(pct*100));
    }

    /* Value — very large */
    spr.setTextSize(4); spr.setTextColor(vc, C_BG);
    spr.setCursor(x+10, y+16);
    if (!act) spr.print("N/A");
    else if (pids[pi].id==0x0C) spr.printf("%.0f",(double)dVal);
    else                         spr.printf("%.1f",(double)dVal);

    /* Unit — size 2 for readability */
    spr.setTextSize(2); spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(x+10, y+h-20); spr.print(dUnit);

    /* Thin bar — 3px, bottom */
    spr.drawFastHLine(x, y+h-3, w, C_LINE);
    spr.drawFastHLine(x, y+h-2, w, C_LINE);
    spr.drawFastHLine(x, y+h-1, w, C_LINE);
    if (act && pct>0) {
        spr.drawFastHLine(x, y+h-3, (int)(w*pct), vc);
        spr.drawFastHLine(x, y+h-2, (int)(w*pct), vc);
        spr.drawFastHLine(x, y+h-1, (int)(w*pct), vc);
    }
}

/* Draw grid lines over black background */
void drawGrid4x2(int Y0) {
    int cw=SCREEN_W/4, ch=(SCREEN_H-Y0)/2;
    /* vertical dividers */
    for(int c=1;c<4;c++) spr.drawFastVLine(c*cw, Y0+6, SCREEN_H-Y0-12, C_LINE);
    /* horizontal divider */
    spr.drawFastHLine(10, Y0+ch, SCREEN_W-20, C_LINE);
}
void drawGrid2x2(int Y0) {
    int hw=SCREEN_W/2, hh=(SCREEN_H-Y0)/2;
    spr.drawFastVLine(hw, Y0+6, SCREEN_H-Y0-12, C_LINE);
    spr.drawFastHLine(10, Y0+hh, SCREEN_W-20, C_LINE);
}

void drawDashboard() {
    const int Y0=14, HH=(SCREEN_H-Y0)/2, HW=SCREEN_W/2;
    spr.fillRect(0,Y0,SCREEN_W,SCREEN_H-Y0,C_BG);
    drawBigGauge(0,  Y0,   HW,HH,pidIdx(dashIds[0]),defaultMax(dashIds[0]));
    drawBigGauge(HW, Y0,   HW,HH,pidIdx(dashIds[1]),defaultMax(dashIds[1]));
    drawBigGauge(0,  Y0+HH,HW,HH,pidIdx(dashIds[2]),defaultMax(dashIds[2]));
    drawBigGauge(HW, Y0+HH,HW,HH,pidIdx(dashIds[3]),defaultMax(dashIds[3]));
    drawGrid2x2(Y0);
}

/* ── Page 1: Engine ─────────────────────────────────────────── */
void drawEngine() {
    const int Y0=14, COLS=4, ROWS=2;
    int cw=SCREEN_W/COLS, ch=(SCREEN_H-Y0)/ROWS;
    spr.fillRect(0,Y0,SCREEN_W,SCREEN_H-Y0,C_BG);
    for(int i=0;i<8;i++){
        int p=pidIdx(engineIds[i]), x=(i%COLS)*cw, y=Y0+(i/COLS)*ch;
        if(p>=0) drawPidCell(x,y,cw,ch,p); else drawCell(x,y,cw,ch,"---",0,"",false);
    }
    drawGrid4x2(Y0);
}

/* ── Page 2: Fuel ───────────────────────────────────────────── */
void drawFuel() {
    const int Y0=14, COLS=4, ROWS=2;
    int cw=SCREEN_W/COLS, ch=(SCREEN_H-Y0)/ROWS;
    spr.fillRect(0,Y0,SCREEN_W,SCREEN_H-Y0,C_BG);
    for(int i=0;i<8;i++){
        int p=pidIdx(fuelIds[i]), x=(i%COLS)*cw, y=Y0+(i/COLS)*ch;
        if(p>=0) drawPidCell(x,y,cw,ch,p); else drawCell(x,y,cw,ch,"---",0,"",false);
    }
    drawGrid4x2(Y0);
}

/* ── Page 3: All OBD2 — compact 3-col × 9-row table ──────── */
void drawAllOBD() {
    const int Y0   = 14;
    const int COLS = 3;
    const int ROWS = (N_PIDS + COLS - 1) / COLS;  // ceil(26/3) = 9
    const int CW   = SCREEN_W / COLS;              // ≈213 px per column
    const int RH   = (SCREEN_H - Y0) / ROWS;       // ≈18 px per row
    spr.fillRect(0, Y0, SCREEN_W, SCREEN_H-Y0, C_BG);

    spr.setTextSize(1);
    for (int i = 0; i < N_PIDS; i++) {
        int col = i / ROWS;
        int row = i % ROWS;
        int x   = col * CW;
        int y   = Y0 + row * RH;

        /* Alternate row tint */
        uint16_t rowBg = (row & 1) ? 0x0821u : C_BG;
        spr.fillRect(x, y, CW, RH, rowBg);

        /* Label */
        spr.setTextColor(C_LABEL, rowBg);
        spr.setCursor(x + 3, y + (RH-8)/2);
        spr.printf("%-9s", pids[i].label);

        /* Value — apply unit conversion */
        float raw = pids[i].val;
        float dv  = convertVal(pids[i].id, raw);
        const char* du = convertUnit(pids[i].id, pids[i].unit);
        uint16_t vc = pids[i].active ? valColor(pids[i].id, raw) : C_DIM;
        spr.setTextColor(vc, rowBg);
        if (pids[i].active) {
            char vbuf[10];
            if      (fabsf(dv)>=10000) snprintf(vbuf,sizeof(vbuf),"%.0f",(double)dv);
            else if (fabsf(dv)>=100)   snprintf(vbuf,sizeof(vbuf),"%.1f",(double)dv);
            else                       snprintf(vbuf,sizeof(vbuf),"%.2f",(double)dv);
            spr.setCursor(x + 58, y + (RH-8)/2);
            spr.printf("%6s%-4s", vbuf, du);
        } else {
            spr.setCursor(x + 58, y + (RH-8)/2);
            spr.print("   --    ");
        }
    }
    /* Column dividers */
    for (int c = 1; c < COLS; c++)
        spr.drawFastVLine(c * CW, Y0+2, SCREEN_H-Y0-4, C_LINE);
    /* Horizontal separators */
    for (int r = 1; r < ROWS; r++)
        spr.drawFastHLine(2, Y0+r*RH, SCREEN_W-4, C_LINE);
}

/* ── Page 4: DTC fault codes ─────────────────────────────────── */
void drawDTC() {
    const int Y0     = 14;
    const int HBAR   = 22;    // header bar height
    const int LIST_Y = Y0 + HBAR;
    const int COLS   = 3;
    const int CW     = SCREEN_W / COLS;       // ≈213 px
    const int ROW_H  = 38;                    // taller rows for bigger text
    // max rows per column: (SCREEN_H - LIST_Y) / ROW_H
    const int MAX_ROWS = (SCREEN_H - LIST_Y) / ROW_H;  // ≈3
    const int MAX_VIS  = MAX_ROWS * COLS;               // ≈9

    spr.fillRect(0, Y0, SCREEN_W, SCREEN_H-Y0, C_BG);

    /* ── Header bar ── */
    spr.fillRect(0, Y0, SCREEN_W, HBAR, 0x1082u);
    spr.setTextSize(2);
    spr.setTextColor(C_TEXT, 0x1082u);
    spr.setCursor(6, Y0+3);
    if (!carConn && !demoMode) {
        spr.print("DTC  --  no car");
    } else if (!dtcRead) {
        spr.setTextColor(C_WARN, 0x1082u); spr.print("Reading DTCs...");
    } else if (dtcCount == 0) {
        spr.setTextColor(C_GREEN, 0x1082u); spr.print("DTC  --  ALL CLEAR");
    } else {
        spr.setTextColor(C_RED, 0x1082u);
        spr.printf("DTC  --  %d FAULT%s", dtcCount, dtcCount>1?"S":"");
    }

    /* ── DTC description helper ── */
    auto dtcDesc=[](const char* c)->const char*{
        if(!c)return"";
        if(strcmp(c,"P0100")==0)return"MAF sensor";
        if(strcmp(c,"P0101")==0)return"MAF range/perf";
        if(strcmp(c,"P0110")==0)return"IAT sensor";
        if(strcmp(c,"P0115")==0)return"ECT sensor";
        if(strcmp(c,"P0120")==0)return"TPS sensor";
        if(strcmp(c,"P0128")==0)return"Coolant low temp";
        if(strcmp(c,"P0130")==0)return"O2 B1S1";
        if(strcmp(c,"P0171")==0)return"Sys lean B1";
        if(strcmp(c,"P0172")==0)return"Sys rich B1";
        if(strcmp(c,"P0174")==0)return"Sys lean B2";
        if(strcmp(c,"P0201")==0)return"Injector 1";
        if(strcmp(c,"P0300")==0)return"Random misfire";
        if(strcmp(c,"P0301")==0)return"Cyl 1 misfire";
        if(strcmp(c,"P0302")==0)return"Cyl 2 misfire";
        if(strcmp(c,"P0303")==0)return"Cyl 3 misfire";
        if(strcmp(c,"P0304")==0)return"Cyl 4 misfire";
        if(strcmp(c,"P0305")==0)return"Cyl 5 misfire";
        if(strcmp(c,"P0306")==0)return"Cyl 6 misfire";
        if(strcmp(c,"P0400")==0)return"EGR flow";
        if(strcmp(c,"P0420")==0)return"Cat eff B1 low";
        if(strcmp(c,"P0440")==0)return"EVAP system";
        if(strcmp(c,"P0500")==0)return"VSS sensor";
        if(strcmp(c,"P0505")==0)return"Idle ctrl";
        if(strcmp(c,"P0600")==0)return"CAN comm";
        if(strcmp(c,"P0700")==0)return"TCM fault";
        if(strncmp(c,"P",1)==0)return"Powertrain";
        if(strncmp(c,"C",1)==0)return"Chassis";
        if(strncmp(c,"B",1)==0)return"Body";
        if(strncmp(c,"U",1)==0)return"Network";
        return"Unknown";
    };

    if (dtcCount==0 && dtcRead) {
        /* Big ALL CLEAR */
        spr.setTextSize(4); spr.setTextColor(C_GREEN, C_BG);
        int tw = 9*24; // "ALL CLEAR" = 9 chars at size4 (24px each)
        spr.setCursor(SCREEN_W/2 - tw/2, LIST_Y+28);
        spr.print("ALL CLEAR");
    } else {
        int vis = min((int)dtcCount, MAX_VIS);
        for (int i = 0; i < vis; i++) {
            int cx = (i % COLS) * CW;
            int cy = LIST_Y + (i / COLS) * ROW_H;
            spr.fillRect(cx+1, cy+1, CW-2, ROW_H-2, 0x1862u);
            spr.drawRect(cx, cy, CW, ROW_H, C_BORDER);

            /* DTC code — size 2 */
            spr.setTextSize(2); spr.setTextColor(C_RED, 0x1862u);
            spr.setCursor(cx+5, cy+4);
            spr.print(dtcCodes[i]);

            /* Description — size 1 below the code */
            spr.setTextSize(1); spr.setTextColor(C_TEXT, 0x1862u);
            spr.setCursor(cx+5, cy+24);
            spr.print(dtcDesc(dtcCodes[i]));
        }
        if (dtcCount > MAX_VIS) {
            spr.setTextSize(1); spr.setTextColor(C_NA, C_BG);
            spr.setCursor(4, LIST_Y + MAX_ROWS*ROW_H + 2);
            spr.printf("...and %d more", dtcCount - MAX_VIS);
        }
    }

    spr.setTextSize(1); spr.setTextColor(C_NA, C_BG);
    spr.setCursor(SCREEN_W-160, SCREEN_H-11);
    spr.print("Hold 2s to clear all DTCs");
}

/* ── Page 5: ESP32 system info ───────────────────────────────── */
/* ── Page 8: Race ────────────────────────────────────────────── */
static uint32_t raceResetTs = 0;   // timestamp of last reset (for flash)

void drawRace() {
    const int Y0 = 14;
    spr.fillRect(0, Y0, SCREEN_W, SCREEN_H - Y0, C_BG);

    /* ── Shift light strip ── */
    const int SH = 10;
    int rpmI = pidIdx(0x0C);
    float rpm    = (rpmI >= 0 && pids[rpmI].active) ? pids[rpmI].val : 0.0f;
    float rpmPct = constrain(rpm / (float)cfg.rpmMax, 0.0f, 1.0f);
    const int N_LED = 20, LED_W = SCREEN_W / N_LED;
    int litN = (int)(rpmPct * N_LED);
    for (int i = 0; i < N_LED; i++) {
        uint16_t col = (i >= litN) ? C_LINE :
                       (i < 10)   ? C_OK   :
                       (i < 16)   ? C_WARN : C_DANGER;
        spr.fillRect(i * LED_W + 1, Y0 + 1, LED_W - 2, SH - 2, col);
    }

    const int CY = Y0 + SH + 2;
    const int CH = SCREEN_H - CY;

    /* Panel boundaries:  Stats 0-419 | Timer 423-639 */
    const int DIV = 420;
    const int RX  = DIV + 4;
    const int RW  = SCREEN_W - DIV;   // ~220 px

    /* ── Left: RPM / Speed / Boost — 3 horizontal columns ── */
    int spdI = pidIdx(0x0D);
    int bstI = pidIdx(0x6F);
    float spd      = (spdI >= 0 && pids[spdI].active) ? pids[spdI].val : 0.0f;
    float boost    = (bstI >= 0 && pids[bstI].active) ? pids[bstI].val : 0.0f;
    float boostDsp = convertVal(0x6F, boost);
    const char* bUnit = convertUnit(0x6F, "kPa");

    const int C0 = 6, C1 = 146, C2 = 286;   // column x-starts
    const int LY = CY + 4;                   // label y
    const int VY = CY + 16;                  // value y

    /* RPM */
    spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(C0, LY); spr.print("RPM");
    spr.setTextSize(4); spr.setTextColor(valColor(0x0C, rpm), C_BG);
    spr.setCursor(C0, VY);
    if (rpmI >= 0 && pids[rpmI].active) spr.printf("%.0f", (double)rpm);
    else { spr.setTextColor(C_DIM, C_BG); spr.print("----"); }

    /* Speed */
    spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(C1, LY); spr.print("km/h");
    spr.setTextSize(4); spr.setTextColor(C_VALUE, C_BG);
    spr.setCursor(C1, VY); spr.printf("%.0f", (double)spd);

    /* Boost */
    spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(C2, LY); spr.print("BOOST");
    spr.setTextSize(4); spr.setTextColor(valColor(0x6F, boost), C_BG);
    spr.setCursor(C2, VY);
    if (bstI >= 0 && pids[bstI].active) spr.printf("%.2f", (double)boostDsp);
    else { spr.setTextColor(C_DIM, C_BG); spr.print("--"); }
    spr.setTextSize(2); spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(C2, VY + 34); spr.print(bUnit);

    spr.drawFastVLine(DIV, CY + 4, CH - 8, C_LINE);

    /* ── Right: 0-100 timer ── */
    spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
    spr.setCursor(RX, CY + 2); spr.print("0 - 100 km/h");

    /* Timer — exclusive states, no overlap */
    if (launchState == L_RUNNING) {
        float t = (millis() - launchStart) / 1000.0f;
        char tbuf[10]; snprintf(tbuf, sizeof(tbuf), "%.1fs", (double)t);
        spr.setTextSize(4); spr.setTextColor(C_WARN, C_BG);
        spr.setCursor(RX, CY + 14); spr.print(tbuf);
    } else if (launchResult > 0.0f) {
        char tbuf[10]; snprintf(tbuf, sizeof(tbuf), "%.2fs", (double)launchResult);
        spr.setTextSize(4); spr.setTextColor(C_ACCENT, C_BG);
        spr.setCursor(RX, CY + 14); spr.print(tbuf);
        /* Status below result */
        spr.setTextSize(2);
        if (launchState == L_READY) {
            spr.setTextColor(C_OK, C_BG);
            spr.setCursor(RX, CY + 52); spr.print("READY");
        }
    } else if (launchState == L_READY) {
        spr.setTextSize(3); spr.setTextColor(C_OK, C_BG);
        spr.setCursor(RX, CY + 14); spr.print("READY");
    } else {
        spr.setTextSize(1); spr.setTextColor(C_DIM, C_BG);
        spr.setCursor(RX, CY + 18); spr.print("slow below 5 km/h");
    }

    /* ── Peaks ── */
    float pkBoost = convertVal(0x6F, trip.maxBoostKPa);
    char pb[14];
    const int PY = CY + 72;
    spr.drawFastHLine(RX, PY - 3, RW - 8, C_LINE);

    /* Label left, value right-aligned — size 2 for readability */
    auto peakRow = [&](int y, const char* lbl, const char* val) {
        spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
        spr.setCursor(RX, y);
        spr.print(lbl);
        spr.setTextSize(2); spr.setTextColor(C_VALUE, C_BG);
        int vw = strlen(val) * 12;
        spr.setCursor(SCREEN_W - vw - 4, y);
        spr.print(val);
    };

    snprintf(pb, sizeof(pb), "%.0f", (double)trip.maxRpmRpm);
    peakRow(PY + 2,  "PEAK RPM", pb);
    snprintf(pb, sizeof(pb), "%.0f km/h", (double)trip.maxSpeedKmh);
    peakRow(PY + 22, "PEAK SPD", pb);
    snprintf(pb, sizeof(pb), "%.2f %s", (double)pkBoost, bUnit);
    peakRow(PY + 42, "PEAK BST", pb);

    /* Reset flash or hint */
    spr.setTextSize(1);
    if (millis() - raceResetTs < 1000) {
        spr.setTextColor(C_OK, C_BG);
        spr.setCursor(RX, CY + CH - 10); spr.print("RESET OK");
    } else {
        spr.setTextColor(C_DIM, C_BG);
        spr.setCursor(RX, CY + CH - 10); spr.print("hold 2s = reset");
    }
}

/* ── PID Picker — full-screen rolling drum ───────────────────── *
 *  Covers full screen (below status bar).                        *
 *  5 visible rows; centre row = selected.  Swipe up/down to roll.*
 *  Tap (little movement) = confirm.   Hold 2s = cancel.          *
 * ───────────────────────────────────────────────────────────── */
void drawPicker() {
    const int PY  = 14;                    // top of picker (below status bar)
    const int PH  = SCREEN_H - PY;        // 166 px
    const int TH  = 24;                   // title bar height
    const int ITH = (PH - TH) / 5;       // item height ≈ 28 px
    const int IY0 = PY + TH;             // items start y

    /* ── Picker background ── */
    spr.fillRect(0, PY, SCREEN_W, PH, C_PANEL);
    spr.drawFastHLine(0, PY,   SCREEN_W, C_VAL);
    spr.drawFastHLine(0, PY+1, SCREEN_W, C_TEAL);

    /* ── Title bar ── */
    spr.fillRect(0, PY, SCREEN_W, TH, C_CARD);
    spr.drawFastHLine(0, PY+TH-1, SCREEN_W, C_SEP);

    const char* pNames[]={"Dash","Engine","Fuel"};
    /* Slot / page label — size 2 */
    spr.setTextSize(2);
    spr.setTextColor(C_ACCENT, C_CARD);
    spr.setCursor(6, PY+4);
    spr.printf("Slot %d  %s", pickerSlot+1, pNames[pickerPage]);

    /* Cancel hint — size 1 right-aligned */
    spr.setTextSize(1);
    spr.setTextColor(C_DIM, C_CARD);
    spr.setCursor(SCREEN_W-76, PY+8);
    spr.print("Hold 2s=cancel");

    /* ── Items: -2 -1 [0] +1 +2 ── */
    for (int offset = -2; offset <= 2; offset++) {
        int idx = pickerSelIdx + offset;
        if (idx < 0 || idx >= N_PIDS) continue;

        int iy   = IY0 + (offset + 2) * ITH;
        bool sel = (offset == 0);

        /* Row background */
        uint16_t bg = sel ? 0x1946u : C_PANEL;
        spr.fillRect(0, iy, SCREEN_W, ITH, bg);

        /* Selection highlight border + accent sidebar */
        if (sel) {
            spr.drawFastHLine(0, iy,        SCREEN_W, C_TEAL);
            spr.drawFastHLine(0, iy+ITH-1,  SCREEN_W, C_TEAL);
            spr.fillRect(0,          iy, 5, ITH, C_ACCENT);
            spr.fillRect(SCREEN_W-5, iy, 5, ITH, C_ACCENT);
        } else {
            spr.drawFastHLine(0, iy, SCREEN_W, C_SEP);
        }

        /* Fade by distance from centre */
        uint16_t tc, vc;
        if (sel) {
            tc = C_TEXT; vc = C_VAL;
        } else if (abs(offset) == 1) {
            tc = C_DIM;  vc = 0x0459u;
        } else {
            tc = 0x2965u; vc = 0x0230u;
        }

        if (sel) {
            /* Selected row — size 2 label + unit */
            int ty = iy + (ITH - 16) / 2;
            spr.setTextSize(2); spr.setTextColor(tc, bg);
            spr.setCursor(12, ty);
            spr.printf("%-10s", pids[idx].label);

            spr.setTextColor(C_DIM, bg);
            spr.setCursor(12 + 10*12 + 4, ty);
            spr.printf("%-5s", pids[idx].unit);

            /* Live value — right aligned, size 2 */
            if (pids[idx].active) {
                char vbuf[12];
                float v = pids[idx].val;
                if (fabsf(v)>=1000) snprintf(vbuf,sizeof(vbuf),"%.0f",(double)v);
                else                snprintf(vbuf,sizeof(vbuf),"%.1f",(double)v);
                int vw = strlen(vbuf) * 12;
                spr.setTextColor(valColor(pids[idx].id, v), bg);
                spr.setCursor(SCREEN_W - vw - 12, ty);
                spr.print(vbuf);
            } else {
                spr.setTextSize(2); spr.setTextColor(C_DIM, bg);
                spr.setCursor(SCREEN_W-36, ty); spr.print("--");
            }
        } else {
            /* Non-selected rows — size 1 */
            int ty = iy + (ITH - 8) / 2;
            spr.setTextSize(1); spr.setTextColor(tc, bg);
            spr.setCursor(12, ty);
            spr.printf("%-12s", pids[idx].label);

            spr.setTextColor(sel ? C_DIM : 0x2965u, bg);
            spr.setCursor(84, ty);
            spr.printf("%-5s", pids[idx].unit);

            if (pids[idx].active) {
                char vbuf[12];
                float v = pids[idx].val;
                if (fabsf(v)>=1000) snprintf(vbuf,sizeof(vbuf),"%.0f",(double)v);
                else                snprintf(vbuf,sizeof(vbuf),"%.2f",(double)v);
                int vw = strlen(vbuf)*6;
                spr.setTextColor(vc, bg);
                spr.setCursor(SCREEN_W - vw - 10, ty); spr.print(vbuf);
            } else {
                spr.setTextColor(0x2965u, bg);
                spr.setCursor(SCREEN_W-22, ty); spr.print("--");
            }
        }
    }

    /* ── Scroll arrows at top/bottom of item area ── */
    spr.setTextSize(2);
    spr.setTextColor(pickerSelIdx>0       ? C_VAL : C_SEP, C_PANEL);
    spr.setCursor(SCREEN_W/2-6, IY0+1); spr.print("^");
    spr.setTextColor(pickerSelIdx<N_PIDS-1 ? C_VAL : C_SEP, C_PANEL);
    spr.setCursor(SCREEN_W/2-6, IY0+5*ITH-20); spr.print("v");
}

/* ── Page 7: Settings ────────────────────────────────────────── */
void drawSettings() {
    const int Y0  = 14;
    const int ROW = (SCREEN_H - Y0) / 6;   // ≈27 px per row
    spr.fillRect(0, Y0, SCREEN_W, SCREEN_H-Y0, C_BG);

    struct SRow { const char* lbl; char val[18]; };
    SRow rows[6];
    rows[0].lbl = "RPM Max";        snprintf(rows[0].val,sizeof(rows[0].val),"%d rpm",    cfg.rpmMax);
    rows[1].lbl = "Speed Max";      snprintf(rows[1].val,sizeof(rows[1].val),"%d km/h",   cfg.speedMax);
    rows[2].lbl = "Boost Max";      snprintf(rows[2].val,sizeof(rows[2].val),"%d kPa",    cfg.mapMax);
    rows[3].lbl = "Coolant Warn";   snprintf(rows[3].val,sizeof(rows[3].val),"%d C",      cfg.coolWarn);
    const char* tNames[]={"Celsius","Fahrenheit"};
    const char* pNames[]={"kPa","bar","psi"};
    rows[4].lbl = "Temp Unit";      snprintf(rows[4].val,sizeof(rows[4].val),"%s",        tNames[cfg.tempUnit]);
    rows[5].lbl = "Pressure Unit";  snprintf(rows[5].val,sizeof(rows[5].val),"%s",        pNames[cfg.pressUnit]);

    for (int i = 0; i < 6; i++) {
        int ry = Y0 + i * ROW;
        uint16_t bg = (i & 1) ? 0x0821u : C_BG;
        spr.fillRect(0, ry, SCREEN_W, ROW, bg);

        /* Left accent bar on active row */
        spr.fillRect(0, ry, 4, ROW, C_ACCENT);

        /* Label — size 2 */
        spr.setTextSize(2); spr.setTextColor(C_LABEL, bg);
        spr.setCursor(10, ry + (ROW-16)/2);
        spr.print(rows[i].lbl);

        /* Value — size 2, accent color, right-aligned */
        spr.setTextColor(C_ACCENT, bg);
        int vw = strlen(rows[i].val)*12;
        spr.setCursor(SCREEN_W - vw - 14, ry + (ROW-16)/2);
        spr.print(rows[i].val);

        /* Tap indicator */
        spr.setTextColor(C_DIM, bg);
        spr.setCursor(SCREEN_W - vw - 28, ry + (ROW-16)/2);
        spr.print(">");

        /* Row separator */
        spr.drawFastHLine(0, ry+ROW-1, SCREEN_W, C_LINE);
    }

    /* Bottom hint */
    spr.setTextSize(1); spr.setTextColor(C_DIM, C_BG);
    spr.setCursor(4, SCREEN_H-10);
    spr.print("Tap row to change  |  Hold 2s to reset defaults");
}

/* ── Page 8: Trip Computer ───────────────────────────────────── */
void tripTick() {
    uint32_t now = millis();
    if (tripLastTs == 0) { tripLastTs = now; return; }
    if (now - tripLastTs < 1000) return;
    float dt = (now - tripLastTs) / 3600000.0f;   // fraction of an hour
    tripLastTs = now;

    int spdI   = pidIdx(0x0D), fuelI = pidIdx(0x5E);
    int rpmI   = pidIdx(0x0C), bstI  = pidIdx(0x6F);

    float spd   = (spdI  >= 0 && pids[spdI ].active) ? pids[spdI ].val : 0.0f;
    float frate = (fuelI >= 0 && pids[fuelI].active) ? pids[fuelI].val : 0.0f;
    float rpm   = (rpmI  >= 0 && pids[rpmI ].active) ? pids[rpmI ].val : 0.0f;
    float boost = (bstI  >= 0 && pids[bstI ].active) ? pids[bstI ].val : 0.0f;

    trip.distKm   += spd * dt;
    trip.fuelL    += frate * dt;
    trip.speedSum += spd;
    trip.speedN++;
    if (spd   > trip.maxSpeedKmh) trip.maxSpeedKmh = spd;
    if (rpm   > trip.maxRpmRpm)   trip.maxRpmRpm   = rpm;
    if (boost > trip.maxBoostKPa) trip.maxBoostKPa = boost;
}

void drawTrip() {
    const int Y0 = 14;
    spr.fillRect(0, Y0, SCREEN_W, SCREEN_H - Y0, C_BG);

    /* Header */
    spr.fillRect(0, Y0, SCREEN_W, 20, 0x0821u);
    spr.fillRect(0, Y0, 4, 20, C_ACCENT);
    spr.setTextSize(2); spr.setTextColor(C_ACCENT, 0x0821u);
    spr.setCursor(8, Y0 + 2); spr.print("TRIP COMPUTER");
    spr.setTextSize(1); spr.setTextColor(C_DIM, 0x0821u);
    spr.setCursor(SCREEN_W - 116, Y0 + 7); spr.print("Hold 2s = reset");

    /* Derived values */
    float elapsedS = (millis() - trip.startMs) / 1000.0f;
    float avgSpd   = (trip.speedN > 0) ? trip.speedSum / (float)trip.speedN : 0.0f;
    float l100km   = (trip.distKm > 0.1f) ? (trip.fuelL / trip.distKm * 100.0f) : 0.0f;
    int   h        = (int)(elapsedS / 3600);
    int   m        = (int)(fmod((double)elapsedS, 3600.0) / 60);

    struct TCell { const char* lbl; char val[12]; const char* unit; };
    TCell cells[8];
    snprintf(cells[0].val, 12, "%.1f",  (double)trip.distKm);     cells[0].lbl="Distance"; cells[0].unit="km";
    snprintf(cells[1].val, 12, "%.2f",  (double)trip.fuelL);      cells[1].lbl="Fuel Used"; cells[1].unit="L";
    snprintf(cells[2].val, 12, "%.1f",  (double)l100km);          cells[2].lbl="Economy";  cells[2].unit="L/100";
    snprintf(cells[3].val, 12, "%d:%02d", h, m);                  cells[3].lbl="Trip Time"; cells[3].unit="h:m";
    snprintf(cells[4].val, 12, "%.0f",  (double)avgSpd);          cells[4].lbl="Avg Speed"; cells[4].unit="km/h";
    snprintf(cells[5].val, 12, "%.0f",  (double)trip.maxSpeedKmh);cells[5].lbl="Max Speed"; cells[5].unit="km/h";
    snprintf(cells[6].val, 12, "%.0f",  (double)trip.maxRpmRpm);  cells[6].lbl="Peak RPM";  cells[6].unit="rpm";
    snprintf(cells[7].val, 12, "%.0f",  (double)trip.maxBoostKPa);cells[7].lbl="Pk Boost";  cells[7].unit="kPa";

    const int COLS = 4, ROWS = 2;
    const int GY   = Y0 + 22;
    const int CW   = SCREEN_W / COLS;
    const int CH   = (SCREEN_H - GY - 6) / ROWS;

    for (int i = 0; i < 8; i++) {
        int cx = (i % COLS) * CW;
        int cy = GY + (i / COLS) * CH;

        /* Label */
        spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
        spr.setCursor(cx + 6, cy + 5); spr.print(cells[i].lbl);

        /* Value */
        spr.setTextSize(3); spr.setTextColor(C_VALUE, C_BG);
        spr.setCursor(cx + 6, cy + 18); spr.print(cells[i].val);

        /* Unit — bottom right */
        spr.setTextSize(1); spr.setTextColor(C_LABEL, C_BG);
        int uw = strlen(cells[i].unit) * 6;
        spr.setCursor(cx + CW - uw - 4, cy + CH - 10);
        spr.print(cells[i].unit);

        /* Bottom divider */
        spr.drawFastHLine(cx, cy + CH - 1, CW, C_LINE);
    }
    /* Column dividers */
    for (int c = 1; c < COLS; c++)
        spr.drawFastVLine(c * CW, GY + 2, SCREEN_H - GY - 8, C_LINE);
    /* Row divider */
    spr.drawFastHLine(4, GY + CH, SCREEN_W - 8, C_LINE);
}

/* ── Critical alert banner — drawn over page content ─────────── */
void drawAlertBanner() {
    const char* msg = nullptr;
    uint16_t    col = C_DANGER;

    int coolI  = pidIdx(0x05), voltI = pidIdx(0x42);
    int boostI = pidIdx(0x6F), oilI  = pidIdx(0x5C);

    if      (coolI  >= 0 && pids[coolI ].active && pids[coolI ].val > (float)(cfg.coolWarn + 15))
        msg = "\x01 ENGINE OVERHEATING \x01";
    else if (oilI   >= 0 && pids[oilI  ].active && pids[oilI  ].val > 135.0f)
        msg = "\x01 OIL TEMP CRITICAL \x01";
    else if (voltI  >= 0 && pids[voltI ].active && pids[voltI ].val < 11.5f)
      { msg = "\x01 LOW BATTERY VOLTAGE \x01"; col = C_WARN; }
    else if (boostI >= 0 && pids[boostI].active && pids[boostI].val > 210.0f)
      { msg = "\x01 HIGH BOOST \x01"; col = C_WARN; }

    if (!msg) return;

    const int BY = 14, BH = 18;
    bool pulse = (millis() / 400) & 1;
    uint16_t bg = pulse ? col : (uint16_t)(col >> 1);
    spr.fillRect(0, BY, SCREEN_W, BH, bg);
    spr.setTextSize(2); spr.setTextColor(C_TEXT, bg);
    int tw = strlen(msg) * 12;
    spr.setCursor(SCREEN_W / 2 - tw / 2, BY + 1);
    spr.print(msg);
}


void draw() {
    switch(currentPage){
        case 0: drawDashboard(); break;
        case 1: drawEngine();    break;
        case 2: drawFuel();      break;
        case 3: drawAllOBD();    break;
        case 4: drawDTC();       break;
        case 5: drawWiFi();      break;
        case 6: drawSettings();  break;
        case 7: drawTrip();      break;
        case 8: drawRace();      break;
    }
    if (pickerOpen) drawPicker();
    drawAlertBanner();
    drawStatusBar();
}

/* ═══════════════════════════════════════════════════════════════════
 *  Touch  —  polled I2C mode (no INT pin dependency)
 *
 *  AXS15231B: write 0xD0,0x00 then read 14 bytes.
 *  buf[1] = touch point count.  X/Y from AXS_GET_POINT_X/Y macros.
 *
 *  Raw values (tchRawX/Y) updated every frame while touching.
 * ═══════════════════════════════════════════════════════════════════ */
static bool     tchDown     = false;
static int      tchLx       = 0;      // mapped screen-X saved at finger-down
static int      tchLy       = 0;      // mapped screen-Y saved at finger-down
static uint32_t tchDownTs   = 0;
static bool     tchHoldDone = false;

/* Live raw + mapped position (updated every frame while finger is down) */
int  tchRawX = 0, tchRawY = 0;
int  tchMapX = 0, tchMapY = 0;
bool tchActive = false;

/* I2C diagnostics — shown on touch test page */
uint8_t tchI2CErr  = 0xFF;   // endTransmission result (0=OK)
uint8_t tchGot     = 0;      // bytes received from requestFrom
uint8_t tchBuf6[6] = {};     // first 6 raw bytes for display

/* ── FreeRTOS touch task — reads I2C within microseconds of INT firing ── */
static SemaphoreHandle_t  tchSem   = NULL;
static volatile int16_t   tchTaskX = 0, tchTaskY = 0;
static volatile bool      tchTaskOK = false;

void IRAM_ATTR touchISR() {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(tchSem, &woken);
    portYIELD_FROM_ISR(woken);
}

/* Magic 8-byte command the AXS15231B touch IC requires before each read */
static const uint8_t kTchCmd[8] = {0xb5,0xab,0xa5,0x5a,0x00,0x00,0x00,0x08};

/* Runs on Core 0. Blocks on semaphore, reads I2C the instant INT fires. */
void touchTask(void* arg) {
    for (;;) {
        xSemaphoreTake(tchSem, portMAX_DELAY);

        uint8_t buf[8] = {};
        Wire.beginTransmission(0x3B);
        Wire.write(kTchCmd, 8);
        uint8_t err = Wire.endTransmission();          // STOP (not repeated-start)
        uint8_t got = Wire.requestFrom((uint8_t)0x3B, (uint8_t)8);
        Wire.readBytes(buf, got < 8 ? got : 8);

        tchI2CErr = err; tchGot = got;
        for (int i = 0; i < 6; i++) tchBuf6[i] = buf[i];

        Serial.printf("[TCH] err=%d got=%d  %02X %02X %02X %02X %02X %02X\n",
                      err, got, buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);

        if (got >= 6) {
            int16_t rx = (int16_t)(((buf[2] & 0x0F) << 8) | buf[3]);
            int16_t ry = (int16_t)(((buf[4] & 0x0F) << 8) | buf[5]);
            if (rx > 0 || ry > 0) {   // ignore (0,0) noise
                tchTaskX  = rx;
                tchTaskY  = ry;
                tchTaskOK = true;
                Serial.printf("[TCH] rawX=%d rawY=%d\n", rx, ry);
            }
        }
    }
}

/* Finger-up is timeout-based: AXS15231B pulses INT briefly per update,
 * so digitalRead(INT)==LOW is unreliable for sustained hold detection.
 * If no touch event arrives for TCH_RELEASE_MS, finger is considered up. */
#define TCH_RELEASE_MS 200

void pollTouch() {
    static uint32_t hbTs        = 0;
    static uint32_t tchLastEvMs = 0;

    if (millis() - hbTs > 2000) {
        Serial.printf("[TCH] alive  down=%d mapX=%d page=%d\n",
                      (int)tchDown, tchMapX, currentPage);
        hbTs = millis();
    }

    /* ── Process new touch data from task ───────────────────────── */
    if (tchTaskOK) {
        tchTaskOK   = false;
        tchRawX     = tchTaskX; tchRawY = tchTaskY;
        tchMapX     = constrain(map(tchRawX, TOUCH_MAX_X, TOUCH_MIN_X, 0, SCREEN_W-1), 0, SCREEN_W-1);
        tchMapY     = constrain(map(tchRawY, TOUCH_MAX_Y, TOUCH_MIN_Y, 0, SCREEN_H-1), 0, SCREEN_H-1);
        tchLastEvMs = millis();

        if (!tchDown) {                        // first event = finger down
            tchLx       = tchMapX;
            tchLy       = tchMapY;
            tchDown     = true;
            tchDownTs   = millis();
            tchHoldDone = false;
            pickerPrevY = tchMapY;
            pickerScrollAcc = 0;
        }

        /* Real-time picker scroll — track incremental Y drag */
        if (pickerOpen) {
            int dy = tchMapY - pickerPrevY;
            pickerScrollAcc += dy;
            const int STEP = 20;
            while (pickerScrollAcc <= -STEP) { pickerSelIdx = min(pickerSelIdx+1, N_PIDS-1); pickerScrollAcc += STEP; }
            while (pickerScrollAcc >=  STEP) { pickerSelIdx = max(pickerSelIdx-1, 0);         pickerScrollAcc -= STEP; }
        }
        pickerPrevY = tchMapY;
    }

    tchActive = tchDown;

    /* ── Long hold (2 s) ────────────────────────────────────────── */
    if (tchDown && !tchHoldDone && millis() - tchDownTs >= 2000) {
        tchHoldDone = true;
        if (pickerOpen) {
            pickerOpen = false;                // cancel picker
        } else if (currentPage == 4) {
            if (demoMode) { dtcCount = 0; dtcRead = true; }
            else          obdClearDTC();
        } else if (currentPage == 5) {
            /* WiFi page — long-hold toggles AP */
            if (wifiOn) { dnsServer.stop(); webServer.stop(); WiFi.softAPdisconnect(true); wifiOn=false; Serial.println("[WiFi] AP stopped"); }
            else        { startWiFiAP(); }
        } else if (currentPage == 6) {
            /* Settings page — long-hold resets all settings to defaults */
            cfg = { 8000, 260, 250, 95, 0, 1 };
            saveSettings();
            Serial.println("[Settings] Reset to defaults");
        } else if (currentPage == 7) {
            /* Trip page — long-hold resets trip */
            trip = {};
            trip.startMs = millis();
            tripLastTs   = millis();
            Serial.println("[Trip] Reset");
        } else if (currentPage == 8) {
            /* Race page — long-hold resets 0-100 result and peaks */
            launchState  = L_IDLE;
            launchResult = 0.0f;
            trip.maxRpmRpm = 0; trip.maxSpeedKmh = 0; trip.maxBoostKPa = 0;
            raceResetTs  = millis();
            Serial.println("[Race] Reset");
        }
    }

    /* ── Finger up (timeout) ────────────────────────────────────── */
    if (tchDown && millis() - tchLastEvMs > TCH_RELEASE_MS) {
        tchDown = false;
        int dx = tchMapX - tchLx;   // total horizontal travel
        int dy = tchMapY - tchLy;   // total vertical travel

        if (!tchHoldDone) {
            if (pickerOpen) {
                /* Tap (little movement) = confirm current selection */
                if (abs(dy) < 20 && abs(dx) < 40) {
                    uint8_t newId = pids[pickerSelIdx].id;
                    if      (pickerPage == 0) dashIds[pickerSlot]   = newId;
                    else if (pickerPage == 1) engineIds[pickerSlot] = newId;
                    else                      fuelIds[pickerSlot]   = newId;
                    Serial.printf("[PICK] slot=%d -> 0x%02X %s\n",
                                  pickerSlot, newId, pids[pickerSelIdx].label);
                    pickerOpen = false;
                }
                /* Large vertical drag = just scrolled, keep picker open */
            } else if (abs(dx) > 45) {
                /* Horizontal swipe = page change */
                if (dx < 0) currentPage = (currentPage + 1) % N_PAGES;
                else        currentPage = (currentPage - 1 + N_PAGES) % N_PAGES;
                Serial.printf("[TCH] swipe dx=%d -> page=%d\n", dx, currentPage);
            } else if (abs(dy) < 25 && abs(dx) < 45 && currentPage == 5) {
                /* Tap on WiFi page = start AP if off */
                if (!wifiOn) startWiFiAP();
            } else if (abs(dy) < 30 && abs(dx) < 45 && currentPage == 6) {
                /* Tap on Settings row = cycle that setting */
                const int ROW_H = (SCREEN_H - 14) / 6;
                int row = (tchLy - 14) / ROW_H;
                if (row >= 0 && row < 6) {
                    settingCycle(row);
                    saveSettings();
                    Serial.printf("[Settings] row=%d cycled\n", row);
                }
            } else if (abs(dy) < 25 && abs(dx) < 45 && currentPage <= 2) {
                /* Tap on tile (pages 0-2) = open picker for that slot */
                int slot = 0;
                if (currentPage == 0) {
                    const int HW = SCREEN_W/2, HH = (SCREEN_H-14)/2;
                    slot = (tchLx >= HW ? 1 : 0) + (tchLy >= 14+HH ? 2 : 0);
                } else {
                    const int CW = SCREEN_W/4, CH = (SCREEN_H-14)/2;
                    slot = (tchLx / CW) + (tchLy >= 14+CH ? 4 : 0);
                }
                /* Initialise pickerSelIdx to currently assigned PID */
                uint8_t curId = (currentPage==0) ? dashIds[slot] :
                                (currentPage==1) ? engineIds[slot] : fuelIds[slot];
                pickerSelIdx = 0;
                for (int i=0; i<N_PIDS; i++) if (pids[i].id==curId) { pickerSelIdx=i; break; }
                pickerSlot = slot; pickerPage = currentPage; pickerOpen = true;
                pickerScrollAcc = 0;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  WiFi AP + Web Configurator
 * ═══════════════════════════════════════════════════════════════════ */

/* ── HTML helpers ─────────────────────────────────────────────── */
static void wifiSendPidOptions(uint8_t selId) {
    for (int i = 0; i < N_PIDS; i++) {
        char buf[80];
        snprintf(buf, sizeof(buf),
                 "<option value='%d'%s>%s (%s)</option>",
                 pids[i].id,
                 pids[i].id == selId ? " selected" : "",
                 pids[i].label, pids[i].unit);
        webServer.sendContent(buf);
    }
}

static void wifiSendSlotRow(const char* name, int idx, uint8_t selId) {
    char buf[120];
    snprintf(buf, sizeof(buf),
             "<tr><td>%s %d</td><td><select name='%s%d'>",
             name, idx+1, name, idx);
    webServer.sendContent(buf);
    wifiSendPidOptions(selId);
    webServer.sendContent("</select></td></tr>");
}

void handleRoot() {
    /* Send in chunks to avoid large String allocations */
    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer.send(200, "text/html", "");

    webServer.sendContent(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>OBD2 Config</title>"
        "<style>"
        "body{background:#111;color:#ddd;font-family:sans-serif;margin:0;padding:16px}"
        "h2{color:#0af;margin-top:0}h3{color:#888;margin:12px 0 4px}"
        "table{border-collapse:collapse;width:100%;max-width:500px}"
        "td{padding:6px 8px;border-bottom:1px solid #333}"
        "td:first-child{color:#888;width:110px}"
        "select{background:#222;color:#fff;border:1px solid #444;padding:4px 6px;"
        "border-radius:4px;width:200px}"
        "input[type=submit]{background:#0af;color:#000;border:none;padding:10px 28px;"
        "font-size:1em;border-radius:6px;margin-top:16px;cursor:pointer}"
        "input[type=submit]:hover{background:#08d}"
        "</style></head><body>"
        "<h2>OBD2 Reader Config</h2>"
        "<form method='POST' action='/save'>"
    );

    webServer.sendContent("<h3>Dashboard (4 tiles)</h3><table>");
    for (int i = 0; i < 4; i++) wifiSendSlotRow("d", i, dashIds[i]);
    webServer.sendContent("</table>");

    webServer.sendContent("<h3>Engine page (8 tiles)</h3><table>");
    for (int i = 0; i < 8; i++) wifiSendSlotRow("e", i, engineIds[i]);
    webServer.sendContent("</table>");

    webServer.sendContent("<h3>Fuel page (8 tiles)</h3><table>");
    for (int i = 0; i < 8; i++) wifiSendSlotRow("f", i, fuelIds[i]);
    webServer.sendContent("</table>");

    webServer.sendContent(
        "<br><input type='submit' value='Save &amp; Apply'>"
        "</form></body></html>"
    );
    webServer.sendContent("");
}

void handleSave() {
    prefs.begin("obd2cfg", false);
    char key[4];
    for (int i = 0; i < 4; i++) {
        snprintf(key, sizeof(key), "d%d", i);
        if (webServer.hasArg(key)) {
            dashIds[i] = (uint8_t)webServer.arg(key).toInt();
            prefs.putUChar(key, dashIds[i]);
        }
    }
    for (int i = 0; i < 8; i++) {
        snprintf(key, sizeof(key), "e%d", i);
        if (webServer.hasArg(key)) {
            engineIds[i] = (uint8_t)webServer.arg(key).toInt();
            prefs.putUChar(key, engineIds[i]);
        }
        snprintf(key, sizeof(key), "f%d", i);
        if (webServer.hasArg(key)) {
            fuelIds[i] = (uint8_t)webServer.arg(key).toInt();
            prefs.putUChar(key, fuelIds[i]);
        }
    }
    prefs.end();
    Serial.println("[WiFi] Config saved");
    webServer.sendHeader("Location", "/");
    webServer.send(303);
}

void loadSettings() {
    Preferences sp;
    sp.begin("obd2set", true);
    cfg.rpmMax   = sp.getUShort("rpmMax",   8000);
    cfg.speedMax = sp.getUShort("speedMax", 260);
    cfg.mapMax   = sp.getUShort("mapMax",   250);
    cfg.coolWarn = sp.getUChar("coolWarn",  95);
    cfg.tempUnit  = sp.getUChar("tempUnit",  0);
    cfg.pressUnit = sp.getUChar("pressUnit", 1);  // default: bar
    sp.end();
    Serial.println("[Settings] Loaded");
}

void loadPrefs() {
    loadSettings();
    prefs.begin("obd2cfg", true);
    char key[4];
    for (int i = 0; i < 4; i++) {
        snprintf(key, sizeof(key), "d%d", i);
        dashIds[i] = prefs.getUChar(key, dashIds[i]);
    }
    for (int i = 0; i < 8; i++) {
        snprintf(key, sizeof(key), "e%d", i);
        engineIds[i] = prefs.getUChar(key, engineIds[i]);
        snprintf(key, sizeof(key), "f%d", i);
        fuelIds[i] = prefs.getUChar(key, fuelIds[i]);
    }
    prefs.end();
    Serial.println("[Prefs] Loaded slot config");
}

void startWiFiAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    delay(100);   // wait for AP IP to be assigned before starting DNS

    /* DNS server: redirect ALL queries to this device (192.168.4.1)
       so obd2.local (and any other hostname) resolves correctly in AP mode */
    dnsServer.start(53, "*", WiFi.softAPIP());

    webServer.on("/",      HTTP_GET,  handleRoot);
    webServer.on("/save",  HTTP_POST, handleSave);
    webServer.begin();
    wifiOn = true;
    Serial.printf("[WiFi] AP started  SSID=%s  IP=%s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

/* ── QR code renderer — uses QRCodeGenerator by felixerdy ── */
void drawQRCode(int x, int y, const char* text, int moduleSize) {
    QRCode qrcode;
    /* version 2 = 25×25 modules — fits URLs up to ~28 bytes with ECC_LOW */
    uint8_t qrBuf[qrcode_getBufferSize(2)];

    qrcode_initText(&qrcode, qrBuf, 2, ECC_LOW, text);

    int sz = qrcode.size;
    int px = sz * moduleSize;
    spr.fillRect(x-3, y-3, px+6, px+6, 0xFFFFu);   // white quiet zone
    for (int ry = 0; ry < sz; ry++)
        for (int rx = 0; rx < sz; rx++) {
            uint16_t c = qrcode_getModule(&qrcode, rx, ry) ? 0x0000u : 0xFFFFu;
            spr.fillRect(x + rx*moduleSize, y + ry*moduleSize, moduleSize, moduleSize, c);
        }
}

/* ── Page 6: WiFi Config ──────────────────────────────────────── */
void drawWiFi() {
    const int Y0  = 14;
    const int QSZ = 5;               // pixels per QR module
    const int Y1  = Y0 + 4;         // top of content area
    spr.fillRect(0, Y0, SCREEN_W, SCREEN_H-Y0, C_BG);

    if (!wifiOn) {
        /* ── OFF state — large text for easy reading ── */
        // "WiFi AP OFF" = 11 chars × 24px (size 4) = 264px → center at 320-132=188
        spr.setTextSize(4);
        spr.setTextColor(C_DIM, C_BG);
        spr.setCursor(SCREEN_W/2 - 132, Y0+16);
        spr.print("WiFi AP OFF");

        // "Tap to enable" = 13 chars × 18px (size 3) = 234px → center at 320-117=203
        spr.setTextSize(3);
        spr.setTextColor(C_ACCENT, C_BG);
        spr.setCursor(SCREEN_W/2 - 117, Y0+70);
        spr.print("Tap to enable");

        // Small hint
        spr.setTextSize(1);
        spr.setTextColor(C_DIM, C_BG);
        spr.setCursor(SCREEN_W/2 - 84, Y0+124);
        spr.print("Hold 2s when running to stop");
    } else {
        /* ── ON state — text left (size 3), QR right ── */
        /* "http://obd2.local" → QR version 2 = 25×25 modules */
        const int QR_MOD = 25;
        int qrPx  = QR_MOD * QSZ;                     // 105 px
        int qrX   = SCREEN_W - qrPx - 8;              // 527
        int qrY   = Y1 + (SCREEN_H - Y1 - qrPx) / 2; // vertically centred

        drawQRCode(qrX, qrY, "http://192.168.4.1", QSZ);

        /* Label under QR */
        spr.setTextSize(1);
        spr.setTextColor(C_LABEL, C_BG);
        int lx = qrX + (qrPx - 72) / 2;
        spr.setCursor(lx, qrY + qrPx + 5);
        spr.print("192.168.4.1");

        /* Text panel — size 3 (18×24 px per char), 28px line spacing */
        // Value column aligned at label_x + 5 chars × 18px = 8+90 = 98
        const int TX = 8;    // label x
        const int VX = 98;   // value x
        const int LS = 28;   // line spacing

        spr.setTextSize(3);
        spr.setTextColor(C_OK,     C_BG);
        spr.setCursor(TX, Y1+4);
        spr.print("WiFi ON");

        spr.setTextColor(C_LABEL,  C_BG); spr.setCursor(TX, Y1+4+LS*1); spr.print("SSID");
        spr.setTextColor(C_VALUE,  C_BG); spr.setCursor(VX, Y1+4+LS*1); spr.print(WIFI_AP_SSID);

        spr.setTextColor(C_LABEL,  C_BG); spr.setCursor(TX, Y1+4+LS*2); spr.print("Pass");
        spr.setTextColor(C_VALUE,  C_BG); spr.setCursor(VX, Y1+4+LS*2); spr.print(WIFI_AP_PASS);

        spr.setTextColor(C_LABEL,  C_BG); spr.setCursor(TX, Y1+4+LS*3); spr.print("URL");
        spr.setTextColor(C_ACCENT, C_BG); spr.setCursor(VX, Y1+4+LS*3); spr.print("192.168.4.1");

        int clients = WiFi.softAPgetStationNum();
        spr.setTextColor(C_LABEL,              C_BG); spr.setCursor(TX, Y1+4+LS*4); spr.print("Dev");
        spr.setTextColor(clients>0?C_OK:C_DIM, C_BG); spr.setCursor(VX, Y1+4+LS*4);
        spr.printf("%d conn", clients);
    }
}


/* ═══════════════════════════════════════════════════════════════════
 *  setup()
 * ═══════════════════════════════════════════════════════════════════ */
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=== OBD2 READER BOOT ===");

    /* Load saved PID slot assignments from NVS */
    loadPrefs();
    trip.startMs = millis();
    tripLastTs   = millis();

    /* Backlight + button */
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);

    /* ── Step 1: QSPI display init ───────────────────────────── */
    Serial.println("[1] Display...");
    axs15231_init();

    /* ── Step 2: Framebuffer sprite ──────────────────────────── */
    Serial.println("[2] Sprite...");
    spr.createSprite(SCREEN_W, SCREEN_H);
    if (!spr.getPointer()) {
        Serial.println("FATAL: sprite alloc failed — enable OPI PSRAM!");
        while (true) { digitalWrite(TFT_BL, !digitalRead(TFT_BL)); delay(300); }
    }
    spr.setSwapBytes(true);

    /* First visible boot frame: display + memory done */
    bootFrame(2, "Initializing Touch I2C...");
    delay(200);

    /* ── Step 3: Touch I2C ───────────────────────────────────── */
    Serial.println("[3] Touch I2C...");
    /* Create semaphore and launch touch reader on Core 0 BEFORE enabling INT */
    tchSem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(touchTask, "touch", 3072, NULL, 20, NULL, 0);
    Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);
    Wire.setClock(100000);   // 100 kHz — more reliable than 400 kHz over PCB traces
    /* Wait for touch controller to boot after shared GPIO-16 reset */
    delay(300);
    pinMode(TOUCH_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TOUCH_INT), touchISR, FALLING);

    /* I2C bus scan — prints every device found */
    Serial.println("   I2C scan:");
    int tchFoundAddr = -1;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("     Found: 0x%02X\n", addr);
            if (tchFoundAddr < 0) tchFoundAddr = addr;
        }
    }
    if (tchFoundAddr < 0) Serial.println("     No I2C devices found!");

    /* Try address 0x3B explicitly */
    Wire.beginTransmission(0x3B);
    uint8_t tchErr = Wire.endTransmission();
    bool tchOK = (tchErr == 0);
    Serial.printf("   0x3B: %s (err=%d)   INT pin: %d\n",
                  tchOK ? "ACK" : "NACK", tchErr, digitalRead(TOUCH_INT));

    char bootMsg[64];
    snprintf(bootMsg, sizeof(bootMsg),
             tchOK ? "Touch OK (0x3B) -- Initializing CAN Bus..."
                   : "Touch NACK (err=%d, scan found 0x%02X) -- CAN Bus...",
             tchErr, tchFoundAddr < 0 ? 0 : tchFoundAddr);
    bootFrame(3, bootMsg);
    delay(200);

    /* ── Step 4: MCP2515 CAN controller ─────────────────────── */
    Serial.println("[4] MCP2515...");
    CAN_SPI.begin(MCP_SCK, MCP_MISO, MCP_MOSI, MCP_CS);
    mcp.reset();
    if (mcp.setBitrate(CAN_500KBPS, MCP_8MHZ) == MCP2515::ERROR_OK) {
        mcp.setNormalMode();
        canOK = true;
        Serial.println("   MCP2515: OK  CAN 500 kbps");
        bootFrame(4, "CAN Bus OK  --  Starting...");
    } else {
        Serial.println("   MCP2515: FAIL (check wiring, or change MCP_8MHZ to MCP_16MHZ)");
        bootFrame(4, "CAN Bus NOT FOUND  --  Starting in Demo mode...");
    }
    delay(400);

    /* ── Step 5: Ready ───────────────────────────────────────── */
    bootFrame(5, "System ready!  OBD2 scanning started.");
    delay(1000);

    fpsTimer = millis();
    Serial.println("=== RUNNING ===");
    Serial.println("Button: short tap=next page   long hold 1.5s=toggle demo");
    Serial.println("Touch:  left 1/3=prev   right 1/3=next   hold 2s on DTC=clear");
}

/* ═══════════════════════════════════════════════════════════════════
 *  loop()
 * ═══════════════════════════════════════════════════════════════════ */
void loop() {
    /* Poll touch FIRST — before any drawing — so INT is still LOW
     * when we read.  Frame push takes ~33 ms; if we poll after it
     * the chip may have already cleared the event register.        */
    pollTouch();
    if (wifiOn) { dnsServer.processNextRequest(); webServer.handleClient(); }

    if (!canOK && !demoMode && !demoUserSet) demoMode = true;

    if (demoMode) demoTick();
    else          obdTick();

    tripTick();
    launchTick();

    draw();
    lcd_PushFrame((uint16_t*)spr.getPointer());

    fpsCount++;
    if (millis() - fpsTimer >= 1000) {
        currentFps = fpsCount;
        int act=0; for(int i=0;i<N_PIDS;i++) if(pids[i].active) act++;
        Serial.printf("FPS:%d  heap:%d  CAN:%s  car:%s  demo:%s  PIDs:%d  tchINT:%d\n",
            currentFps, ESP.getFreeHeap(),
            canOK?"OK":"NO", carConn?"YES":"NO",
            demoMode?"ON":"OFF", act,
            digitalRead(TOUCH_INT));
        fpsCount=0; fpsTimer=millis();
    }

    /* ── Boot button ─────────────────────────────────────────────
     *   Short tap  (released < 1.5 s) → next page
     *   Hold 1.5 s (still pressed)    → toggle demo mode        */
    static int      lastBtn     = HIGH;
    static uint32_t btnDownTs   = 0;
    static bool     btnLongDone = false;
    int btn = digitalRead(PIN_BUTTON_1);

    if (btn==LOW && lastBtn==HIGH) { btnDownTs=millis(); btnLongDone=false; }

    if (btn==LOW && !btnLongDone && millis()-btnDownTs>=1500) {
        btnLongDone = true;
        demoUserSet = true;
        demoMode    = !demoMode;
        if (demoMode) { demoPhase=D_COLD_IDLE; demoPhaseTs=millis(); Serial.println("Demo ON"); }
        else          { for(int i=0;i<N_PIDS;i++) pids[i].active=false; Serial.println("Demo OFF"); }
    }

    if (btn==HIGH && lastBtn==LOW && !btnLongDone)
        currentPage = (currentPage+1) % N_PAGES;

    lastBtn = btn;
}
